# ADR-0004：保留技术边界与资源所有权，退役一次性架构路线图

- 状态：Accepted（追记已采用的方向，不表示旧计划全部任务已完成）
- 决策来源：2026-08-30 架构计划中的 ADR-001–008；2026-09-14 按代码边界复核与整理。
- 替代对象：8 月执行计划作为当前施工入口的用途。既有 ADR-0001–0003 继续有效。

## 上下文

8 月计划混合持久架构原则、当时目录布局、测试计数和一次性迁移顺序。当前已有 `features/`、`playback/`、`platform/`、`shared/` 分层以及媒体生命周期代码；再次从旧 clean tree 前提执行会重复施工。9 月的原始故障则需要跨层与异机证据，不适合用文件移动替代修复。

## 决策

1. 保留 Vue → Tauri/Rust → C ABI → C++。Rust 承担桌面、FFI adapter 与本地音频代理；C++ 承担音乐协议、签名、会话、业务与存储。WinHTTP 与 Rust HTTP 服务不同职责，不以“统一技术栈”为由合并。
2. 播放资源按生命周期分属 owner：应用投影在 playerStore，命令协调在 Coordinator/Orchestrator，媒体实例与监听器在 MediaRuntime/Backend，音效在 EQ，偏好持久化在 playerPersistence。修改时验证资源只安装/释放一次，不以全部塞进单 Store 为目标。
3. Feature gateway 负责 typed use-case 和跨层字段转换。KuGou profile、签名、取链回退仍归 Native；UI 不复制协议栈。跨层 snake_case/camelCase 需真实形状契约保护。
4. 保留 `server/` 参考 submodule。其 hash 属于某次构建清单，不作为 ADR 的恒定值。
5. 样式按职责维护；对目录和大文件的拆分必须有生命周期、依赖、变更范围或性能依据。无证据不搬迁 Native Core，不重写播放状态机或动画体系。
6. 确定性逻辑缺陷用可辨别的反例验证；性能归因依靠相同设备与条件的 trace/指标。旧计划的具体绘制热点必须先确认当前代码是否仍存在。

当前源码入口：[媒体运行时](../../ui/src/playback/runtime/mediaRuntime.ts)、[播放状态](../../ui/src/playback/playerStore.ts)、[Native Core](../../native/core/README.md)、[Storage Actor](0002-storage-actor-single-thread.md)、[FFI 边界](0001-ffi-boundary-c-abi.md)。

## 备选与取舍

- 全量迁 Rust：大量协议与构建回归面，尚无证据说明可以解决原故障；不采用。
- 纯目录/文件长度重构：增加 diff，不能直接证明启动、权益或播放正确；不作为本轮目标。
- 保留现有边界并针对根因小修：接受三套工具链和跨层测试成本，换取可审阅、可回归的改动范围；采用。

## 后果与验证

必须维护字段契约、二进制身份和跨层诊断。仍可能有 owner 泄漏、同步初始化与后台任务存活风险，本 ADR 不替这些问题背书。是否调整边界，应由新的根因证据驱动并另记 ADR。

旧 ADR-009“Phase A 完成、工作树 clean”是一次性快照，不升格为永久决策。旧测试数、旧 commit 与 submodule hash 同样留在历史。

## 追溯

8 月计划原件（本机留存：../archive/task1-2026-09-14/0830-architecture-remediation-plan.md）、当前审计（本机留存：../task1-audit-2026-09-14.md）、当前任务入口（本机留存：../task1-recovery-plan-2026-09-14.md）。
