#pragma once

#include <d2d1.h>

// BottleMusic Newsprint 视觉令牌（color tokens）。
//
// 数值来源：项目根目录 `Music Player.html` 的 :root CSS 变量。
// 设计基线：docs/REFERENCE.zh-CN.md §4bis "Newsprint UI 规格"（待补，由 List 20 完成）。
//
// 使用方式：
//   - 视图层取色：`theme::color::Paper()` / `theme::color::Accent()` 等访问器
//   - 兼容旧字段：`theme::MakeNewsprintPalette()` 返回 `Palette`，字段保持 bg/panel/text 等旧名以减少
//     MainWindow.cpp 现有 ~160 处 palette_.X 引用的重命名压力
//
// 颜色保持 `D2D1_COLOR_F`（线性 0..1 浮点）。RGBA hex 转换约定：
//   `D2D1::ColorF(0xRRGGBB)`              -> 不透明
//   `D2D1::ColorF(0xRRGGBB, alpha)`       -> 带 alpha（alpha 范围 0..1）
//   rgba 显式分量见各访问器的注释。

namespace echo::win32_app {
namespace theme::color {

// 主纸色与层级
const D2D1_COLOR_F& Paper();       // #f1ead8 主背景纸色
const D2D1_COLOR_F& PaperAlt();    // #ebe2cb 次级面板（feature.hero 背景等）
const D2D1_COLOR_F& PaperEdge();   // #d8cdb1 纸边阴影色

// 墨色文字层级
const D2D1_COLOR_F& Ink();         // #221b12 主文字
const D2D1_COLOR_F& InkSoft();     // #4a3f2f 次级文字
const D2D1_COLOR_F& InkMute();     // #847460 弱文字
const D2D1_COLOR_F& InkFaint();    // #b5a98e 极弱文字 / 占位

// 分割线
const D2D1_COLOR_F& Rule();        // rgba(34,27,18,0.14)
const D2D1_COLOR_F& RuleSoft();    // rgba(34,27,18,0.07)

// 强调（红）
const D2D1_COLOR_F& Accent();      // #a8311b
const D2D1_COLOR_F& AccentDeep();  // #7a2010

// 玻璃面板（List 19 使用）
const D2D1_COLOR_F& GlassTint();   // rgba(248,243,230,0.46)
const D2D1_COLOR_F& GlassTint2();  // rgba(248,243,230,0.62)
const D2D1_COLOR_F& GlassEdge();   // rgba(255,252,243,0.85)

// 纯白
const D2D1_COLOR_F& White();

}  // namespace theme::color

// 视图持有的 Palette，字段名保留旧语义（bg/panel/line/text/...）。
// 此结构最终会被 theme::color::*() 直访替代；现阶段先保兼容。
struct Palette {
  D2D1_COLOR_F bg;
  D2D1_COLOR_F panel;
  D2D1_COLOR_F panelStrong;
  D2D1_COLOR_F line;
  D2D1_COLOR_F text;
  D2D1_COLOR_F muted;
  D2D1_COLOR_F faint;
  D2D1_COLOR_F accent;
  D2D1_COLOR_F accentDark;
  D2D1_COLOR_F white;
};

// 构造 Newsprint 配色 Palette。
//
// 旧字段 -> Newsprint 映射：
//   bg          = Paper          (#f1ead8)
//   panel       = GlassTint      (rgba(248,243,230,0.46))
//   panelStrong = GlassTint2     (rgba(248,243,230,0.62))
//   line        = Rule           (rgba(34,27,18,0.14))
//   text        = Ink            (#221b12)
//   muted       = InkMute        (#847460)
//   faint       = InkFaint       (#b5a98e)
//   accent      = Accent         (#a8311b 红)
//   accentDark  = AccentDeep     (#7a2010 深红)
//   white       = White          (1,1,1,1)
Palette MakeNewsprintPalette();

}  // namespace echo::win32_app
