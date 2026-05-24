#pragma once

// PaperTexture — Newsprint 报纸纹理 tile（List 19）
//
// 用途：为玻璃面板、Hero 卡片等区域提供报纸纹理叠加。
//
// 实现策略：
//   - 程序化生成 128×128 BGRA 噪声 tile（不打包 PNG，零磁盘 I/O；总常驻 ~64KB）。
//   - 一次生成、缓存为 ID2D1Bitmap；通过 BitmapBrush(EXTEND_WRAP) 平铺到任意 rect。
//   - 颜色基底为半透明纸黄（Paper #f1ead8 偏移 ±α），叠加在主色之上时呈现纸张颗粒感。
//
// 生命周期：
//   1. Initialize(ctx)        — D2D DeviceContext 就绪后调用，创建 Bitmap + BitmapBrush。
//   2. FillRect(rect, alpha)  — 在 BeginDraw/EndDraw 之间调用，平铺纸纹到 rect。
//   3. OnDeviceLost()         — 设备丢失时调用，释放 Bitmap + Brush。
//   4. Shutdown()             — 析构时自动调用。

#include <d2d1_1.h>

struct ID2D1Bitmap;
struct ID2D1BitmapBrush;
struct ID2D1DeviceContext;

namespace echo::win32_app {

class PaperTexture {
 public:
  PaperTexture() = default;
  ~PaperTexture();

  PaperTexture(const PaperTexture&) = delete;
  PaperTexture& operator=(const PaperTexture&) = delete;

  // 创建 128x128 程序化纸纹 Bitmap + BitmapBrush。
  // ctx 为非拥有引用；Painter 内部不调用 AddRef / Release。
  bool Initialize(ID2D1DeviceContext* ctx);

  // 设备丢失时调用，释放 Bitmap + Brush。
  void OnDeviceLost();

  // 释放所有资源。析构时自动调用。
  void Shutdown();

  // 在 BeginDraw/EndDraw 之间调用，平铺纸纹到 rect。
  // alpha 控制纸纹强度（0.0=完全透明，1.0=完全不透明，推荐 0.18-0.32）。
  void FillRect(const D2D1_RECT_F& rect, float alpha = 0.22f);

  // 访问器：返回缓存的 Bitmap（可能为 nullptr）。
  ID2D1Bitmap* bitmap() const noexcept;

 private:
  ID2D1DeviceContext* ctx_  = nullptr;  // 非拥有
  ID2D1Bitmap*        bmp_  = nullptr;
  ID2D1BitmapBrush*   brush_ = nullptr;
};

}  // namespace echo::win32_app
