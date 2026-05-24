#include "echo/win32_app/GlassPanel.h"

#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <d2d1effects.h>
#include <d2d1effects_2.h>
#include <algorithm>
#include <cmath>

#include "echo/win32_app/PaperTexture.h"

namespace echo::win32_app {

namespace {

// 模糊半径（HTML 参考：filter: blur(22px)）。
constexpr float kBlurStandardDeviation = 22.0f;

// 饱和度提升系数（HTML 参考：filter: saturate(1.36)）。
constexpr float kSaturationStrength = 1.36f;

// 模糊位图缩放因子（¼ 分辨率即 25%）。
constexpr float kBlurDownscale = 0.25f;

// Rec.601 灰度系数（用于构造饱和度矩阵）。
constexpr float kLumR = 0.299f;
constexpr float kLumG = 0.587f;
constexpr float kLumB = 0.114f;

D2D1_MATRIX_5X4_F SaturationMatrix(float s) {
  const float inv = 1.0f - s;
  const float r = inv * kLumR;
  const float g = inv * kLumG;
  const float b = inv * kLumB;
  // 行向量：R' G' B' A'（D2D 的颜色矩阵规则）。
  return D2D1::Matrix5x4F(
      r + s, r,     r,     0.0f,
      g,     g + s, g,     0.0f,
      b,     b,     b + s, 0.0f,
      0.0f,  0.0f,  0.0f,  1.0f,
      0.0f,  0.0f,  0.0f,  0.0f);
}

template <typename T>
void SafeRelease(T*& p) {
  if (p) { p->Release(); p = nullptr; }
}

}  // namespace

GlassPanel::~GlassPanel() {
  Shutdown();
}

bool GlassPanel::Initialize(ID2D1DeviceContext* ctx, UINT sceneW, UINT sceneH) {
  Shutdown();
  if (!ctx || sceneW == 0 || sceneH == 0) return false;
  ctx_ = ctx;
  if (!CreateBitmaps(sceneW, sceneH)) {
    Shutdown();
    return false;
  }
  if (!CreateEffects()) {
    Shutdown();
    return false;
  }
  // 创建 tint / edge 复用笔刷（颜色由 DrawGlassPanel 时 SetColor）
  ctx_->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.0f), &tintBrush_);
  ctx_->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.0f), &edgeBrush_);
  blurDirty_ = true;
  return true;
}

bool GlassPanel::EnsureSceneSize(UINT sceneW, UINT sceneH) {
  if (!ctx_ || sceneW == 0 || sceneH == 0) return false;
  if (sceneW == sceneW_ && sceneH == sceneH_ && sceneBitmap_ && blurredBitmap_) return true;
  ReleaseBitmaps();
  if (!CreateBitmaps(sceneW, sceneH)) return false;
  blurDirty_ = true;
  return true;
}

bool GlassPanel::CreateBitmaps(UINT sceneW, UINT sceneH) {
  if (!ctx_) return false;

  // sceneBitmap：D2D1_BITMAP_OPTIONS_NONE，仅作普通离屏 bitmap（用 CopyFromRenderTarget 写入）。
  const auto sceneProps = D2D1::BitmapProperties1(
      D2D1_BITMAP_OPTIONS_NONE,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
  HRESULT hr = ctx_->CreateBitmap(D2D1::SizeU(sceneW, sceneH), nullptr, 0, sceneProps, &sceneBitmap_);
  if (FAILED(hr) || !sceneBitmap_) return false;

  // blurredBitmap：作为 DeviceContext 的 SetTarget 目标，需要 D2D1_BITMAP_OPTIONS_TARGET。
  blurredW_ = std::max<UINT>(1u, static_cast<UINT>(std::round(sceneW * kBlurDownscale)));
  blurredH_ = std::max<UINT>(1u, static_cast<UINT>(std::round(sceneH * kBlurDownscale)));
  const auto blurProps = D2D1::BitmapProperties1(
      D2D1_BITMAP_OPTIONS_TARGET,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
  hr = ctx_->CreateBitmap(D2D1::SizeU(blurredW_, blurredH_), nullptr, 0, blurProps, &blurredBitmap_);
  if (FAILED(hr) || !blurredBitmap_) {
    SafeRelease(sceneBitmap_);
    return false;
  }
  sceneW_ = sceneW;
  sceneH_ = sceneH;
  return true;
}

void GlassPanel::ReleaseBitmaps() {
  SafeRelease(sceneBitmap_);
  SafeRelease(blurredBitmap_);
  sceneW_ = sceneH_ = blurredW_ = blurredH_ = 0;
}

bool GlassPanel::CreateEffects() {
  if (!ctx_) return false;
  HRESULT hr = ctx_->CreateEffect(CLSID_D2D1GaussianBlur, &blurEffect_);
  if (FAILED(hr) || !blurEffect_) return false;
  blurEffect_->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, kBlurStandardDeviation);
  blurEffect_->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);

  hr = ctx_->CreateEffect(CLSID_D2D1ColorMatrix, &colorMatrix_);
  if (FAILED(hr) || !colorMatrix_) return false;
  colorMatrix_->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, SaturationMatrix(kSaturationStrength));
  colorMatrix_->SetInputEffect(0, blurEffect_);
  return true;
}

