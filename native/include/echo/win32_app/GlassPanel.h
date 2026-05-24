#pragma once

// GlassPanel — Newsprint 玻璃面板（List 19，按 docs 前端报告设计）
//
// 实现思路（D2D Effects 链）：
//   1. sceneBitmap (full-res) — 把 backbuffer 当前内容拷贝过来（含面板背后的所有像素）。
//   2. blurredBitmap (¼-res) — 当 blurDirty 为 true 时，用 GaussianBlur(22px) 写入此 bitmap；
//      静止帧直接复用上次结果，零开销。
//   3. ColorMatrix(saturation=1.36) — 提升模糊后的色彩饱和度，避免 "灰白褪色"。
//   4. composite — 把 blurred+saturated 画到目标 rect，再叠 glass tint + paper texture + edge。
//
// 此设计避免每帧重做模糊；只有在场景实际变化时（滚动/换页/缩放）才标记 blurDirty=true。
//
// 用法：
//   - InitializeFonts 之后 ctx 创建完时调用 Initialize(ctx, sceneW, sceneH)。
//   - 每帧 BeginDraw 之前调用 EnsureSceneSize 检查尺寸；变化时重建 sceneBitmap/blurredBitmap。
//   - 绘制玻璃面板时 DrawGlassPanel(rect, tint, optionalPaper)。
//   - 帧末（EndDraw 之前）若 blurDirty 仍未被消费，自动按需重新模糊（lazy）。
//   - 设备丢失：OnDeviceLost() 释放所有 D2D 资源。

#include <d2d1_1.h>

struct ID2D1Bitmap1;
struct ID2D1DeviceContext;
struct ID2D1Effect;
struct ID2D1SolidColorBrush;

namespace echo::win32_app {

class PaperTexture;

class GlassPanel {
 public:
  GlassPanel() = default;
  ~GlassPanel();

  GlassPanel(const GlassPanel&) = delete;
  GlassPanel& operator=(const GlassPanel&) = delete;

  // 初始化效果链 + 离屏 bitmap。
  // sceneW/sceneH 为窗口设备像素尺寸（即 backbuffer 大小）。
  // ctx 为非拥有引用，调用方负责寿命管理。
  bool Initialize(ID2D1DeviceContext* ctx, UINT sceneW, UINT sceneH);

  // 窗口尺寸变化时调用。若新尺寸与当前不同则重建 sceneBitmap/blurredBitmap。
  // 返回 true 表示当前已就绪可用。
  bool EnsureSceneSize(UINT sceneW, UINT sceneH);

  // 释放 D2D 资源（Effect / Bitmap / Brush），保留尺寸记录。
  void OnDeviceLost();

  // 完全释放（含尺寸记录）。析构时自动调用。
  void Shutdown();

  // 标记模糊需重算（场景内容已变更）。下一次 DrawGlassPanel 会触发重模糊。
  void MarkBlurDirty() noexcept { blurDirty_ = true; }

  // 在 player bar / drawer 等位置绘制毛玻璃面板。
  // - rect：目标矩形（backbuffer 坐标）。
  // - tint：玻璃色调（如 theme::GlassTint，含 alpha）。
  // - edgeColor：高光边色（theme::GlassEdge 等），alpha=0 则跳过边线。
  // - paper：纸纹（可空；非空则按 alpha 叠加到玻璃面板）。
  // - paperAlpha：纸纹强度（建议 0.18-0.32）。
  //
  // 内部流程：
  //   1. 若 blurDirty_ → CopyFromRenderTarget(backbuffer) 到 sceneBitmap，
  //      → 设 GaussianBlur 输入 → SetTarget(blurredBitmap) DrawImage(saturate)。
  //   2. 把 blurredBitmap 取样到 rect（用 D2D1_INTERPOLATION_MODE_LINEAR）。
  //   3. 叠 tint（半透明纯色） + paper（可选） + edge（顶 1px 高光线）。
  void DrawGlassPanel(const D2D1_RECT_F& rect,
                      const D2D1_COLOR_F& tint,
                      const D2D1_COLOR_F& edgeColor,
                      PaperTexture*       paper      = nullptr,
                      float               paperAlpha = 0.22f);

  // 访问器（主要供测试用）。
  ID2D1Bitmap1* scene_bitmap() const noexcept { return sceneBitmap_; }
  ID2D1Bitmap1* blurred_bitmap() const noexcept { return blurredBitmap_; }
  UINT scene_width() const noexcept { return sceneW_; }
  UINT scene_height() const noexcept { return sceneH_; }
  UINT blurred_width() const noexcept { return blurredW_; }
  UINT blurred_height() const noexcept { return blurredH_; }
  bool blur_dirty() const noexcept { return blurDirty_; }
  bool ready() const noexcept { return ctx_ != nullptr && sceneBitmap_ != nullptr && blurredBitmap_ != nullptr; }

 private:
  bool CreateBitmaps(UINT sceneW, UINT sceneH);
  void ReleaseBitmaps();
  bool CreateEffects();
  void ReleaseEffects();

  // 把 backbuffer 当前内容拷贝进 sceneBitmap，再模糊到 blurredBitmap。
  // 失败返回 false（例如 headless 无 backbuffer 时）。
  bool RebuildBlurIfNeeded();

  ID2D1DeviceContext*   ctx_           = nullptr;  // 非拥有

  // 全分辨率离屏 bitmap（保存场景内容，供模糊采样）
  ID2D1Bitmap1*         sceneBitmap_   = nullptr;
  // ¼ 分辨率离屏 bitmap（保存模糊结果）
  ID2D1Bitmap1*         blurredBitmap_ = nullptr;

  // 效果链：blur -> colorMatrix(saturate 1.36x)
  ID2D1Effect*          blurEffect_   = nullptr;
  ID2D1Effect*          colorMatrix_  = nullptr;

  // 复用的玻璃 tint / edge 笔刷（创建/销毁随 ctx）
  ID2D1SolidColorBrush* tintBrush_    = nullptr;
  ID2D1SolidColorBrush* edgeBrush_    = nullptr;

  UINT sceneW_   = 0;
  UINT sceneH_   = 0;
  UINT blurredW_ = 0;
  UINT blurredH_ = 0;

  bool blurDirty_ = true;
};

}  // namespace echo::win32_app
