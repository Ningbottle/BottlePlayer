# Architecture Decision Records

ADR 记录长期决策、依据与取舍；任务进度、测试计数与机器验收记录不作为永久架构事实。

| 编号 | 决策 | 状态 |
|---|---|---|
| [0001](0001-ffi-boundary-c-abi.md) | FFI 使用 C ABI | Accepted，既有记录 |
| [0002](0002-storage-actor-single-thread.md) | Storage Actor 管理 SQLite | Accepted，既有记录 |
| [0003](0003-shared-audio-hmr-lifecycle.md) | Shared Audio/HMR 生命周期 | 既有记录，见原文 |
| [0004](0004-architecture-boundaries-and-ownership.md) | 技术边界与资源所有权 | Accepted，8 月决策整理 |
| [0005](0005-vip-and-request-lifecycle-evidence.md) | 权益、媒体与请求生命周期证据 | Accepted，实施状态见活动队列 |
| [0006](0006-endpoint-contract-and-verification-scope.md) | 按端点维护签名契约与验证范围 | Accepted；登录权益代次+day Concept 候选离线契约已落地；51002/实测未关 |

当前任务入口：活动队列（本机留存：docs/current-work.md）；历史证据：TASK1 审计
（本机留存：docs/task1-audit-2026-09-14.md）；全局入口：[文档索引](../README.md)。
