# Dual-Interface Player Redesign — Closeout Design

日期：2026-07-12  
状态：待用户审阅书面 spec  
范围：在现有 redesign 分支上收口视觉差距、完成验收、合并进 `main`  
前置文档：

- 产品设计：`docs/superpowers/specs/2026-07-10-dual-interface-player-redesign-design.md`
- 实施计划：`docs/superpowers/plans/2026-07-11-dual-interface-player-redesign.md`

## 1. 背景与结论

双界面重构的主体实现已在分支 `codex/dual-interface-player-redesign`（worktree：`.worktrees/dual-interface-player-redesign`）落地：独立 Shell、Home、PlayerBar、LyricStage、homeFeed 保活、viewRegistry、皮肤 token、部分次级页原语。

用户感知「外观变化不大」的根因不是「没有代码」，而是：

1. **工作区错位**：日常工作区 `C:\BottleMusic` 的 `main` 只有 design/plan 文档，**不含** UI 重写代码。从 main 启动看不到 redesign。
2. **壳层仍共用**：`App.vue` 向 Shell 注入同一套 `Sidebar` / `Topbar`；差异主要集中在首页舞台与底栏，导航骨架辨识度不足。
3. **近期提交偏比例微调**：队列高度、舞台比例等迭代，而非结构性换壳。
4. **空队列弱化舞台**：QA 捕获中常见 `railRows: 0`，右侧 rail 呈空竖条，信息密度偏低。
5. **计划未完全收口**：Task 9 次级页密度与 Task 10 视觉矩阵/验证报告仍有缺口。

**产品决策不变。** 本文件不重新定义 Aurora/Newsprint 产品语言，只定义如何在 worktree 收口并合并到 main。

**选定路径（用户已确认）：方案 1** — 在 worktree 完成差距收口与验收，再合并 `main`。

## 2. 目标与非目标

### 2.1 目标

1. 在 redesign worktree 达到「不看设置名称即可区分 Aurora 与 Newsprint」。
2. 消除「变化不大」的主要根因（环境错位 + 壳层共用语言 + 空队列/密度）。
3. 完成 Task 10 级自动化回归与最小视觉矩阵。
4. 合并进 `main` 后，默认在 `C:\BottleMusic` 启动即为新界面。
5. 更新 `CONTEXT.md` 反映 redesign 已合入。

### 2.2 非目标

- 不修改 C++、Rust、AudioProxy、EQ DSP、统计协议或登录协议。
- 不引入 Motion/Framer Motion 或其他新动画依赖。
- 不重写播放业务逻辑、队列状态机或 home feed 数据契约。
- 不废弃 2026-07-10 产品 design 或 2026-07-11 plan；仅补差距与收口流程。
- 不为 Sidebar/Topbar 复制两套业务请求逻辑。

## 3. 工作位置与分支规则

| 项 | 约定 |
|---|---|
| 实现目录 | `C:\BottleMusic\.worktrees\dual-interface-player-redesign` |
| 实现分支 | `codex/dual-interface-player-redesign` |
| main 在收口前 | 不改 UI；仅在最终 merge 时接收结果 |
| 启动验证 | 必须在 worktree 内 `pnpm tauri dev` 或 `pnpm dev` |
| 合并方式 | 普通 merge（保留 redesign 历史）；禁止 force-push `main` / `origin` |
| 冲突处理 | 在 worktree 解决后再合 |

## 4. 分阶段计划

### P0 — 环境对齐

- 仅从 worktree 启动应用。
- 确认 `themeStore` 为 Aurora（或可切换到 Newsprint 做对照）。
- 确认首页推荐有真实数据（避免空舞台误判「没变化」）。
- 对照现有 `ui/design-qa-captures/` 确认当前构建与 QA 截图同代。

完成标准：操作者明确「看的是 redesign 构建」，而非 main。

### P1 — 差距审计

对照 2026-07-10 design 与当前实现，输出书面 gap list，分级：

| 级别 | 含义 |
|---|---|
| 高 | 直接导致「两套皮肤看起来差不多」或可用性失败 |
| 中 | 密度/空态/次级页不一致 |
| 低 | 像素级微调、可延后 |

预期高优先级项（实现前可修订，但默认纳入 P2）：

1. Sidebar / Topbar 结构性皮肤语言不足（选中态、导航形态、字重，不仅是颜色）。
2. 空队列 rail 无有效占位/引导。
3. Newsprint 首页/壳层相对 Aurora 辨识度偏弱。
4. 次级页（尤其 Stats / Search）仍有共享「旧卡片语言」残留。

完成标准：gap list 写入 plan 附录或 verification 草稿，每项有「做 / 不做 / 延后」决定。

### P2 — 高影响视觉收口

在**现有架构**内补强，不推翻控制器分层：

