# Aurora Turntable Night — Design QA Ledger

**Spec:** `docs/superpowers/specs/2026-07-21-aurora-turntable-night-design.md`
**Plan:** `docs/superpowers/plans/2026-07-21-aurora-turntable-night.md`
**Rendered screenshots:** `ui/design-qa-captures/current/aurora-home-*.png`(本次重跑)
**Extra viewport:** `ui/design-qa-captures/icon-rail-1000x800.png`(1000×800,图标栏)
**Viewport:** 1586 × 1024 (primary);矩阵另有 1440×900、1280×720、900×720、light、reduced-motion(见 `manifest.json`)

| Check | Target | Render evidence | Result |
| --- | --- | --- | --- |
| 黑胶空盘空态 | 无曲时舞台为静态黑胶(沟槽+翡翠 label),单行文案,无巨标无描边盒 | 1586×1024 dark:`.aurora-vinyl` 圆盘 + 沟槽 + spindle 可见;"选择一首歌,开始聆听" 26px 级标题;无边框/无大盒 | passed |
| 暖黑令牌 | dark `#0c0b09` 深炭暖黑,accent 不变 | manifest:`appBg:#0c0b09`、`accent:#62d6a2`、`progressTrack:#45403a`;light 仍 `#eef3f0/#18875b/#a9b6af` | passed |
| 光锥 + 尘埃 | 静态右上光锥,锥内细尘,≤60 粒 | 1586 dark:舞台右上至唱盘区域可见细微尘点;`data-particle-cap` 30/60(测试锁定) | passed |
| 常驻传输区 | 无曲时传输键常驻静音态,删除虚线占位 pill | 1586 dark / 1000×800:Dock 中央 loop/prev/play/next 全键可见(muted),无 "选择曲目后显示播放控制" pill;00:00/00:00 进度常驻 | passed |
| 唱针播放头 | 进度播放头为唱针三角(aurora-only :deep 覆写) | live-preview dock:00:34/03:37 填充 + 唱针头;`PlayerProgress.vue` markup 零改动(测试锁定) | passed |
| 器物控件 | 播放键 44px 内凹器物钮 + 静态指示光;音量旋钮同族 | dock 截图:播放键翡翠实心圆带 inset 深度;音量白色旋钮带 accent 环 | passed |
| 图标栏断点 | 900–1099px sidebar 收为 64px 纯图标栏 | icon-rail-1000x800.png:仅 update 圆点/头像/四个导航图标;无 masthead 线、无文字;舞台不再被挤压 | passed |
| 圆角纪律 | 圆只给器物;卡片/rail 8px;Dock 16px | 推荐卡、rail、chips 均 8px;Dock 16px;唱片/播放键/旋钮为圆 | passed |
| 动效语义 | 旋转=播放、唱针=进度、尘埃=氛围、回弹=按压;reduced-motion 全静止可用 | reduced-motion 矩阵:舞台/rail/Dock 结构完整;`startVinylSpin` reduced-motion 惰性(测试锁定) | passed |
| 对比度 | 暖黑上正文/次级 ≥4.5:1 | 令牌校验:text 17.6/7.3/5.2:1,muted 在 surface-2 上 4.8:1,fill/track 5.7:1 | passed |

**final result: passed**

## Capture matrix

| File | Viewport | Mode | Notes |
| --- | --- | --- | --- |
| `design-qa-captures/current/aurora-home-1586x1024-dark.png` | 1586 × 1024 | dark | 主对比:空盘黑胶 + 静音传输区 |
| `design-qa-captures/current/aurora-home-1586x1024-light.png` | 1586 × 1024 | light | 冷白不变,黑胶器物跨模式一致 |
| `design-qa-captures/current/aurora-home-1440x900-dark.png` | 1440 × 900 | dark | rail 可见 |
| `design-qa-captures/current/aurora-home-1280x720-dark.png` | 1280 × 720 | dark | rail 可见(<1280 隐藏边界) |
| `design-qa-captures/current/aurora-home-900-dark.png` | 900 × 720 | dark | sidebar 隐藏,舞台单列 |
| `design-qa-captures/icon-rail-1000x800.png` | 1000 × 800 | dark | 64px 图标栏 |
| `design-qa-captures/current/aurora-home-1586x1024-dark-reduced-motion.png` | 1586 × 1024 | dark + reduced | 无动画依赖 |

## Notes

- Vite-only 截图无 Tauri IPC,推荐/队列内容为空;结构、令牌、黑胶、Dock、断点均完成验证。真实填充态(封面黑胶旋转)需后端环境;组件级已由 `AuroraHome.test.ts`/`AuroraPlayerBar.test.ts` 覆盖。
- `capture-stage-preview.mjs` 的 DOM 注入假设旧结构(往 `.aurora-cover` 塞方块),对黑胶空态不再适用;保留脚本仅用于 rail/dock 演示,后续可重写为驱动真实 view model。
- 已知可选项(未做,不在 spec 范围):rail 空态的大号实心 `重试` CTA 视觉上略抢舞台,可在下一轮降为次级按钮。
- 测试基线:全量 76 文件 / 917 用例通过(2026-07-22);`vue-tsc --noEmit` 干净。
