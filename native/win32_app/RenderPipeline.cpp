#include "echo/win32_app/RenderPipeline.h"

#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <iterator>

namespace echo::win32_app {

namespace {

template <typename T>
void SafeRelease(T*& value) {
  if (value) {
    value->Release();
    value = nullptr;
  }
}

}  // namespace

RenderPipeline::~RenderPipeline() {
  Reset();
}

bool RenderPipeline::IsDeviceLossHResult(HRESULT hr) noexcept {
  return hr == D2DERR_RECREATE_TARGET || hr == DXGI_ERROR_DEVICE_REMOVED ||
         hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}

bool RenderPipeline::CreateFactoryIfNeeded() {
  if (d2dFactory_) return true;
  D2D1_FACTORY_OPTIONS options{};
#if defined(_DEBUG)
  options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#else
  options.debugLevel = D2D1_DEBUG_LEVEL_NONE;
#endif
  const HRESULT hr = D2D1CreateFactory(
      D2D1_FACTORY_TYPE_SINGLE_THREADED,
      __uuidof(ID2D1Factory1),
      &options,
      reinterpret_cast<void**>(&d2dFactory_));
  return SUCCEEDED(hr) && d2dFactory_ != nullptr;
}

bool RenderPipeline::CreateD3DAndD2DDevice() {
  // BGRA_SUPPORT 是 D2D 互操作的硬性要求。
  // 不开 DEBUG flag —— 在用户机器上加载 D3D11SDKLayers.dll 会失败导致设备创建失败。
  UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

  const D3D_FEATURE_LEVEL levels[] = {
      D3D_FEATURE_LEVEL_11_1,
      D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_10_1,
      D3D_FEATURE_LEVEL_10_0,
      D3D_FEATURE_LEVEL_9_3,
  };
  D3D_FEATURE_LEVEL selectedLevel{};

  // 先尝试 HARDWARE，失败回退到 WARP（软件渲染）。
  HRESULT hr = D3D11CreateDevice(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
      levels, static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
      &d3dDevice_, &selectedLevel, &d3dContext_);
  if (FAILED(hr)) {
    hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
        levels, static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
        &d3dDevice_, &selectedLevel, &d3dContext_);
  }
  if (FAILED(hr)) return false;

  hr = d3dDevice_->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice_));
  if (FAILED(hr)) return false;

  hr = d2dFactory_->CreateDevice(dxgiDevice_, &d2dDevice_);
  if (FAILED(hr)) return false;

  hr = d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &ctx_);
  if (FAILED(hr)) return false;

  ctx_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
  ctx_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  return true;
}

bool RenderPipeline::CreateSwapChainAndBackbuffer(HWND hwnd, UINT width, UINT height) {
  hwnd_ = hwnd;

  if (width == 0 || height == 0) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    width = static_cast<UINT>(rc.right - rc.left);
    height = static_cast<UINT>(rc.bottom - rc.top);
  }
  // SwapChain 不接受 0 尺寸；最小化时可能为 0，给最小 1x1 兜底。
  if (width == 0) width = 1;
  if (height == 0) height = 1;

  IDXGIAdapter* adapter = nullptr;
  HRESULT hr = dxgiDevice_->GetAdapter(&adapter);
  if (FAILED(hr)) return false;

  IDXGIFactory2* dxgiFactory = nullptr;
  hr = adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&dxgiFactory));
  SafeRelease(adapter);
  if (FAILED(hr)) return false;

  DXGI_SWAP_CHAIN_DESC1 desc{};
  desc.Width = width;
  desc.Height = height;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = 2;
  desc.Scaling = DXGI_SCALING_NONE;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;  // Win10+ 推荐翻转模型
  desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
  desc.Flags = 0;

  hr = dxgiFactory->CreateSwapChainForHwnd(
      d3dDevice_, hwnd, &desc, nullptr, nullptr, &swapChain_);
  SafeRelease(dxgiFactory);
  if (FAILED(hr)) return false;

  // 取 back buffer (IDXGISurface) 并绑定为 D2D 渲染目标。
  IDXGISurface* surface = nullptr;
  hr = swapChain_->GetBuffer(0, __uuidof(IDXGISurface), reinterpret_cast<void**>(&surface));
  if (FAILED(hr)) return false;

  const auto bmpProps = D2D1::BitmapProperties1(
      D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
  hr = ctx_->CreateBitmapFromDxgiSurface(surface, &bmpProps, &backbuffer_);
  SafeRelease(surface);
  if (FAILED(hr)) return false;

  ctx_->SetTarget(backbuffer_);
  return true;
}

