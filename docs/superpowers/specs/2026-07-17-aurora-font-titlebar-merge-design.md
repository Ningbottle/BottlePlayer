# Aurora 字体与顶部栏合并设计规格

> 状态：已批准
> Revision：r1（2026-07-17）
> 实施分支：`main`（主工作树 `C:/BottleMusic` 直接实施）
> 实施方式：TDD、子代理或内联执行
> 前置：已完成安全加固与深度重构合并（main @ 443f00e3）

## 1. 文档地位

本规格定义两项 UI 调整：Aurora 皮肤字体修复、顶部栏（标题栏+顶栏）在非播放页面合并为大页面头。独立于安全加固规格，不触及已落地的安全/C++/Rust 改动。

## 2. 已确认问题

### 2.1 Aurora 字体在 Windows 上丑陋

Aurora 的 `--font-serif`/`--font-sans` 设为 `"SF Pro Display"`/`"SF Pro Text"`（Apple 专属字体）。在 Windows 上 SF Pro 不存在，回退到 `-apple-system` → `"PingFang SC"`（macOS）→ `"Microsoft YaHei UI"`（Windows 默认中文黑体）→ `system-ui`/`sans-serif`。最终渲染为微软雅黑，用户评价为"黑漆马虎的丑黑体"。

Newsprint 用 `"Noto Serif SC"`/`"EB Garamond"`（衬线），Windows 回退到 Times New Roman/Georgia，可接受。

`index.html` 只 web 加载了 `ZCOOL XiaoWei`，Inter/Noto Sans SC/Noto Serif SC 均未 web 加载。"最初的字体"也是 SF Pro（深度重构前后一致），无法直接恢复，需加载 web 字体。

### 2.2 顶部栏与内容割裂、非播放页过于紧凑

Aurora/Newsprint 的 `.titlebar` 为 32px 紧凑条（字标 + 窗口控件），`.shell-topbar`（后退/前进、搜索、登录、设置）在主区上方独立一行。虽已"无分割线"，但仍是两条独立窄行，非播放页面（home/settings/stats 等）顶部缺乏整体感与存在感。

## 3. 产品目标

1. Aurora 在 Windows（及所有平台）渲染为干净现代的无衬线字体，不回退到系统默认黑体。
2. 非播放页面顶部为一个大页面头（字标 + 顶栏控件一体），窗口控件浮于右上角，更有整体感与存在感。
3. 播放页（/lyric 非全屏）与歌词全屏行为不变。
4. 两皮肤（Aurora + Newsprint）顶部栏合并一致。

## 4. 非目标

- 不改 Newsprint 字体（用户只要求 Aurora）。
- 不改播放页/歌词全屏的标题栏行为。
- 不重做整体布局，只合并首行标题栏+顶栏。
- 不改 C++/Rust/安全相关代码。

## 5. 架构方案

### 5.1 Aurora 字体

**`ui/index.html`**：Google Fonts `<link>` 增加 `Inter` 与 `Noto Sans SC`，保留 `ZCOOL XiaoWei`。用 `display=swap` 异步加载，首屏先用回退字体、加载完切换，不阻塞渲染。

新链接形如：
```html
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&family=Noto+Sans+SC:wght@400;500;700&family=ZCOOL+XiaoWei&display=swap" rel="stylesheet" />
```

**`ui/src/style.css`** Aurora 块（`:root[data-skin="aurora"]`，约第 80-83 行）：`--font-serif` 与 `--font-sans` 都改为：
```css
--font-serif: "Inter", "Noto Sans SC", system-ui, -apple-system, sans-serif;
--font-sans:  "Inter", "Noto Sans SC", system-ui, -apple-system, sans-serif;
```
（Aurora 不使用衬线；两个变量都设为同一 Inter 栈，保证所有 `var(--font-serif)` 用法在 Aurora 下也渲染为 Inter 无衬线。）

Aurora dark 块无需改字体（继承）。

### 5.2 顶部栏合并为大页面头

**状态推导**：`App.vue` 经 vue-router 推导 `isPlaybackView`（当前路由 `path` 以 `/lyric` 开头即为 true），作为 prop 传给 `AuroraShell`/`NewsprintShell`。

**Shell 模板**：在 `.app` 上根据 `!isPlaybackView && !lyricFullscreen` 设置 `data-header="merged"`，否则 `data-header="compact"`。

**CSS（`aurora.css` + `newsprint.css`）**：

- `data-header="merged"` 时：
  - grid 首行由 32px 改为约 60px（`grid-template-rows: 60px auto minmax(0,1fr) 104px`）。
  - `.titlebar` 与 `.shell-topbar` 视觉合并：同一背景、无分割线、字标与顶栏控件同处首行；字标字号加大（如 13px → 16-18px）。
  - `.titlebar-controls`（窗口控件）改为 `position: absolute; top: 0; right: 0`，浮于右上角，仍可拖拽/双击最大化区域避开控件。
  - 保留 `data-tauri-drag-region` 在合并头上（控件区域 `@mousedown.stop`）。
- `data-header="compact"` 时（播放页 /lyric 非全屏）：保持现状（32px 标题栏 + 独立顶栏）。
- `lyric-fullscreen` 时：隐藏（现状不变）。

**范围**：`aurora.css` 与 `newsprint.css` 都实现 `data-header="merged"` 规则；`AuroraShell.vue`/`NewsprintShell.vue` 都接收 `isPlaybackView` prop 并设置属性。两皮肤一致。

### 5.3 拖拽与窗口控件交互

合并头仍需支持 `data-tauri-drag-region`（拖动窗口）与双击最大化。窗口控件区域 `@mousedown.stop` 阻止拖拽。控件绝对定位后，其下方的合并头区域仍可拖拽。需测试拖拽与控件点击不冲突。

## 6. 保护项

- Newsprint 字体栈不动。
- 播放页（/lyric 非全屏）与全屏的标题栏行为不动。
- 现有 `Shells.test.ts`、`FullscreenWindowControls.test.ts` 不回退；如断言锁定了 32px 高度或独立标题栏结构，先写新合同测试再改写冲突断言并在提交说明指出。
- 拖拽/双击最大化行为不回退。

## 7. 测试策略

1. **字体测试**：断言 `index.html` 含 `Inter` 与 `Noto+Sans+SC` 链接；断言 Aurora `--font-sans` 不含 `SF Pro`、含 `Inter`。可加到 `releaseSecurity.test.ts` 或新 `appearanceStore`/字体测试。
2. **顶部栏测试**：组件测试断言--
   - 非播放页：`.app` 带 `data-header="merged"`；窗口控件在合并头内且为绝对定位（或存在）。
   - 播放页（/lyric）：`.app` 带 `data-header="compact"`。
   - 全屏：标题栏隐藏。
3. **回归**：`Shells.test.ts`、`FullscreenWindowControls.test.ts`、全量 Vitest、`vue-tsc`、`vite build` 绿。
4. **真机/浏览器 QA**：dev server 看 Aurora 字体（不再雅黑）、两皮肤非播放页大页面头布局、播放页与全屏不变、拖拽与控件可用。

## 8. 实施顺序

1. 字体：index.html + style.css Aurora 块 + 字体测试。
2. 顶部栏：App.vue `isPlaybackView` → shell prop → `data-header` 属性 → aurora.css/newsprint.css merged 规则 → 顶部栏测试。
3. 全量验证 + dev server QA。

每阶段独立提交，TDD（先写失败测试再实现）。
