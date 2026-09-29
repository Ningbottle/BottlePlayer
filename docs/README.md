# BottleMusic 文档入口

更新：2026-09-29。**真实 VIP/权益链路仍未闭环**；本地测试与构建通过不等于完整
VIP 或发布验收。

本仓库是公开仓库。含上游签名族、appid/盐/错误码取证、账号日志分析与智能体交接
细节的记录只保留在开发机上，不入库（清单见 `.gitignore` 的
「内部取证与逆向分析记录」段）。下面只列已入库的入口。

| 用途 | 入口 | 使用方式 |
|---|---|---|
| 长期架构决策 | [ADR 索引](adr/README.md) | 不用任务报告代替架构决策 |
| 边界与归属 | [ADR 0004](adr/0004-architecture-boundaries-and-ownership.md) | 三层归属、跨层改动的落点 |
| VIP 与请求生命周期证据口径 | [ADR 0005](adr/0005-vip-and-request-lifecycle-evidence.md) | 鉴权有效性 / 当前权益 / 最终媒体类型三者分离 |
| 端点契约与验证范围 | [ADR 0006](adr/0006-endpoint-contract-and-verification-scope.md) | 一个端点的证据不自动推广到另一个端点 |
| 架构地图 | [Wiki](wiki/README.md)、[CONTEXT](../CONTEXT.md) | 有历史内容，接口路径与状态须核对代码 |
| 稳定性整改计划 | [VIP 稳定性整改 2026-09-05](vip-stability-remediation-plan-2026-09-05.md) | 分阶段验收批次 |
| 架构整改计划 | [0830 计划](../0830-architecture-remediation-plan.md) | 与 README 顶部说明配合阅读 |
| 签名族 A/B 实测结论 | [2026-09-29 结论](signature-family-ab-20260929.md) | 结论与判定边界；取证原件本机留存 |

文档分类纪律：活动队列只保留一份；每次实验保存日期化证据；长期选择写 ADR；
旧计划就地标记替代或归档并保留跳转。未知、离线通过、真实应用通过、故障机通过
分别表述，不合并成一句「已验证」。
