#pragma once

// D2D 渲染管线（List 16 地基）：把旧 `ID2D1HwndRenderTarget` 替换为
// `D3D11Device + DXGI SwapChain1 + ID2D1Device + ID2D1DeviceContext`。
// 升级动机是为 List 19 玻璃面板打开 D2D Effects (GaussianBlur/ColorMatrix/Shadow) 通路 ——
// Effects 仅对 `ID2D1DeviceContext` 可用，旧 HwndRenderTarget 没有 CreateEffect。
//
// 设计要点：
//  - 工厂（ID2D1Factory1）一次创建、与窗口/设备生命周期解耦，便于 device removal 后重建。
//  - DXGI swap chain 使用 FLIP_SEQUENTIAL 翻转模型 + B8G8R8A8_UNORM 像素格式，与
//    Direct2D 默认 PixelFormat 对齐；Win10+ 推荐路径。
//  - `InitializeHeadless()` 仅创建到 D2D DeviceContext 一级（无 HWND、无 swap chain），
//    供单元测试构造-销毁循环复用，避免在 CI / 烟雾测试里造真实窗口。
//  - 调用方在 `BeginDraw / EndDraw` 之间的所有绘制 API 与 HwndRenderTarget 完全兼容
//    （ID2D1DeviceContext 继承 ID2D1RenderTarget），无须批量改 ~160 处 renderTarget_->X()。
//  - 设备丢失（DXGI_ERROR_DEVICE_REMOVED / D2DERR_RECREATE_TARGET）通过 `EndFrame()`
//    返回值通知；调用方需在下一帧前调用 `Shutdown()` 与 `Initialize()` 重建。

#include <windows.h>

struct ID2D1Bitmap1;
struct ID2D1Device;
struct ID2D1DeviceContext;
struct ID2D1Factory1;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGIDevice;
struct IDXGISwapChain1;

namespace echo::win32_app {

class RenderPipeline {
 public:
  RenderPipeline() = default;
  ~RenderPipeline();

  RenderPipeline(const RenderPipeline&) = delete;
  RenderPipeline& operator=(const RenderPipeline&) = delete;
  RenderPipeline(RenderPipeline&&) = delete;
  RenderPipeline& operator=(RenderPipeline&&) = delete;

  // 仅创建设备链路（Factory1 + D3D11 + D2D Device + DeviceContext），
  // 不创建 swap chain / 后台缓冲。用于单元测试。
  // 成功后 `device_context()` 与 `factory()` 均非空。
  bool InitializeHeadless();

  // 完整初始化：在 `InitializeHeadless()` 之上追加 swap chain + 后台缓冲 bitmap 并绑定到
  // DeviceContext 的渲染目标。`width/height` 为设备像素，0 时按 GetClientRect 推算。
  bool Initialize(HWND hwnd, UINT width = 0, UINT height = 0);

  // 释放 swap chain / backbuffer / D2D DeviceContext / D2D Device / D3D11 / DXGI。
  // 保留 Factory1（Factory 与设备生命周期解耦）。
  void Shutdown();

  // 释放所有资源包括 Factory1。析构时自动调用。
  void Reset();

  // 处理 WM_SIZE。`width/height` 为新的设备像素尺寸。
  // 内部调用 IDXGISwapChain1::ResizeBuffers 并重建后台缓冲 bitmap。
  // 没有 swap chain（仅 headless）时为 no-op。
  HRESULT Resize(UINT width, UINT height);

  // 设置 DPI。等同于 ID2D1DeviceContext::SetDpi(dpi, dpi)。
  void SetDpi(float dpi);

  // 把绘制目标设为后台缓冲并调用 BeginDraw。返回 false 表示无 backbuffer（headless）。
  bool BeginFrame();

  // EndDraw + Present。返回 EndDraw 或 Present 的 HRESULT，调用方据此判断 device removal。
  // D2DERR_RECREATE_TARGET 与 DXGI_ERROR_DEVICE_REMOVED / RESET 均视为需重建。
  HRESULT EndFrame();

  // 访问器。Initialize/InitializeHeadless 失败后均返回 nullptr。
  ID2D1Factory1* factory() const noexcept { return d2dFactory_; }
  ID2D1DeviceContext* device_context() const noexcept { return ctx_; }
  IDXGISwapChain1* swap_chain() const noexcept { return swapChain_; }
  ID2D1Bitmap1* backbuffer() const noexcept { return backbuffer_; }

  bool has_device() const noexcept { return ctx_ != nullptr; }
  bool has_swap_chain() const noexcept { return swapChain_ != nullptr; }

  static bool IsDeviceLossHResult(HRESULT hr) noexcept;

 private:
  bool CreateFactoryIfNeeded();
  bool CreateD3DAndD2DDevice();
  bool CreateSwapChainAndBackbuffer(HWND hwnd, UINT width, UINT height);
  void ReleaseSwapChainResources();
  void ReleaseDeviceResources();

  // Factory 跨设备重建；其它字段在 Shutdown 时全部释放。
  ID2D1Factory1* d2dFactory_ = nullptr;

  ID3D11Device* d3dDevice_ = nullptr;
  ID3D11DeviceContext* d3dContext_ = nullptr;
  IDXGIDevice* dxgiDevice_ = nullptr;

  ID2D1Device* d2dDevice_ = nullptr;
  ID2D1DeviceContext* ctx_ = nullptr;

  IDXGISwapChain1* swapChain_ = nullptr;
  ID2D1Bitmap1* backbuffer_ = nullptr;

  HWND hwnd_ = nullptr;
};

}  // namespace echo::win32_app
