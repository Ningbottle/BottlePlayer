# ADR-0006：按端点维护签名契约与验证范围

- 状态：Accepted（延续已采用的端点级切换与分层验收约束；不表示领取候选已验证，也不表示 51002 已关闭）
- 日期：2026-09-18
- 更新：2026-09-18 交接步 —— 登录所有权缺口与领取契约测试冲突已按本 ADR 实施

## 背景

VIP 与歌单查询的同会话实验支持这两个生产调用显式使用 Concept。领取接口仍被拒；此前领取契约测试已先行要求四路 Concept，实现仍为 Standard。把测试期望当上游证据，会使“测试转绿”与用户实际恢复脱节。普通 `checkLoginStatus` 亦缺少异步响应所有权校验，退出/换号/并发检查后的旧响应会污染账户状态。

## 决策

1. 每个端点的 endpoint、profile、参数、签名、序列化和 headers 是契约整体。对一个端点的证据不自动推广到其他端点；appid/clientver/盐成组选择，端点 override 有独立依据。
2. 生产默认、显式候选实验、已验收配置区分记录。普通 token、vip_token、账号权益、媒体许可不合并成单个成功标志。
3. 领取有副作用，不能复用只读查询的自动正反向实网探测。离线构建对照先行，真实验证使用明确选择的单通道；不捏造上游说明或活动完成事实。
4. 异步账户状态写入必须属于当前操作和会话，包括设备、权益、账户副作用、失败与 finally。到期复查局部保护不代表普通登录入口已满足约束。
5. 离线契约、请求受理、权威权益、完整媒体、实际出声与异机稳定性分别验收。部分成功可以关闭对应子任务，不能关闭总任务。

## 取舍

拒绝全站统一切族、靠测试期望推定协议正确、靠 URL 或查询成功宣称实播恢复。允许维护少量端点差异和未验状态，以保留可归因的改动与明确回退范围。

不在本轮引入全局协议重写或自动选族框架。若将来需要平台身份模型，先用多端点证据另写提案。

## 依据与实施状态

- 签名族实验（本机留存：../signature-family-experiment-2026-09-15.md）、查询验收（本机留存：../validation/vip-playlist-concept-three-node-2026-09-15.md）。
- **领取契约（2026-09-18 S2c）**：生产 day/upgrade/listen/ad 仍 Standard。**day 候选**：Debug UI 单次 `apiGet(..., {profile:'concept'})`；Release 显式候选 `candidate_profile_unavailable` + **零上游 POST**（禁止静默 Standard）。路由离线测试：Debug 默认 Standard POST=1、候选 Concept POST=1、Release 拒绝 POST=0。离线契约 ≠ 领取成功。
- **登录所有权 + 权益代次（2026-09-18）**：`loginOpGeneration` + 登录 VIP 会话/`entitlementGeneration` 捕获校验；迟到响应不覆盖到期复查/领取确认的新权益；无并发时仍允许权威降级。正式测试含原 3 反例、entitlement-order 反例与权威降级保留。
- **实机补充（09-18 领取 / 09-19 复核）**：首次 Concept day status=1 并出现新有效 tvip；第二次 131001。观察到权益 ≠ 证明音乐适用性；tvip 排除规则仍缺一手协议证据。真实领取报告（本机留存：../validation/day-concept-live-20260918.md）。
- **状态表达（09-19）**：claimLedger 分离受理与拒绝；观察权益**随时钟**重算 `active`（查询失败亦生效）；文案为「已观察到权益，音乐适用性待确认」；音乐白名单未改；131001 不作已领取定义。时钟修复（本机留存：../validation/observed-entitlement-clock-fix-20260919.md）。
- **仍未关闭**：tvip 音乐适用性、当前权威权益只读查询、领取后完整播放、51002、F1。见活动队列（本机留存：../current-work.md）。
- [ADR-0005](0005-vip-and-request-lifecycle-evidence.md)：权益、媒体和资源生命周期语义继续有效。
