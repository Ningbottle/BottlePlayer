#include "echo/win32_app/Theme.h"

namespace echo::win32_app {
namespace theme::color {

// 不透明 sRGB hex 与 D2D1::ColorF(uint32_t) 的对应见 d2d1helper.h::ColorF::ColorF(UINT32 rgb)。
// 带 alpha 的颜色使用显式 ColorF(r,g,b,a) 浮点构造，避免 hex 路径的 alpha 默认 1.0。

namespace {
constexpr float kRule34 = 34.0f / 255.0f;   // 0.1333
constexpr float kRule27 = 27.0f / 255.0f;   // 0.1059
constexpr float kRule18 = 18.0f / 255.0f;   // 0.0706

constexpr float kGlass248 = 248.0f / 255.0f;  // 0.9725
constexpr float kGlass243 = 243.0f / 255.0f;  // 0.9529
constexpr float kGlass230 = 230.0f / 255.0f;  // 0.9020

constexpr float kEdge255 = 255.0f / 255.0f;   // 1.0000
constexpr float kEdge252 = 252.0f / 255.0f;   // 0.9882
constexpr float kEdge243 = 243.0f / 255.0f;   // 0.9529
}  // namespace

const D2D1_COLOR_F& Paper() {
  static const D2D1_COLOR_F v = D2D1::ColorF(0xF1EAD8);
  return v;
}
const D2D1_COLOR_F& PaperAlt() {
  static const D2D1_COLOR_F v = D2D1::ColorF(0xEBE2CB);
  return v;
}
const D2D1_COLOR_F& PaperEdge() {
  static const D2D1_COLOR_F v = D2D1::ColorF(0xD8CDB1);
  return v;
}
const D2D1_COLOR_F& Ink() {
  static const D2D1_COLOR_F v = D2D1::ColorF(0x221B12);
  return v;
}
const D2D1_COLOR_F& InkSoft() {
  static const D2D1_COLOR_F v = D2D1::ColorF(0x4A3F2F);
  return v;
}
const D2D1_COLOR_F& InkMute() {
  static const D2D1_COLOR_F v = D2D1::ColorF(0x847460);
  return v;
}
const D2D1_COLOR_F& InkFaint() {
  static const D2D1_COLOR_F v = D2D1::ColorF(0xB5A98E);
  return v;
}
const D2D1_COLOR_F& Rule() {
  static const D2D1_COLOR_F v = D2D1::ColorF(kRule34, kRule27, kRule18, 0.14f);
  return v;
}
const D2D1_COLOR_F& RuleSoft() {
  static const D2D1_COLOR_F v = D2D1::ColorF(kRule34, kRule27, kRule18, 0.07f);
  return v;
}
const D2D1_COLOR_F& Accent() {
  static const D2D1_COLOR_F v = D2D1::ColorF(0xA8311B);
  return v;
}
const D2D1_COLOR_F& AccentDeep() {
  static const D2D1_COLOR_F v = D2D1::ColorF(0x7A2010);
  return v;
}
const D2D1_COLOR_F& GlassTint() {
  static const D2D1_COLOR_F v = D2D1::ColorF(kGlass248, kGlass243, kGlass230, 0.46f);
  return v;
}
const D2D1_COLOR_F& GlassTint2() {
  static const D2D1_COLOR_F v = D2D1::ColorF(kGlass248, kGlass243, kGlass230, 0.62f);
  return v;
}
const D2D1_COLOR_F& GlassEdge() {
  static const D2D1_COLOR_F v = D2D1::ColorF(kEdge255, kEdge252, kEdge243, 0.85f);
  return v;
}
const D2D1_COLOR_F& White() {
  static const D2D1_COLOR_F v = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
  return v;
}

}  // namespace theme::color

Palette MakeNewsprintPalette() {
  Palette p{};
  p.bg = theme::color::Paper();
  p.panel = theme::color::GlassTint();
  p.panelStrong = theme::color::GlassTint2();
  p.line = theme::color::Rule();
  p.text = theme::color::Ink();
  p.muted = theme::color::InkMute();
  p.faint = theme::color::InkFaint();
  p.accent = theme::color::Accent();
  p.accentDark = theme::color::AccentDeep();
  p.white = theme::color::White();
  return p;
}

}  // namespace echo::win32_app
