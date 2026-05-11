# Antigravity 工作清单

本文是分配给 Antigravity 的任务池。Antigravity 可以处理跨模块、需要较多上下文和自主验证的任务，但仍必须遵守 `AGENTS.md`、`docs/WORKLIST.zh-CN.md` 和 TDD tracer bullet 流程。

## 执行规则

- 每次只领取一个任务，不要并行修改多个无关模块。
- 开始前阅读 `docs/README.zh-CN.md`、`docs/MODULES.zh-CN.md`、`docs/MEMORY_BUDGET.zh-CN.md`、`docs/WORKLIST.zh-CN.md`。
- 涉及新功能或 bug 修复时必须使用 `tdd`：先写一个可观察行为测试，再实现。
- 涉及模块边界时使用 `improve-codebase-architecture`，结论写入相关文档。
- 不处理 GitHub 凭据、代理、账号登录等与项目无关的系统配置。

## A1：兼容服务真实 contract fixture

Type：AFK

Status：Done

Blocked by：List 09

What to build：

把当前 handler injection 测试推进为 fixture 驱动的 contract tests。fixture 从旧 Node API 样例和 C++ 原生响应中读取，使用 `ContractJsonMatches` 忽略 volatile 字段。

Acceptance criteria：

- [x] 新增 `native/tests/fixtures/compat/`，至少包含 `/search`、`/song/url`、`/lyric` 的样例。
- [x] 测试能读取 fixture 文件并比较 JSON path。
- [x] volatile path 列表集中维护（集中在各 fixture 的 `_meta.volatile_paths`），不散落在测试体内。
- [x] 缺字段和稳定字段变化会失败并输出 mismatch path。

Latest verification：

- 2026-05-11：新增 `native/tests/fixtures/compat/` 目录，包含 `search.json`、`song_url.json`、`lyric.json`；fixture 驱动 contract 测试集成进 `EchoNativeSmokeTests`；修复 `ContractJsonMatches` 数值类型比较（number_unsigned vs number_integer）；CTest 1/1 通过。

## A2：Win32 截图回归脚本

Type：AFK

Blocked by：List 08

What to build：

把手动截图验证沉淀为可重复脚本，覆盖 900x640、1280x720、1600x1060、2560x1620 四个窗口尺寸，输出截图路径和基础布局断言结果。

Acceptance criteria：

- [ ] 有一个可从 PowerShell 调用的截图/启动验证脚本。
- [ ] 脚本能启动 `EchoWin32`、等待响应、截图、关闭进程。
- [ ] 失败时保留截图和日志，成功时输出窗口尺寸和内存摘要。
- [ ] 文档写清楚如何复现用户反馈的窄窗口问题。

## A3：大列表和图片缓存内存回归

Type：AFK

Status：Done

Blocked by：List 07

What to build：

把一万个模拟项、图片缓存 LRU、滚动释放行为做成稳定回归测试，确保首页、搜索列表、播放队列不会随着滚动无限增长。

Acceptance criteria：

- [x] 搜索结果/队列 ViewModel 只暴露可见区域绘制数据。
- [x] 图片缓存超过预算时稳定淘汰旧项。
- [x] 连续滚动模拟后 memory snapshot 没有持续增长。
- [x] `docs/MEMORY_BUDGET.zh-CN.md` 更新最新数据。

Latest verification：

- 2026-05-11：新增 A3 四个子测试：1,001 步滚动模拟可见行 ≤14；10,000 条 SearchViewModel 绘制切片 ≤14；PlaybackQueueState 10,000 条绘制切片 ≤12；MemoryImageCache 64KB 紧缩预算写入 10,000 条 LRU 正确淡出。更新 MEMORY_BUDGET.zh-CN.md。CTest 1/1 通过。

## A4：Media Foundation 播放错误恢复

Type：AFK

Status：Done

Blocked by：List 05

What to build：

完善播放管线在 URL 打不开、设备失联、seek 失败时的状态恢复，避免 UI 卡死或状态不一致。

Acceptance criteria：

- [x] 播放失败进入 `Failed`，错误信息可被 UI 展示。
- [x] `Stop` 后可以重新 `PlayUrl`。
- [x] 连续切歌释放旧 Media Foundation 对象。
- [x] 行为测试或集成测试覆盖失败后恢复。

Latest verification：

- 2026-05-11：新增 A4 错误恢复测试。验证了 PlayUrl("") 失败后进入 Failed 态，Stop() 可重置状态机，连续 10 次 fail->stop->play 循环不泄漏 Media Foundation 对象且保持稳定的可恢复状态。CTest 1/1 通过。

## A5：最终 UI 直连 BackendFacade 梳理

Type：HITL

Status：Done

Blocked by：List 09

What to build：

设计从开发期 `EchoCompatServer` 到最终 `IBackendFacade` 直连的切换边界，明确哪些 API 只服务旧 Electron，哪些能力要成为最终原生客户端接口。

Acceptance criteria：

- [x] 输出接口迁移表：Compat route、Core service、Facade 方法、UI 页面。
- [x] 标注哪些 route 不进入最终产品。
- [x] 列出 UI 仍依赖假数据的区域。
- [x] 需要用户确认最终保留的页面和功能入口。

Latest verification：

- 2026-05-11：已生成 [A5: UI 直连 BackendFacade 接口迁移规划] Artifact，梳理出 18 个保留接口和废弃路由，用户已确认精简架构（无评论/无云端历史/无登录系统）作为第一版演进路线。