bool RenderPipeline::InitializeHeadless() {
  if (!CreateFactoryIfNeeded()) return false;
  if (ctx_) return true;  // 已经初始化
  if (!CreateD3DAndD2DDevice()) {
    ReleaseDeviceResources();
    return false;
  }
  return true;
}

bool RenderPipeline::Initialize(HWND hwnd, UINT width, UINT height) {
  if (!InitializeHeadless()) return false;
  if (swapChain_) {
    // 已经为同一 hwnd 初始化过；交给 Resize 处理新尺寸。
    if (hwnd_ == hwnd) {
      return SUCCEEDED(Resize(width, height));
    }
    ReleaseSwapChainResources();
  }
  if (!CreateSwapChainAndBackbuffer(hwnd, width, height)) {
    ReleaseSwapChainResources();
    return false;
  }
  return true;
}

void RenderPipeline::ReleaseSwapChainResources() {
  if (ctx_) {
    ctx_->SetTarget(nullptr);
  }
  SafeRelease(backbuffer_);
  SafeRelease(swapChain_);
  hwnd_ = nullptr;
}

void RenderPipeline::ReleaseDeviceResources() {
  ReleaseSwapChainResources();
  SafeRelease(ctx_);
  SafeRelease(d2dDevice_);
  SafeRelease(dxgiDevice_);
  SafeRelease(d3dContext_);
  SafeRelease(d3dDevice_);
}

void RenderPipeline::Shutdown() {
  ReleaseDeviceResources();
  // Factory 保留，便于设备重建。
}

void RenderPipeline::Reset() {
  ReleaseDeviceResources();
  SafeRelease(d2dFactory_);
}

HRESULT RenderPipeline::Resize(UINT width, UINT height) {
  if (!swapChain_) return S_OK;  // headless 模式
  if (width == 0) width = 1;
  if (height == 0) height = 1;

  // 释放 backbuffer 引用前先解除 ctx 的目标绑定；否则 ResizeBuffers 会失败。
  if (ctx_) ctx_->SetTarget(nullptr);
  SafeRelease(backbuffer_);

  HRESULT hr = swapChain_->ResizeBuffers(
      0, width, height, DXGI_FORMAT_UNKNOWN, 0);
  if (FAILED(hr)) return hr;

  IDXGISurface* surface = nullptr;
  hr = swapChain_->GetBuffer(0, __uuidof(IDXGISurface), reinterpret_cast<void**>(&surface));
  if (FAILED(hr)) return hr;

  const auto bmpProps = D2D1::BitmapProperties1(
      D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
  hr = ctx_->CreateBitmapFromDxgiSurface(surface, &bmpProps, &backbuffer_);
  SafeRelease(surface);
  if (FAILED(hr)) return hr;

  ctx_->SetTarget(backbuffer_);
  return S_OK;
}

void RenderPipeline::SetDpi(float dpi) {
  if (ctx_) ctx_->SetDpi(dpi, dpi);
}

bool RenderPipeline::BeginFrame() {
  if (!ctx_ || !backbuffer_) return false;
  ctx_->SetTarget(backbuffer_);
  ctx_->BeginDraw();
  return true;
}

HRESULT RenderPipeline::EndFrame() {
  if (!ctx_) return E_FAIL;
  HRESULT hr = ctx_->EndDraw();
  if (FAILED(hr)) return hr;
  if (swapChain_) {
    hr = swapChain_->Present(1, 0);
  }
  return hr;
}

}  // namespace echo::win32_app
