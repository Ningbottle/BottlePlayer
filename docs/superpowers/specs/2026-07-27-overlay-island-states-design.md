# 浮层灵动岛双态 + 歌词条设置 + 飞行变形 + 频谱环移除 设计 Spec

**日期:** 2026-07-27
**状态:** 用户已批准(2026-07-27 对话)
**范围:** BottleMusic UI(`ui/`)。不动数据层、播放链路、Newsprint 皮肤结构。

## Goal

1. 灵动岛从"卡死单形态胶囊"升级为 **iOS 式折叠↔展开双态浮层**:首次出现在屏幕顶部中央,可自由拖动,折叠态点击展开为横版播放卡。
2. 桌面歌词条增加设置面板(字号/密度/不透明度)+ 实色去雾背景 + 可拖宽。
3. 封面飞行动画由"方形起飞"改为沿路径变形为圆形落地。
4. 移除歌词页唱盘外圈频谱环(用户认定丑)。

## Background / Evidence

- 浮层框架(`overlayWindows.ts`/`playerSync.ts`)与两个浮层页(`IslandView.vue`/`DesktopLyricView.vue`)已于 2026-07-27 落地;窗口创建权限问题(`core:window:allow-get-all-windows` 缺失)已在 `07fb9aed` 修复并实机验证(`[overlay] created overlay-island/overlay-lyric` 日志确认)。
- **拖动失败根因**:拖拽区在浮层页根节点(100vw/vh),胶囊占 `calc(100% - 12px)`,可抓区域仅 6px 边——用户实测"动不了"。
- 歌词/歌曲链接失败事件(同日):经排查为 cargo watch 重建期间 FFI 桥楔死,完全重启后恢复,非代码回归;已归档为已知架构风险(见 docs/architecture-audit.md 的 DLL 生命周期条目)。
- 用户已确认关键决策:折叠态**不显示歌词**(职责归桌面歌词条/歌词页);展开态为**横版 ~480×200**;歌词条设置为**字号+紧凑+透明度+拖宽**。

## Global Constraints

- 所有改动尊重 `prefers-reduced-motion` 与现有测试契约(data-test 钩子)。
- 位置/设置持久化一律 localStorage,键前缀 `overlay_`。
- 浮层窗口不初始化播放器;状态仅经 `playerSync` 通道。
- 不新增运行时依赖(GSAP/Tauri API 现成)。
- Tauri 权限最小化:本设计不需要新增 capability 权限(创建/定位/拖拽权限已齐)。

## Design Decisions

### D1 灵动岛双态(单窗口)

| 状态 | 尺寸 | 内容 | 备注 |
|---|---|---|---|
| 折叠(默认) | 340×88 | 旋转封面盘 + 环形进度 + 歌名/艺人 + 传输三键 | **不显示歌词**;无记忆位置时出现在**屏幕顶部中央**(`anchorPosition('top-center')` 计算坐标,不用 window center) |
| 展开 | 480×200 | 左:圆形黑胶封面(旋转,~140px);右:曲名/艺人/专辑 + 可点 seek 进度条 + 传输三键 + 音量条 | 对齐用户 iOS 参考图比例 |

- **切换**:点击胶囊/卡片空白区(非按钮、非进度条区域)在双态间切换;展开态 Esc 收回折叠。切换时窗口中心位置不动:`setSize(LogicalSize)` + GSAP 内容过渡(0.35s expo.out);reduced-motion 直接切换无动画。
- **拖动**:拖拽区从根节点移到胶囊/卡片主体(`data-tauri-drag-region` 上移到内容容器);按钮/进度条被 Tauri 原生排除,不影响点击与 seek。拖放结束照旧 `settleCurrentOverlay`(24px 磁吸)+ 位置记忆。
- **位置记忆**:双态共享同一 `overlay_island_pos`;切换尺寸时保持窗口几何中心不动,避免跳动。
- **首次位置**:`toggleOverlay('island')` 无记忆位置时,调用现有 `anchorPosition('top-center', win, screen)` 计算 x/y 创建窗口。

### D2 桌面歌词条设置

- 齿轮按钮(hover 显现,窗口右上角)→ 弹出面板(浮层内绝对定位):
  - **字号**:14 / 16 / 18(默认)/ 20 / 24 五档;
  - **密度**:紧凑 / 标准 两档(行距与上下 padding);
  - **不透明度**:50–100% 滑杆,步进 5%,作用于整条背景。
- **背景去雾**:底色从雾面混合改为**实色深色**(`color-mix(in srgb, var(--surface-elevated) 96%, #000 4%)` 为基础,再叠滑杆 alpha),消除"白色雾蒙蒙贴图感"。
- **窗口拖宽**:`resizable: true` + 最小宽 480 / 最大宽 1200;高度固定 96;宽度变更 300ms 防抖后持久化(`overlay_lyric_size`)。
- **持久化**:全部设置存 `overlay_lyric_prefs`(JSON:`{ fontSize, density, opacity }`),启动读取恢复。

### D3 封面飞行变形

`coverFlight.ts` 的幽灵在 Flip.fit 位移期间,`borderRadius` 从 10px 渐变到 50%(gsap tween 与位移同步 0.55s),落地与圆形黑胶一致。reduced-motion 照旧跳过。

### D4 频谱环移除

删除 `ui/src/views/lyric/SpectrumRing.vue` 及 `AuroraLyricStage.vue` 中的挂载与 import;歌词页保留黑胶 + 沟槽 + 封面 wash。无其他引用。

## File Map

| 动作 | 文件 |
|---|---|
| 双态切换 + 拖动区上移 + 首次 top-center | `ui/src/views/overlay/IslandView.vue`、`ui/src/api/overlayWindows.ts`(创建坐标) |
| 展开卡片 UI(复用 `PlayerProgress`/`startVinylSpin`) | `ui/src/views/overlay/IslandView.vue` |
| 设置面板 + 实色底 + 拖宽持久化 | `ui/src/views/overlay/DesktopLyricView.vue`、`ui/src/api/overlayWindows.ts`(`overlay_lyric_size` 读写) |
| 飞行 borderRadius 变形 | `ui/src/api/coverFlight.ts` |
| 频谱环移除 | `ui/src/views/lyric/SpectrumRing.vue`(删)、`ui/src/views/lyric/AuroraLyricStage.vue` |
| 测试同步 | `ui/src/views/__tests__/IslandView.test.ts`、`ui/src/views/__tests__/DesktopLyricView.test.ts`、`ui/src/api/__tests__/overlayWindows.test.ts`、`ui/src/components/player/__tests__/AuroraPlayerBar.test.ts` |

## Verification

1. `pnpm test` 全量绿;`vue-tsc --noEmit` 干净。
2. 新增单测:首次位置 top-center 坐标(anchorPosition 已有测试扩展);飞行 tween 带 borderRadius 50%;歌词条 prefs 读写 round-trip;窗口尺寸持久化防抖。
3. 实机验收:折叠态拖得动、首次顶部中央、点击展开横版卡、Esc 收回;歌词条齿轮三档设置即时生效且重启保留;飞行落地为圆。

## Out of Scope

- iOS 式"收起时缩回 App 图标"动画;多浮层实例;展开态音量以外的 EQ 控制;Newsprint 专属浮层皮肤;歌词条内容区滚动(单行设计)。
