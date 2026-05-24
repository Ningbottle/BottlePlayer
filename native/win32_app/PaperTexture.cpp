#include "echo/win32_app/PaperTexture.h"

#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <cmath>
#include <cstdint>
#include <vector>

namespace echo::win32_app {

namespace {

constexpr UINT kTileSize = 128;

// 32-bit hash → [0, 1) 伪随机。简单 LCG，足以生成纸纹颗粒。
inline float Hash01(uint32_t x) {
  x = (x ^ 61u) ^ (x >> 16);
  x *= 9u;
  x ^= x >> 4;
  x *= 0x27d4eb2du;
  x ^= x >> 15;
  return static_cast<float>(x & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
}

// 生成 128×128 BGRA 纸纹数据。
// 基色：Paper #f1ead8 (0xf1, 0xea, 0xd8)。
// 每像素叠加 ±10 灰度噪声 + 偶发深色斑点。
std::vector<uint8_t> GeneratePaperGrain() {
  std::vector<uint8_t> buf(kTileSize * kTileSize * 4);
  for (UINT y = 0; y < kTileSize; ++y) {
    for (UINT x = 0; x < kTileSize; ++x) {
      const uint32_t seed = (y * kTileSize + x) * 2654435761u;
      const float n = Hash01(seed);                // 主噪声
      const float n2 = Hash01(seed ^ 0xDEADBEEFu); // 次噪声（斑点）

      // ±10 灰度抖动
      int delta = static_cast<int>((n - 0.5f) * 22.0f);
      // 5% 概率产生 -25 的深色斑点（模拟纸纤维）
      if (n2 > 0.95f) delta -= 18;
      // 1% 概率产生 +12 高光颗粒
      if (n2 < 0.01f) delta += 12;

      auto clamp = [](int v) -> uint8_t {
        if (v < 0) return 0;
        if (v > 255) return 255;
        return static_cast<uint8_t>(v);
      };

      const int b = clamp(0xd8 + delta);
      const int g = clamp(0xea + delta);
      const int r = clamp(0xf1 + delta);
      const std::size_t idx = (y * kTileSize + x) * 4;
      buf[idx + 0] = static_cast<uint8_t>(b);
      buf[idx + 1] = static_cast<uint8_t>(g);
      buf[idx + 2] = static_cast<uint8_t>(r);
      buf[idx + 3] = 0xFF;
    }
  }
  return buf;
}

}  // namespace

PaperTexture::~PaperTexture() {
  Shutdown();
}

bool PaperTexture::Initialize(ID2D1DeviceContext* ctx) {
  Shutdown();
  ctx_ = ctx;
  if (!ctx_) return false;

  const auto grain = GeneratePaperGrain();
  const auto props = D2D1::BitmapProperties1(
      D2D1_BITMAP_OPTIONS_NONE,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

  ID2D1Bitmap1* tmp = nullptr;
  HRESULT hr = ctx_->CreateBitmap(
      D2D1::SizeU(kTileSize, kTileSize),
      grain.data(),
      kTileSize * 4,
      props,
      &tmp);
  if (FAILED(hr) || !tmp) return false;
  bmp_ = tmp;  // ID2D1Bitmap1 继承 ID2D1Bitmap

  // BitmapBrush 用 EXTEND_WRAP 自然平铺；插值 NEAREST 保留颗粒锐度。
  const auto brushProps = D2D1::BitmapBrushProperties(
      D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP,
      D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
  hr = ctx_->CreateBitmapBrush(bmp_, brushProps, &brush_);
  if (FAILED(hr) || !brush_) {
    Shutdown();
    return false;
  }
  return true;
}

void PaperTexture::OnDeviceLost() {
  if (brush_) { brush_->Release(); brush_ = nullptr; }
  if (bmp_)   { bmp_->Release();   bmp_   = nullptr; }
  ctx_ = nullptr;
}

void PaperTexture::Shutdown() {
  OnDeviceLost();
}

void PaperTexture::FillRect(const D2D1_RECT_F& rect, float alpha) {
  if (!ctx_ || !brush_) return;
  brush_->SetOpacity(alpha);
  ctx_->FillRectangle(rect, brush_);
}

ID2D1Bitmap* PaperTexture::bitmap() const noexcept {
  return bmp_;
}

}  // namespace echo::win32_app
