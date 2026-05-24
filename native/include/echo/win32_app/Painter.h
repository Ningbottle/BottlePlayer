#pragma once

#include <d2d1.h>
#include <string_view>

// Forward declarations to avoid pulling heavy DWrite headers into every translation unit.
struct ID2D1DeviceContext;
struct ID2D1SolidColorBrush;
struct IDWriteFactory;
struct IDWriteTextFormat;

namespace echo::win32_app {

// Painter — Newsprint 报纸风格排版绘制助手
//
// 封装常用绘制原语（masthead 大标题、section-label 栏目标签、page-head 页头、
// kicker 提要、double-rule 双线），以及衬线字体格式缓存。
//
// 生命周期：
//   1. InitializeFonts(IDWriteFactory*)     — 一次创建 IDWriteTextFormat；与 D2D 设备无关，
//                                             设备丢失后无需重建。创建后 MeasureSectionLabel 可用。
//   2. AttachContext(ID2D1DeviceContext*)   — D2D DeviceContext 创建后调用；创建 D2D 笔刷。
//   3. Draw* 系列方法                       — 在 BeginDraw / EndDraw 之间调用。
//   4. DetachContext()                     — 设备丢失时调用；释放笔刷，保留 TextFormat。
//   5. Shutdown()                          — 析构时自动调用；释放所有资源。
//
// 字体策略：
//   - 主字体族 "Noto Serif SC"；DWrite 在目标系统按 Unicode 码点自动回退
//     （CJK 走 STSong / SimSun；拉丁走 Times New Roman）。
//   - 不打包字体文件（License 与体积考虑）。
//   - 所有 TextFormat 预创建并缓存，单帧无额外分配开销。
class Painter {
 public:
  Painter() = default;
  ~Painter();

  Painter(const Painter&) = delete;
  Painter& operator=(const Painter&) = delete;

  // 创建 IDWriteTextFormat 缓存（设备无关；InitializeFonts 成功后 MeasureSectionLabel 可用）。
  // wf 为非拥有引用；Painter 内部不调用 AddRef / Release。
  bool InitializeFonts(IDWriteFactory* wf);

  // 创建 D2D 笔刷（设备相关）。在 D2D DeviceContext 成功创建后调用。
  // ctx 为非拥有引用；不调用 AddRef / Release。
  void AttachContext(ID2D1DeviceContext* ctx);

  // 仅释放 D2D 笔刷；TextFormat 保留。设备丢失时调用。
  void DetachContext();

  // 释放所有资源（TextFormat + 笔刷）。析构时自动调用。
  void Shutdown();

  // ── 绘制原语（要求先调用 AttachContext）──────────────────────────────────

  // 两条水平墨线（粗 1.5px + 细 0.5px），间距 3.5px。
  void DoubleRule(float y, float x0, float x1);

  // 报纸 masthead：上下双线 + 居中斜体粗衬线标题。
  // rect 建议高度 28–34px；title 典型值为 "BOTTLE TIMES — Vol. I"。
  void DrawMasthead(const D2D1_RECT_F& rect, std::wstring_view title);

  // 节区大标题（section-bar h2）：20px 半粗衬线，墨色，下方 rule 线。
  // 用于内容区的 "为你推荐"、"最近播放"、"推荐歌单" 等主要区块标题。
  // x1 为 rule 线右端 x 坐标（通常为区块右边界）。
  void SectionHead(float x, float y, float x1, std::wstring_view text);

  // 栏目小标签（section-label）：11px 斜体衬线，墨色，带尾随 rule 线。
  // 用于侧边栏分类标签，如 "我的歌单 · Playlists"。
  void SectionLabel(float x, float y, float x1, std::wstring_view text);

  // 页头（page-head）：22px 粗体衬线，墨色。
  void PageHead(float x, float y, std::wstring_view text);

  // 提要（kicker）：10px 正体衬线，弱墨色。
  // 用于 hero 卡片顶部的小标签，如 "今日推荐"。
  void Kicker(float x, float y, std::wstring_view text);

  // ── 测量工具（仅需 InitializeFonts；不依赖 D2D 设备）────────────────────

  // 返回 SectionLabel 文本的 DWrite 布局宽度（DIP）。
  // 用于测试断言和精确排版对齐。
  float MeasureSectionLabel(std::wstring_view text);

 private:
  template <typename T>
  static void SafeRelease(T*& p) {
    if (p) {
      p->Release();
      p = nullptr;
    }
  }

  // DWrite 资源（设备无关；InitializeFonts 后持有）
  IDWriteFactory*    writeFactory_     = nullptr;  // 非拥有
  IDWriteTextFormat* fmtMasthead_      = nullptr;  // 13px 半粗斜体衬线，水平居中
  IDWriteTextFormat* fmtSectionHead_   = nullptr;  // 20px 半粗衬线，左对齐（section-bar h2）
  IDWriteTextFormat* fmtSectionLabel_  = nullptr;  // 11px 斜体衬线，左对齐（sidebar label）
  IDWriteTextFormat* fmtPageHead_      = nullptr;  // 22px 粗体衬线，左对齐
  IDWriteTextFormat* fmtKicker_        = nullptr;  // 10px 正体衬线，左对齐

  // D2D 设备相关资源（AttachContext 后持有）
  ID2D1DeviceContext*   ctx_          = nullptr;  // 非拥有
  ID2D1SolidColorBrush* brushInk_    = nullptr;  // theme::Ink  (#221b12)
  ID2D1SolidColorBrush* brushMuted_  = nullptr;  // theme::InkMute (#847460)
  ID2D1SolidColorBrush* brushRule_   = nullptr;  // theme::Rule (rgba 14% alpha)
  ID2D1SolidColorBrush* brushAccent_ = nullptr;  // theme::Accent (#a8311b)
};

}  // namespace echo::win32_app
