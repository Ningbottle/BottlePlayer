# ADR-0005：权益、媒体完整性与请求生命周期分别由对应证据判定

- 状态：Accepted（追记已采用的语义与过渡方案；实施进度见当前活动队列）
- 日期：2026-09-14；依据 09-05 Stage 计划、09-12 B1-R4 实现和本次审计。
- 实施状态：部分实现并验证；不等于两个用户故障已关闭。

## 上下文

曾把领取上报成功、普通 token 可用、URL 存在、`/full/` 路径、调用超时返回分别误当成会员生效、会员凭证可用、完整播放或后台资源已经释放。它们是不同层次的事实，需要独立证据和生命周期。

## 决策

### 权益与凭证

领取响应只证明上报结果；激活文案必须基于当前有效音乐权益。确认不可用时保持未知/待确认并有限重查；权威明确无权益不能证明激活。既有权益展示与本次领取结果分别表达。

成功刷新后的普通 token、VIP token、类型和 t1 作为同组凭证保存/使用；普通 token 成功换发但无 VIP 也不能回退到旧 token。凭证有效性与会员权益有效性分别判断。

证据：[LoginService](../../native/core/LoginService.cpp)、[会话组合测试](../../native/tests/session_chain_contract_test.cpp)、[VIP 解析](../../ui/src/features/account/vipResolver.ts)。

已知不符合项（已修复 2026-09-14）：`applyVipSnapshot → tryConfirmOnce` 曾把保护旧状态的 pending 解释为 active；独立反例（本机留存：../../ui/audit/claim-stale-vip.probe.ts） 与 account 正式测试现为 GREEN。见 执行结果（本机留存：../task1-execution-result-2026-09-14.md）。

### 媒体完整性

使用 full/preview/unknown 表达最终 URL 所属响应的证据，每个音质项与自己的 URL 同源；前序拒绝通过 attempts 保存。UI 不把未知说成已完整播放。

时长单位只适用于实际取证的端点×响应层级×字段。当前表只有 v5 envelope 的 `timeLength` 一条依据；不由别名、数值量级或另一个端点外推全部来源。新增依据必须附捕获出处。应用中完整播放仍需媒体时长、自然越过试听边界及近尾播放验证，分类字段不能替代。

证据：[SongUrlService](../../native/core/SongUrlService.cpp)、[取链契约](../../native/tests/songurl_contract_test.cpp)、[gateway](../../ui/src/playback/data/songUrlGateway.ts)。

### 请求生命周期

Stage 3a 保留同步 WinHTTP，watchdog 标记截止，实际 owner 关闭请求，遵守[微软同步句柄并发约束](https://learn.microsoft.com/en-us/windows/win32/winhttp/concurrency-in-winhttp)。接受调用返回后才回收的过渡限制。

统计 FFI 使用后台执行及存活期许可。调用方不再等待不等于工作被终止；通用 FFI 的准入许可与实际存活上限仍不同。3b 异步 session 迁移未实施，需要实测归因、明确回调对象寿命和 shutdown drain 后单独设计。

证据：[HttpClient](../../native/core/HttpClient.cpp)、[Rust 调度](../../ui/src-tauri/src/lib.rs)、[统计命令](../../ui/src-tauri/src/stats.rs)。

## 备选方案

- 上报成功立即设 VIP：拒绝，会制造假激活。
- 只保留最后一次取链结果：拒绝，无法定位鉴权与匿名回退断点。
- 对同步 WinHTTP 跨线程强关请求：拒绝，与并发约束冲突。
- 立即全量异步迁移：暂不采用；迁移风险不能替代故障机根因调查。

## 后果与复查条件

unknown 和部分 BLOCKED 会比“全都成功”更常见，但结论可验证。保持响应分类与真实媒体播放的区分；把离线测试、真实领取、实播、异机稳定性分别验收。上游协议变化或实测资源预算不满足时重新评估此 ADR。

历史：Stage 原件（本机留存：../archive/task1-2026-09-14/vip-stability-remediation-plan-2026-09-05.md）、交接原件（本机留存：../archive/task1-2026-09-14/execution-handoff-2026-09-12.md）。剩余执行安排仅见当前计划（本机留存：../current-work.md）。

## 2026-09-14 机主日志补充

机主日志已证实 v6 `20018/token api error`、v5 MAIN `20018` 后匿名回退为 60 秒试听。保持“鉴权有效性、当前权益、最终媒体类型”三者分离；不得由错误码单独推出缺 VIP，也不得把路由 status=1 推出领取成功。当前最终无 URL 的 20018 分支仍有错误归类，按计划 T1b 验证并修复。详见日志分析（本机留存：../task1-owner-logs-analysis-2026-09-14.md）。