```
共享层（不变）：playerStore / homeFeedStore / viewRegistry / lyric / 窗口命令
展示层（补强）：Shell + Home + PlayerBar + LyricStage 已独立
壳层导航（本阶段重点）：Sidebar / Topbar 按 data-skin 结构性分化
```

约束：

- 业务逻辑仍一份；禁止在两个皮肤组件中复制 API、播放队列或登录检查。
- 允许通过 `data-skin` / `data-shell` 驱动的模板分支或薄包装组件区分导航形态。
- Aurora：窄导航、实色/胶囊选中反馈、无衬线。
- Newsprint：目录式编号/字重/缩进，禁止复用 Aurora 胶囊形状作为主选中语言。
- 空队列：显示明确空态文案或推荐占位，禁止长期空白竖条。
- 交互反馈仍走现有 GSAP + `motionProfiles`；动画不得延迟 seek/播放/导航。

完成标准：静态截图下，仅看侧栏+首页即可判断皮肤；首页信息密度明显高于「空舞台+空 rail」。

### P3 — 计划 Task 9/10 收口

- 补齐 Task 9 未完成的次级页原语迁移与密度（Stats → History → Equalizer → Settings；Search/Playlist 按 gap list 高优先级处理）。
- Task 10：
  - `pnpm test`、`vue-tsc --noEmit`、`pnpm build` 全绿。
  - 最小视觉矩阵（见第 5 节）。
  - 首页保活：返回首页不重复三组请求、无先空后填。
  - 写入 verification 报告：`docs/superpowers/reports/2026-07-12-dual-interface-closeout-verification.md`。

完成标准：自动化绿 + 报告中的矩阵项全部勾选或明确 waivers（须用户同意）。

### P4 — 合并 main

1. worktree 工作区干净（无未说明的构建产物提交）。
2. merge `codex/dual-interface-player-redesign` → `main`（或 PR 后 merge）。
3. 更新 `CONTEXT.md`：双界面 redesign 状态、worktree 说明、默认启动路径。
4. 在 `C:\BottleMusic` 验证 dev 即为新 UI。
5. 可选：清理或保留 worktree（保留时更新文档说明路径）。

## 5. 验收标准

### 5.1 自动化

- `cd <worktree>/ui && pnpm test` 全绿
- `pnpm exec vue-tsc --noEmit` 无错误
- `pnpm build` 成功

任何失败先按 systematic-debugging 找根因，禁止放宽断言掩盖回归。

### 5.2 最小视觉矩阵

| 皮肤 | 模式 | 页面 |
|---|---|---|
| Aurora | light / dark | 首页、底栏、普通歌词 |
| Newsprint | light / dark | 首页、底栏、普通歌词 |

必须满足：

- 无顶部装饰横线。
- 进度轨道、fill、thumb 在四套光感组合中可见。
- Aurora 与 Newsprint 在布局/导航/首页舞台上可区分，不只是色差。
- 返回首页不出现全页先空后填；显式刷新才强制重请求。
- 普通歌词「回到当前行」不覆盖正文。
- 全屏歌词仅「最小化」与「退出全屏」。
- 1280px 宽下主控不严重裁切或重叠。

扩展尺寸（时间允许）：1440×900、1920×1080；全屏歌词纳入扩展而非阻塞合并的最低门槛（若 P1 标为高优先级失败项则阻塞）。

### 5.3 合并后

- `main` 含 redesign 实现与本 closeout 文档。
- `CONTEXT.md` 已更新。
- 在 `C:\BottleMusic` 启动可见新界面。

## 6. 风险与缓解

| 风险 | 缓解 |
|---|---|
| 在 main 改 UI 导致双份分叉 | P0–P3 强制只在 worktree 改代码 |
| 壳层皮肤化拖成大重构 | 禁止复制业务；只做结构/样式分支 |
| 空数据导致误判视觉失败 | P0 用真实推荐数据；空态单独验收 |
| 合并冲突（main 另有提交） | merge 前 rebase/merge 同步；冲突在 worktree 解决 |
| 设计 QA 截图未跟踪 | 不强制提交大图；verification 报告引用路径即可 |

## 7. 与现有文档的关系

- **2026-07-10 design**：产品与架构真相源；本文件不覆盖其第 2–7 节产品决策。
- **2026-07-11 plan**：Task 1–8 视为大体已实现；本收口驱动 Task 9–10 完成与 P2 高影响补强。
- **实施计划下一步**：本 spec 用户书面批准后，编写 closeout 实施 plan（`docs/superpowers/plans/2026-07-12-dual-interface-closeout.md`），再按 task 执行。

## 8. 成功定义（一句话）

在 redesign worktree 完成可验收的双界面辨识度与回归后，将分支干净合并进 `main`，使默认仓库启动即呈现 Aurora/Newsprint 独立界面，且播放后端边界未被触碰。
