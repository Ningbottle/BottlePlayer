#include "echo/win32_app/Painter.h"

#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <dwrite.h>

#include "echo/win32_app/Theme.h"

namespace echo::win32_app {

Painter::~Painter() {
  Shutdown();
}

// ── 初始化 / 资源管理 ──────────────────────────────────────────────────────────

bool Painter::InitializeFonts(IDWriteFactory* wf) {
  writeFactory_ = wf;
  if (!wf) return false;

  // 衬线字体族。若 "Noto Serif SC" 未安装，DWrite 自动回退到系统字体
  // （CJK 走 STSong / SimSun，拉丁走 Times New Roman）。
  static const wchar_t kSerif[] = L"Noto Serif SC";

  // 辅助 lambda：创建一个 IDWriteTextFormat 并设置对齐与换行策略。
  auto Make = [&](const wchar_t* family,
                  DWRITE_FONT_WEIGHT weight,
                  DWRITE_FONT_STYLE  style,
                  float              size,
                  DWRITE_TEXT_ALIGNMENT      textAlign,
                  DWRITE_PARAGRAPH_ALIGNMENT paraAlign,
                  IDWriteTextFormat**        out) -> bool {
    HRESULT hr = wf->CreateTextFormat(
        family, nullptr,
        weight, style, DWRITE_FONT_STRETCH_NORMAL,
        size, L"zh-CN", out);
    if (FAILED(hr) || !*out) return false;
    (*out)->SetTextAlignment(textAlign);
    (*out)->SetParagraphAlignment(paraAlign);
    (*out)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    return true;
  };

  // Masthead：13px 半粗斜体，水平 + 垂直居中（用于 "BOTTLE TIMES — Vol. I"）
  if (!Make(kSerif,
            DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_ITALIC, 13.0f,
            DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
            &fmtMasthead_))
    return false;

  // SectionLabel：11px 斜体，左对齐（侧边栏分类小标签，如 "我的歌单 · Playlists"）
  if (!Make(kSerif,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_ITALIC, 11.0f,
            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
            &fmtSectionLabel_))
    return false;

  // PageHead：22px 粗体，左对齐（页面主标题）
  if (!Make(kSerif,
            DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, 22.0f,
            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
            &fmtPageHead_))
    return false;

  // SectionHead：20px 半粗正体，左对齐（section-bar h2，如 "为你推荐"）
  if (!Make(kSerif,
            DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, 20.0f,
            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
            &fmtSectionHead_))
    return false;

  // Kicker：10px 正体，左对齐（hero 顶部提要标签）
  if (!Make(kSerif,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, 10.0f,
            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
            &fmtKicker_))
    return false;

  return true;
}

void Painter::AttachContext(ID2D1DeviceContext* ctx) {
  DetachContext();
  ctx_ = ctx;
  if (!ctx_) return;

  ctx_->CreateSolidColorBrush(theme::color::Ink(),     &brushInk_);
  ctx_->CreateSolidColorBrush(theme::color::InkMute(), &brushMuted_);
  ctx_->CreateSolidColorBrush(theme::color::Rule(),    &brushRule_);
  ctx_->CreateSolidColorBrush(theme::color::Accent(),  &brushAccent_);
}

void Painter::DetachContext() {
  SafeRelease(brushInk_);
  SafeRelease(brushMuted_);
  SafeRelease(brushRule_);
  SafeRelease(brushAccent_);
  ctx_ = nullptr;
}

void Painter::Shutdown() {
  DetachContext();
  SafeRelease(fmtKicker_);
  SafeRelease(fmtPageHead_);
  SafeRelease(fmtSectionLabel_);
  SafeRelease(fmtSectionHead_);
  SafeRelease(fmtMasthead_);
  writeFactory_ = nullptr;
}

// ── 绘制原语 ──────────────────────────────────────────────────────────────────

void Painter::DoubleRule(float y, float x0, float x1) {
  if (!ctx_ || !brushInk_) return;
  // 粗线（主报头线）
  ctx_->DrawLine(D2D1::Point2F(x0, y),        D2D1::Point2F(x1, y),        brushInk_, 1.5f);
  // 细线（副报头线），间距 3.5px
  ctx_->DrawLine(D2D1::Point2F(x0, y + 3.5f), D2D1::Point2F(x1, y + 3.5f), brushInk_, 0.5f);
}

void Painter::DrawMasthead(const D2D1_RECT_F& rect, std::wstring_view title) {
  if (!ctx_ || !brushInk_ || !fmtMasthead_) return;

  // 上方双线
  DoubleRule(rect.top, rect.left, rect.right);

  // 居中斜体标题（双线之间的区域）
  const D2D1_RECT_F textRect = D2D1::RectF(
      rect.left, rect.top + 7.0f, rect.right, rect.bottom - 5.0f);
  ctx_->DrawText(
      title.data(), static_cast<UINT32>(title.size()),
      fmtMasthead_, textRect, brushInk_);

  // 下方双线
  DoubleRule(rect.bottom - 5.0f, rect.left, rect.right);
}

void Painter::SectionHead(float x, float y, float x1, std::wstring_view text) {
  if (!ctx_ || !brushInk_ || !fmtSectionHead_) return;
  // 20px 半粗衬线标题（section-bar h2）
  ctx_->DrawText(
      text.data(), static_cast<UINT32>(text.size()),
      fmtSectionHead_,
      D2D1::RectF(x, y, x1, y + 28.0f),
      brushInk_);
  // 下划 rule 线（section-bar border-bottom: 1px solid var(--rule)）
  if (brushRule_)
    ctx_->DrawLine(D2D1::Point2F(x, y + 28.0f), D2D1::Point2F(x1, y + 28.0f), brushRule_, 1.0f);
}

void Painter::SectionLabel(float x, float y, float x1, std::wstring_view text) {
  if (!ctx_ || !brushInk_ || !fmtSectionLabel_) return;
  // 11px 斜体衬线小标签（section-label，侧边栏分类）
  ctx_->DrawText(
      text.data(), static_cast<UINT32>(text.size()),
      fmtSectionLabel_,
      D2D1::RectF(x, y, x1, y + 22.0f),
      brushInk_);
  // 尾随 rule 线（section-label::after：flex:1, height:1px, background: rule-soft）
  if (brushRule_)
    ctx_->DrawLine(D2D1::Point2F(x, y + 11.0f), D2D1::Point2F(x1, y + 11.0f), brushRule_, 0.5f);
}

void Painter::PageHead(float x, float y, std::wstring_view text) {
  if (!ctx_ || !brushInk_ || !fmtPageHead_) return;
  ctx_->DrawText(
      text.data(), static_cast<UINT32>(text.size()),
      fmtPageHead_,
      D2D1::RectF(x, y, x + 640.0f, y + 32.0f),
      brushInk_);
}

void Painter::Kicker(float x, float y, std::wstring_view text) {
  if (!ctx_ || !brushMuted_ || !fmtKicker_) return;
  ctx_->DrawText(
      text.data(), static_cast<UINT32>(text.size()),
      fmtKicker_,
      D2D1::RectF(x, y, x + 320.0f, y + 18.0f),
      brushMuted_);
}

// ── 测量工具 ──────────────────────────────────────────────────────────────────

float Painter::MeasureSectionLabel(std::wstring_view text) {
  if (!writeFactory_ || !fmtSectionLabel_ || text.empty()) return 0.0f;
  IDWriteTextLayout* layout = nullptr;
  const HRESULT hr = writeFactory_->CreateTextLayout(
      text.data(), static_cast<UINT32>(text.size()),
      fmtSectionLabel_,
      1000.0f,  // 最大布局宽度（足够大，避免换行）
      100.0f,   // 最大布局高度
      &layout);
  if (FAILED(hr) || !layout) return 0.0f;
  DWRITE_TEXT_METRICS metrics{};
  layout->GetMetrics(&metrics);
  layout->Release();
  return metrics.width;
}

}  // namespace echo::win32_app