void GlassPanel::ReleaseEffects() {
  SafeRelease(colorMatrix_);
  SafeRelease(blurEffect_);
}

void GlassPanel::OnDeviceLost() {
  ReleaseEffects();
  ReleaseBitmaps();
  SafeRelease(tintBrush_);
  SafeRelease(edgeBrush_);
  ctx_ = nullptr;
  blurDirty_ = true;
}

void GlassPanel::Shutdown() {
  OnDeviceLost();
}

bool GlassPanel::RebuildBlurIfNeeded() {
  if (!ready()) return false;
  if (!blurDirty_) return true;

  // 1. 从当前渲染目标（应当是 backbuffer）抓取像素到 sceneBitmap。
  //    使用 CopyFromRenderTarget — 注意此处目标 RT 必须仍是 backbuffer，
  //    且未处于 EndDraw 之后的状态。
  ID2D1Image* prevTarget = nullptr;
  ctx_->GetTarget(&prevTarget);
  if (!prevTarget) return false;

  const D2D1_POINT_2U dstZero = D2D1::Point2U(0, 0);
  const D2D1_RECT_U srcRect = D2D1::RectU(0, 0, sceneW_, sceneH_);

  // sceneBitmap 必须以当前 ctx 的 RT 作为源。先 cast 到 ID2D1Bitmap1。
  // 注意：源/目标尺寸需匹配（CopyFromRenderTarget 需要 sceneBitmap 与 backbuffer 同尺寸）。
  HRESULT hr = sceneBitmap_->CopyFromRenderTarget(&dstZero, ctx_, &srcRect);
  prevTarget->Release();
  if (FAILED(hr)) {
    // 设备丢失或目标不匹配；下次再试。
    return false;
  }

  // 2. 把 colorMatrix(blur(scene)) 渲染进 blurredBitmap。
  blurEffect_->SetInput(0, sceneBitmap_);

  ID2D1Image* prev = nullptr;
  ctx_->GetTarget(&prev);
  ctx_->SetTarget(blurredBitmap_);

  // 子帧绘制：调用方此时已 BeginDraw 过，所以直接 Clear + DrawImage 即可。
  ctx_->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

  // 应用 0.25x 缩放，使 sceneBitmap (full-res) 被绘制到 ¼ 大小的 blurredBitmap 上。
  D2D1_MATRIX_3X2_F prevTransform;
  ctx_->GetTransform(&prevTransform);
  ctx_->SetTransform(D2D1::Matrix3x2F::Scale(kBlurDownscale, kBlurDownscale));
  ctx_->DrawImage(colorMatrix_, D2D1_INTERPOLATION_MODE_LINEAR);
  ctx_->SetTransform(prevTransform);

  ctx_->SetTarget(prev);
  if (prev) prev->Release();

  blurDirty_ = false;
  return true;
}

void GlassPanel::DrawGlassPanel(const D2D1_RECT_F& rect,
                                const D2D1_COLOR_F& tint,
                                const D2D1_COLOR_F& edgeColor,
                                PaperTexture* paper,
                                float paperAlpha) {
  if (!ready()) return;
  RebuildBlurIfNeeded();
  if (!blurredBitmap_) return;

  // 1) 把 blurredBitmap 的对应区域采样到 rect（线性插值），同时被 axis-aligned clip 限制到 rect。
  const float sx0 = std::max(0.0f, rect.left   * kBlurDownscale);
  const float sy0 = std::max(0.0f, rect.top    * kBlurDownscale);
  const float sx1 = std::min(static_cast<float>(blurredW_), rect.right  * kBlurDownscale);
  const float sy1 = std::min(static_cast<float>(blurredH_), rect.bottom * kBlurDownscale);
  const D2D1_RECT_F srcRect = D2D1::RectF(sx0, sy0, sx1, sy1);

  ctx_->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  ctx_->DrawBitmap(blurredBitmap_, &rect, 1.0f, D2D1_INTERPOLATION_MODE_LINEAR, &srcRect);

  // 2) 叠玻璃 tint（半透明纯色）
  if (tint.a > 0.0f && tintBrush_) {
    tintBrush_->SetColor(tint);
    ctx_->FillRectangle(rect, tintBrush_);
  }

  // 3) 叠纸纹（可选）
  if (paper && paperAlpha > 0.0f) {
    paper->FillRect(rect, paperAlpha);
  }

  ctx_->PopAxisAlignedClip();

  // 4) 顶部高光边线（玻璃面板上沿 1px）
  if (edgeColor.a > 0.0f && edgeBrush_) {
    edgeBrush_->SetColor(edgeColor);
    ctx_->DrawLine(
        D2D1::Point2F(rect.left,  rect.top + 0.5f),
        D2D1::Point2F(rect.right, rect.top + 0.5f),
        edgeBrush_, 1.0f);
  }
}

}  // namespace echo::win32_app
