# BottleMusic：VIP 与稳定性整改 Stage 计划（2026-09-05）

本计划基于《BottleMusic：VIP、卡死与重构审计（2026-09-05）》（`docs/vip-stability-refactor-audit-2026-09-05.md`），并在制定前对审计的关键论断做了逐条代码复核。除特别标注外，审计论断均与当前代码事实一致。

**执行约束（来自用户，优先级最高）**：
- 先修真实用户故障和错误业务语义，再考虑架构整理。
- 根因、症状、诊断缺陷分开陈述，不混为一谈。
- 计划按依赖关系排序；一次修改只针对一个根因，不把多个根因混在一次提交。
- 不为让测试通过而改测试。例外仅在 Stage 5b：现有测试锁定了错误的业务期望，须先把测试改写为正确业务规则（RED），再改实现（GREEN），并在提交信息中显式说明。
- 每个 Stage 给出：问题、根因、修改范围、为什么这样改、RED 验证、GREEN 验证、回归范围、完成标准。

---

## 1. 事实基础（本轮代码复核确认）

### 1.1 会员显示与领取（前端）

| # | 事实 | 证据 |
|---|---|---|
| F1 | 领取通道返回 `status===1` 后立即写 `isVip=true`，再做权威同步 | `ui/src/features/account/userStore.ts:314,337,388` |
| F2 | `syncVipAfterClaimSuccess` 用 `applyVipSnapshot(..., {allowDowngrade:false})`；权威接口明确返回无权益时返回 `pending`，保留 `isVip=true`，显示"权益状态同步中"，且无后续重查闭环 | `userStore.ts:84-103, 223-239` |
| F3 | 顶层 `is_vip===1 \|\| vip_type>0` 直接判真，不检查任何到期时间 | `ui/src/features/account/vipResolver.ts:54` |
| F4 | 无法解析的到期日期返回 0，按 `Number.MAX_SAFE_INTEGER` 最高优先（视同永久） | `vipResolver.ts:29-34, 62` |
| F5 | 测试 `userStore.test.ts:362-380` 锁定"权威无权益仍保留 optimistic VIP"；`vipResolver.test.ts` 未覆盖"顶层 is_vip=1 且时间已过期"用例（修复空间存在，无旧断言冲突） | 测试文件 |
| F6 | 广告上报主循环最多 8 轮 × 30s 间隔 ≈ 210s 异步等待，期间文案单调变化，无取消入口 | `userStore.ts:251-290` |

### 1.2 歌曲链路（Native + 前端）

| # | 事实 | 证据 |
|---|---|---|
| F7 | `BuildSongUrlOutput` 把"URL 非空"映射为 `status=1`，试听片段同为"成功" | `native/core/SongUrlService.cpp:171-174` |
| F8 | 现有契约测试锁定"试听也 status=1"（`is_preview=true` 区分），该语义本身可保留，问题在一致性与验收口径 | `native/tests/basic_contract_tests.cpp:857-858` |
| F9 | v6 路径 `is_preview` 纯靠 URL 是否含 `/full/`（或 `/yp/full/`）；未知 URL 结构无法区分 | `SongUrlService.cpp:540-543` |
| F10 | v6 成功后按请求音质替换 URL，但不重算该音质的 `is_preview`/元数据 | `SongUrlService.cpp:623-641` |
| F11 | 前端 `playbackOrchestrator` 用 `available_qualities` 再选一次 `finalUrl`，而 `isPreview` 仍取 native 顶层标志——URL 与试听标志可错配 | `ui/src/playback/runtime/playbackOrchestrator.ts:152-157, 162` |
| F12 | v5 回退链（tryPreview / 匿名）用 `std::move` 覆盖 `upstream`，最终 FFI 输出只保留最后一次尝试；最初的鉴权拒绝（如 errcode 20018、fail_process=pkg/buy）不进输出 JSON（仅落 ECHO_LOG） | `SongUrlService.cpp:796-826, 836-869, 924-947` |
| F13 | 在线探针（2026-09-05，真实保存会话）：SVIP 有效至当日 16:58，但历史问题曲目取链 `status=1`、`is_preview=true`、`hash_offset.end_ms=60000`；会话中 `vip_token` 为空、`vip_type=0` | `outputs/vip-stability-audit-2026-09-05/live-session-probe-results.json` |
| F14 | v6 在缺 viptoken 时会"成功"返回试听包（`/yp/p_` 片段、`fail_process=12`），当前代码只在 `is_preview && /yp/p_` 时降级 v5 | `SongUrlService.cpp:614-646` |

### 1.3 稳定性（Native / Tauri）

| # | 事实 | 证据 |
|---|---|---|
| F15 | WinHTTP session 以同步模式创建（`WinHttpOpen(..., 0)`），watchdog 线程超时后执行 `WinHttpCloseHandle(request)`，而主线程可能正阻塞在同一 request 的 Send/Receive/Read 中。微软同步句柄并发契约禁止此用法；CAS 只防双关，不防"使用中关闭" | `native/core/HttpClient.cpp:147-152, 57-62, 273-276, 326-345` |
| F16 | `RequestWatchdog::Loop` 单线程串行执行所有到期 action；一个阻塞 action 延迟后续所有 deadline（探针复现：30ms deadline 在另一 action 挂起时 150ms+ 未兑现） | `native/async/RequestWatchdog.cpp:43-72`；`outputs/.../native-probe-results.json` |
| F17 | `SubmitWithDeadline` 非 void 分支：`try { promise->set_value(fn(...)); } catch (...) {}` 把 `fn` 抛出的服务异常连同 `set_value` 异常一并吞掉，promise 永不满足 → 调用方只见 `broken promise`。void 分支无此问题 | `native/include/echo/async/RequestScheduler.h:175-184`；探针 `throwing_task_error="broken promise"` |
| F18 | 6 个统计 Tauri command 为同步 `pub fn`（非 async、无 `#[tauri::command(async)]`），Tauri v2 下在主线程执行；FFI 侧取 `shared_lock` 后经 Storage Actor `future.get()` 等待数据库，且无 deadline | `ui/src-tauri/src/stats.rs:5-81`；`native/core/C_API.cpp:363-433`；`native/include/echo/storage/Database.h:103` |
| F19 | `lib.rs` 的 16 路 semaphore 是准入上限而非存活上限：超时立即归还 permit，`spawn_blocking` 闭包继续运行（测试明确锁定该语义） | `ui/src-tauri/src/lib.rs:64-99, 374-424` |
| F20 | 本机日志两次 Run-Time Check Failure #2（07-31 `ctx`、09-04 `session`），无调用栈与 dump；`C_API.cpp:330-358` 已有 terminate/CRT 钩子（CRT 钩子仅 `_DEBUG`），Release 无 minidump 设施 | 审计 §2.1；`native/core/C_API.cpp` |
| F21 | 未提交改动 18 个 Native 文件（+796/-124）：`AesCbcEncryptHex`、双密钥族 RSA、`LoginService::RefreshSession` 双族（Standard→Concept）刷新、`SongUrlService::Resolve` 透传 `vipType`、登录/歌曲契约测试扩充。方向针对 F13 证实的 `vip_token` 缺口，但未经旧会话恢复/新登录/完整播放验证 | `git status`、`git diff` |
| F22 | HEAD `fd04497` 为 46 文件混合 WIP（+1334/-3200）：VIP/token 协议 + 登录 UI 重写 + 删除桌面歌词/悬浮窗 + 播放改动混合，不能视为"已验证基线" | `git show --stat fd04497` |

### 1.4 未被证实的（任何 Stage 不得声称）

- WinHTTP 违规关闭是否就是某次具体卡死的触发点（F15 是契约违规，因果未证实）。
- RTC#2 栈损坏的写入者与函数（F20 只有变量名）。
- 未提交凭证链路改动（F21）能否恢复完整播放。
- 另一台 Windows 卡死与本机 RTC#2 是否同源。

---

## 2. 症状 / 根因 / 诊断缺陷（三分）

### 症状（用户可观察）

- **S1** VIP 权益有效但歌曲只播放 60 秒试听（09-02 起，09-05 在线复现）。
- **S2** 本机两次 RTC#2 栈损坏。
- **S3** 另一台 Windows 卡死（无 dump，未归因）。
- **S4** VIP 显示不可信：领取后"同步中"不收敛；过期/无效日期可显示为有效 VIP。
- **S5** 领取最长 210s 异步等待且无取消，观感与卡死混淆。

### 根因（代码级，已证实）

- **R1**（→S4）VIP 状态语义错误：领取受理=权益有效（F1/F2）；顶层标志不经时间校验（F3）；非法日期按永久（F4）。
- **R2**（→S1）会话凭证链缺 `vip_token`：保存会话 vip_token 为空时 v6 只回试听包（F13/F14）。**修复材料在未提交改动中（F21），未验证。**
- **R3**（→S1 的不可验收性）"取到 URL"被当作成功/完整（F7/F9）；音质切换后 URL 与试听标志错配（F10/F11）。
- **R4**（→S2/S3 嫌疑）WinHTTP 同步句柄跨线程关闭（F15）。
- **R5**（→S3 嫌疑）统计命令同步阻塞主线程且无 deadline（F18）。
- **R6**（放大 S3）watchdog 单线程串行执行 action（F16）。

### 诊断缺陷（不直接致障，但让根因无法观测/定位）

- **G1** `SubmitWithDeadline` 非 void 吞异常 → broken promise（F17）。
- **G2** v5 回退覆盖原始响应，最初鉴权拒绝不进 FFI 输出（F12）。
- **G3** RTC#2 无 dump/PDB/首异常栈（F20）。
- **G4** 测试锁定错误语义：`userStore.test.ts:362`（权威无权益仍 true）；`lib.rs`（超时即还 permit 被当成唯一正确语义，掩盖 zombie 工作的生命周期问题）（F5/F19）。
- **G5** 设备自测以免费歌曲 `/full/` 字样充当 VIP 完整播放证据（审计 §1.3，低优先）。

根因与诊断缺陷的处置区别：**根因必须修行为；诊断缺陷修的是"下次能看清"**。两者都进计划，但不允许用修 G 来宣称修了 R/S。

---

## 3. 现有重构处置结论

| 处置 | 对象 | 理由 |
|---|---|---|
| **保留** | 签名构造集中化（d3238cc，保留端点级差异）；去除 Media Foundation/重复播放路径（9c8bb47、38946cf、3b4df14）；Storage Actor 与数据所有权（1b70538）；音频资源单一 owner、音量单一写入、活动代理路由 LRU；Rust–C++ 契约测试与 CI `--lib`→`--tests` | 有具体行为收益与测试，且与本次根因正交 |
| **暂停** | 新一轮目录/facade/CSS 大迁移；包装架构文档；无端到端验收的多渠道/多身份兜底扩写 | 不修复任何已证实根因，抢占回归定位带宽 |
| **拆分** | 当前 18 个未提交 Native 文件（F21）：主线 = AES-HEX/双族 RSA/RefreshSession/vipType 透传（对应 R2）；非主线 = 扫码版型变更等，单独评审。HEAD fd04497 不回退，但不得作为"已验证"基线；其协议部分在 Stage 7 单独验收 | 混合范围是本轮回归定位困难的主要来源 |
| **回退** | 无已提交项需要回退 | 05-28 的硬编码本地拒绝已在 fd04497 恢复；未发现已提交的错误协议改动 |

---

## 4. Stage 计划（按依赖排序）

依赖图：`S0 → {S1 → S2 → S3}；S0 → S4；S5a/S5b → S6 → S7；S2/S3/S4/S7 → S8 → S9`。
理由：取证先行（否则后续无法判定成败）；native 诊断保真（S1）先于调度器/http 整改（S2/S3），否则整改期排障信号仍被吞；业务语义（S5/S6）先于凭证链路验收（S7），否则"修没修好"无可信判定口径。

**依赖关系澄清（2026-09-06 审查修订）**：
- S2 排在 S3 之前是**执行顺序安排**（先让计时器可信、再动 HTTP 取消，减少整改期变量），**不是 WinHTTP 合规修复（S3）的技术前提**。S3 的完成标准不依赖 S2 的任何产物；若 S2 遇阻，S3 可先行。（原文"watchdog 分离是 WinHTTP 合规化的前提"表述作废。）
- S7a 的材料拆分（把 18 个未提交文件分成主线/非主线并评审）**可以提前整理**，不被 7b/7c 阻塞；真正依赖 S6 的是 7c 的完整播放验收（需要 `delivery` 字段做判据）。原有"**7c 在线验收通过前不 commit/合入任何 7a 内容**"的约束继续有效。

### Stage 0 — 取证与基线冻结（不改行为）

- **问题**：RTC#2 只有变量名；故障无法映射到构建；修复无法对照验证（G3）。
- **根因**：缺 Release 可用故障取证：PDB 保留、首异常/未处理异常 minidump、构建 hash 记录。
- **修改范围**：`native/CMakeLists.txt`（RelWithDebInfo/Release 生成并归档 PDB）、`native/diagnostics/`（新增未处理异常过滤器 → MiniDumpWriteDump + 线程栈 + 构建 hash 落盘；保持现有 terminate/CRT 钩子并扩展到 Release）、`outputs/vip-stability-audit-2026-09-05/` 探针脚本固化为可重复运行形态（仍不提交账号数据）。
- **为什么**：S2/S3 当前不可证实根因；没有取证，任何"稳定性修复完成"都是空话。
- **RED**：受控越界测试程序在当前构建下不产生 minidump/符号化栈（证明缺失）。
- **GREEN**：同一受控越界产生 minidump + 全线程栈 + 构建 hash；日志含符号化首异常位置。
- **回归范围**：Native CTest 全量（当前 14/14）；确认零行为变更（仅增量设施）。
- **完成标准**：受控故障取证产物齐备；Release 构建 PDB 随构建归档。

### Stage 1 — `SubmitWithDeadline` 异常保真（诊断 G1）

- **问题**：非 void 任务抛服务异常，调用方收到 `broken promise`（F17）。
- **根因**：`RequestScheduler.h:180` 把 `fn(...)` 求值包进 `set_value` 的 try，`catch (...){}` 一并吞掉 `fn` 的异常，promise 永不满足。
- **修改范围**：仅 `native/include/echo/async/RequestScheduler.h` 的 `SubmitWithDeadline` 非 void 分支：`fn` 求值与 `set_value` 分两层 try；`fn` 异常走外层 `set_exception`。
- **为什么**：这是所有后续 native 整改的排障信号保真前提；改动极小、风险极低。
- **RED**：契约测试——非 void 任务抛 `std::runtime_error("marker")`，断言 `future.get()` 抛出该类型且 `what()` 含 `marker`（当前实现必红，得 `std::future_error`）。同时保留 watchdog deadline 仍抛 `job_deadline` 的断言。
- **GREEN**：上述测试转绿；`basic_contract_tests.cpp:1617-1633`（Submit 异常安全）保持绿。
- **回归范围**：`request_scheduler_resilience_test`、`basic_contract_tests`、全量 CTest。
- **完成标准**：服务异常类型/消息保真传递；无新吞异常路径。

### Stage 2 — RequestWatchdog 计时与执行分离（根因 R6）

- **问题**：单个阻塞 action 延迟全部后续 deadline（F16，探针已复现）。
- **根因**：`RequestWatchdog::Loop` 在同一线程既做计时等待又同步执行 action。
- **修改范围**：`native/include/echo/async/RequestWatchdog.h`、`native/async/RequestWatchdog.cpp`、`native/include/echo/async/RequestScheduler.h`（Rule R）、`native/tests/request_scheduler_resilience_test.cpp`。
- **执行方式（2026-09-06 三次审查定稿；作废首版"饱和时 timer 内联回退"——它与"timer 不执行 action"矛盾，且会造成 4 executor + 1 timer 超并发）**：
  - 计时线程：只做"到期 → CAS 认领 → 入队"；**永不执行 action**。
  - 执行设施：**共享有界 FIFO 队列（cap 64）+ 4 个 worker**。"运行中"与"已入队未取"是两种状态；worker 阻塞不占邮箱（首版槽位邮箱模型的缺陷：执行前清空槽位标志导致 Dispatch 把任务塞回阻塞线程的私有邮箱、其余空闲线程饿死——已由正式 RED 测试钉住）。
  - **饱和协议 = 重试再入堆**：队列满时该 entry 以 +10ms 延迟重新入堆并保留认领权（`owned` 跳过二次 CAS）。已接受的 deadline **只延迟、不丢弃、不上 timer**。
  - **整体积压硬上限（2026-09-06 三次审查新增）**：待处理量 = heap + 队列 + 运行中，原子计数 CAS 准入，上限 `kMaxPendingEntries=256`（**"上界等于 armed entry 数"只是数量关系不是容量限制**）。`Arm` 返回 bool；注册被拒的反馈协议：**scheduler → future 立即 `watchdog_overload`，job 不入队**；**HttpClient → `ArmRequestHandleWatchdog` 返回 bool，注册失败时请求立即以 `watchdog_overload` 错误返回并释放当前持有的 request 句柄（无 watchdog 时无关闭 CAS 竞争，直接关闭安全）；GET 包装层将 `watchdog_overload` 排除出 transient 分类，不进入退避重试，错误保真 + 立即返回（首版被归类为 transient 重试，1000ms 预算实测 2527ms——审查探针）；POST 单次尝试天然满足。全部调用点完成适配，"编译兼容不等于行为兼容"（2026-09-06 四次审查修订；HTTP 取消模型的完整迁移仍在 3a）**。
  - **认领即注销（Cancel）**：worker/queue_full 赢得认领 CAS 后立即从堆中移除 entry 并释放预算；timer 的 lazy-drop 路径作为竞态安全网递减——两侧按"堆内是否还存在该 entry"互斥，恰好减一次。若无此注销，已解决任务的 entry 会占用准入预算直到 deadline 到期（生产中 10s），造成假性 `watchdog_overload`（该泄漏由饱和测试的精确账目断言发现：accepted=246≠251）。**Cancel 未命中（外来 flag/重复取消/entry 已被 timer 取走/无 deadline 任务的从未武装 flag）必须恢复堆中全部幸存 entry、只在真移除时减计数——首版在未命中路径丢失整个堆（scheduler 的 `deadlineMs=0` 正常调用链即可触发），由审查探针发现；回归覆盖四场景 + 幸存 deadline 断言（2026-09-06 四次审查修正）**。scheduler 侧仅在 `watchdogArmed` 时调用 Cancel（无 deadline 任务零扫描）。**HTTP 侧：owner 赢得关闭 CAS 的两条清理路径（send/receive 失败、成功收尾）同样立即 Cancel 释放配额——首版只关句柄不注销，每个完成请求泄漏预算直到 deadline 到期，256 个成功请求后必现假性 overload（审查 HTTP 探针复现）；watchdog 赢得 CAS 的路径由执行器完成时释放，不重复注销（2026-09-06 五次审查修正）**。
  - **退出所有权**：全部可变状态收敛进 `shared_ptr<State>`，线程各自捕获其副本、只触碰 State；watchdog 对象仅持线程句柄。析构 = 置 stop + notify + **detach**（join 会正中阻塞 action，即本类要防的故障）；worker 停止前排空队列中已接受的 action，正在运行的 action 自行结束后随 State 释放。构造/析构公开：生产用 `Instance()` 单例，测试/嵌入式可持局部实例走同一退出协议。
  - "action 必须可中断/有界，阻塞性资源回收禁止进入 watchdog action"为硬性契约（与 Stage 3 衔接）。
- **CAS 胜负语义（Rule R："认领决定结果归属"，2026-09-06 定稿）**：
  - 必须二选一并全程一致；本计划选择 **Rule R**：谁赢得 `watchdogClaimed` 的 CAS，谁拥有 promise；**输家不得触碰 promise**。
  - scheduler 侧落实：worker 在 `fn` 完成后、触碰 promise 前**先 CAS**；CAS 失败（deadline 已认领）则业务结果/业务异常一律让位 `job_deadline`。`queue_full` 拒绝路径同样先认领再 set_exception。
  - HttpClient 的 `ArmRequestHandleWatchdog` CAS 协议（双关保护）**不在本 Stage 改动**，其 action（关闭句柄）改由执行器线程承载——与 timer 隔离但仍是跨线程关闭，Stage 3a 移除。
  - **W3 窗口测试（2026-09-06 三次审查修正：首版 W3 在 deadline action 完成 promise 后才释放业务任务——promise 已完成时任何实现都无法覆写，旧实现同样通过，锁不住 Rule R）**：正确窗口 = timer 已 CAS 认领、**deadline action 尚未执行（promise 仍空）**、业务任务完成。**确定性同步（2026-09-06 四次审查修订，取代固定 sleep 猜测）**：①认领用 `DebugClaimedCount` 计数门控（timer CAS 成功处递增，可观测"已认领"，不猜测 deadline 时刻）；②业务完成后的检查 = 屏障保持关闭期间的有限预算就绪轮询——屏障关闭时 deadline action 不可能运行，唯一可能 fulfiller 是业务路径，变体在此窗口必被检出；③放行屏障后断言最终 `job_deadline`。该测试**必须在旧 scheduler 变体上 RED（实测 6 处 FAIL）、Rule R 上 GREEN**；"deadline 明确先赢"场景（race B）只接受 `job_deadline`，不接受"二者之一"。
- **为什么**：所有 deadline 承诺（含 Stage 3 的 HTTP 超时）依赖计时器可信。
- **RED**：可释放屏障探针转正式测试——Arm 一个**可释放阻塞**的 action（atomic 屏障）与一个 30ms 的 deadline，断言后者的 **action 完成信号**（不是 claimed 标志）在 500ms 内出现；屏障释放后必须等待阻塞 action 退出才离开作用域；测试状态一律 shared_ptr 持有（CHECK 失败路径不得留下引用栈局部变量的 action）；"真正永久阻塞"行为放隔离子进程单独验证。**500ms 阈值只证明阻塞隔离性，不宣称 30ms 精度**。
- **GREEN**：隔离性/W3/竞态/饱和/退出测试全绿；阻塞 action 不再影响其他 deadline。
- **回归范围**：`request_scheduler_resilience_test`、`http_client_resilience_test`、全量 CTest。**HTTP 配额回归（2026-09-06 五次审查新增）：①超过 backlog 上限的连续成功请求永不触发 overload 且 pending 归零（配额按完成释放）；②失败清理路径的配额在 deadline 到期前释放（短窗口轮询，deadline=900ms、轮询 300ms）；③饱和下 GET/POST 错误保真 + GET 即时返回（<1s，无退避）+ 句柄归零**。
- **完成标准**：计时与执行解耦（timer 零 action 执行）；并发上限 4 / 队列 cap 64 / 重试 10ms / 待处理硬上限 256 有定义且有测试（含超载反馈与认领即注销的精确账目、Cancel 四场景未命中回归、HTTP 配额三回归）；退出协议经局部实例真实测试（析构及时返回 + **以 entered==worker 数 且 queued==排队数 双门控确认"运行中/已入队"后再析构**——首版 `pending>kQueued` 条件恒真，等于没验证（四次审查修正）+ 队列排空 + shared_ptr 状态无 UAF），不以 CTest 超时充当退出测试；Rule R 由 W3 窗口测试锁定（旧实现 RED、新实现 GREEN 双向验证）；HTTP 调用点完成 Arm 返回值适配且 GET 层不重试准入失败。

### Stage 3 — WinHTTP 取消合规化（根因 R4，P0）

- **问题**：watchdog 线程关闭正被同步调用使用的 request 句柄，违反 WinHTTP 官方并发契约（F15）。
- **根因**：同步 session（`WinHttpOpen(..., 0)`）+ "另一线程 CloseHandle 即取消"模型。
- **契约口径（2026-09-06 审查修订）**：微软的规则是**同步模式下句柄被阻塞调用使用期间不得从其他线程关闭该句柄**（WinHTTP 并发契约），并非禁止一切跨线程关闭。引用：concurrency-in-winhttp、WinHttpSetTimeouts 文档。
- **修改范围**：`native/core/HttpClient.cpp`（移除 `ArmRequestHandleWatchdog` 对 request 句柄的关闭；超时改由 per-op `WinHttpSetOption`（已有）+ 协作取消 + "超时后 owner 线程自行关闭"），`native/async/RequestWatchdog.h`（相应 API 收缩）。**分两小步**：3a 过渡——超时只置 cancel flag，句柄一律由发起线程在 WinHTTP 调用返回后关闭（最坏泄露窗口被 per-op timeout 上界封死）；3b 根治——评估并迁移异步 session（`WINHTTP_FLAG_ASYNC` + 状态回调所有权），3b 可在 3a 稳定后单独排期，不阻塞 Stage 4-7。
- **为什么**：这是卡死嫌疑链上唯一被官方契约确认的实现级违规；CAS 不解决"使用中关闭"。
- **RED**：(i) 结构契约测试：grep 级测试或封装层断言"request 句柄的 `WinHttpCloseHandle` 调用点只存在于 `ExecuteRequest` 调用线程上下文"（当前因 watchdog lambda 存在而红）；(ii) 压力测试：高并发 + 极短超时（1-10ms）× N 千次，统计异常错误形态与句柄计数——当前实现下可观测非确定性异常（此测试在 3a 后必须稳定）。
- **GREEN（2026-09-06 审查修订，验收拆分为两项独立承诺）**：
  - **调用方返回时间**：所有请求在调用方预算内返回错误或结果（per-op timeout + 总预算），压力测试下 P99 返回时间有界——这是对用户可见的承诺。
  - **底层退出与资源回收**：句柄计数 `HttpClientLiveRequestHandleCount` 在请求结束后归零；超时后底层 WinHTTP 调用最迟在 per-op timeout 上界内退出并释放句柄（3a 阶段该上界可能大于调用方 deadline，须如实记录两者差值，不得宣称等价）。
  - (i) 转绿；(ii) 压力测试在 3a 后稳定通过；**"超时语义等价"只能指调用方返回时间等价，底层退出时间等价须在 3b（异步 session）后才可声称**。
- **回归范围**：`http_client_resilience_test`、SongUrl/Login/Playlist 契约测试、全量 CTest；手动冒烟：弱网（限速/丢包）下搜索、取链、封面加载。
- **完成标准**：全库不存在"同步调用期间跨线程 `WinHttpCloseHandle`"；调用方返回时间语义等价；句柄计数无泄露；两项承诺的差值有记录。

### Stage 4 — 统计 FFI 迁出主线程（根因 R5，P0）

- **问题**：6 个统计命令在主线程同步执行 FFI，经 `shared_lock` + Storage Actor `future.get()`，无 deadline（F18）；磁盘慢/锁竞争/关闭竞态时 UI 冻结。
- **根因**：`stats.rs` 命令未 async 化，且 FFI 路径绕过了带 deadline 的 `EchoHandleRequest` 调度链。
- **修改范围**：`ui/src-tauri/src/stats.rs`（6 个命令改 `async fn`，内部 `spawn_blocking` + 专用信号量 + `tokio::time::timeout`），`ui/src-tauri/src/lib.rs`（注册不变；必要时提取共享的 bounded-ffi 辅助）；C++ 侧不在本 Stage 改动（若要加 deadline 属后续 Stage）。
- **存活上限约束（2026-09-06 审查修订，取代"复用现有 timeout 辅助"的隐含方案）**：
  - 现有 `dispatch_bounded_ffl` 语义是"超时立即归还 permit，`spawn_blocking` 闭包继续运行"（F19），那是**准入上限而非存活上限**。若统计命令直接复用它，被超时放弃的 FFI 仍会无限累积——正是本 Stage 要修的 R5 家族问题。
  - 因此统计命令的 permit 必须由 blocking 闭包持有直到闭包真正结束（存活上限 = 并发上限），调用方超时只放弃**等待**、不释放 permit。
  - **排队纳入超时**：`tokio::time::timeout` 包住 acquire+执行全程（排队慢也算超时），不是只包执行段。
  - **关闭边界**：应用退出时在途统计 FFI 的处理策略须写明（与 EchoShutdown 的 bounded shutdown 语义一致：等待上界内 join，超界 detach 由进程退出回收），并有测试覆盖"超时后闭包仍在跑"场景不产生 use-after-free/死锁。
  - 已开始的 `spawn_blocking` 不会随调用方超时停止（Tokio 语义），任何"取消"只是放弃等待，文档与测试都必须以此为准。
- **为什么**：这是可证实的"主线程可被数据库等待占住"路径；修复成本低、收益直接。
- **RED**：集成测试——对 stats FFI 注入人为延迟（测试专用路径或替换 DLL 符号的现有测试机制），并发调用一个快速命令与 `stats_get_summary`，断言快速命令 P95 延迟不因统计调用而超过阈值（当前同步实现下必红）。
- **GREEN**：命令 async 化后测试转绿；超时与排队语义有测试（超时返回错误而非无限等待；超过并发上限排队；**存活任务数不超过并发上限——超时放弃等待后闭包结束时才归还 permit**）。
- **回归范围**：`stats.rs` 两个既有测试、`lib.rs` dispatch 测试、`cargo test --tests`；手动：统计页在播放下刷切不卡顿。
- **完成标准**：统计命令不占用主线程；排队/超时/关闭边界有定义与测试覆盖；存活 FFI 数有上界且被测试锁定。

### Stage 5 — VIP 状态语义修正（根因 R1，P0 业务）

拆三个子步，按 5a → 5b → 5c 顺序。

#### 5a `vipResolver`：判真必须有未过期证据

- **问题**：顶层 `is_vip=1`/`vip_type>0` 不看时间（F3）；非法日期按永久（F4）。
- **根因**：判真与取时间分离，判真分支无时间校验。
- **修改范围**：`ui/src/features/account/vipResolver.ts`：顶层判真要求"存在有效到期时间且未过期"或"字段缺失时按 unknown 而非 true"；非法日期串（非空但解析失败）不再按永久处理，视同缺失。保留 busi_vip svip/music/musicpack 未过期判真与"最晚未过期"选取；保留 tvip 不解锁。
- **unknown 与明确无权益必须区分（2026-09-06 审查修订）**：`unknown`（无法解析/字段缺失）在 UI 语义上不等于"无权益"——不得显示"已过期"或"无权益"的确切断言，只能显示"权益状态未知/同步中"类中性文案；`expired` 才可以明确显示过期。语义三态（active/expired/unknown）各自的前端展示约束写入代码注释并测试锁定。
- **busi_vip 非法日期同样覆盖（2026-09-06 审查修订）**：busi_vip.svip/music/musicpack 子对象内的非法日期串（非空但解析失败）同顶层一样按缺失→unknown 处理，不得按永久判真；RED 需含 busi_vip 子对象非法日期用例。
- **为什么**：显示层把过期 VIP 判真是 S4 的一半根因。
- **RED**：新增用例——`{is_vip:1, vip_end_time: PAST}` → `isVip:false`；`{is_vip:1, vip_end_time:'not-a-date'}` → 不判真且 `vipEndDate` 不落非法串（当前实现红，对应探针 expiredTopLevel/invalidExpiry）。`vipResolver.test.ts` 现有用例均使用 FUTURE 或 busi_vip 路径，无旧断言需推翻。
- **GREEN**：新用例 + 全部既有用例绿。
- **回归范围**：`vipResolver.test.ts`、`userStore.test.ts`、account 相关 6 个 vitest 文件。
- **完成标准**：任何"已过期/不可解析"输入不得产生"有效 VIP"输出；语义表（active/expired/unknown）写入代码注释。

#### 5b `userStore`：领取受理 ≠ 权益有效

- **问题**：领取成功先写 `isVip=true`，权威无权益时挂"同步中"永不收敛（F1/F2）。
- **根因**：缺少"受理 / 待确认 / 有效 / 无效"状态区分；`applyVipSnapshot(allowDowngrade:false)` 把"权威否定"与"查询失败"合并成 pending。
- **修改范围**：`ui/src/features/account/userStore.ts`：领取成功仅置"待确认"（不置 `isVip=true`）；权威同步返回无权益 → 显示未生效并提示稍后查询（不保留 true）；查询失败 → 保留待确认并启动有上限重查（如 3 次 × 10s，可取消，超限转"未确认"而非"有效"）。`checkLoginStatus` 的 `allowDowngrade:true` 语义不变。
- **为什么**：消除"权益状态同步中"悬挂与乐观值永存。
- **RED**：把 `userStore.test.ts:362-380` 改写为正确业务期望——"权威无权益 → 不显示 VIP、消息如实说明未生效"（当前实现红）。**注意：此处是先改测试表达正确规则、再改实现，属业务语义修正而非为绿改测试**；保留 `:334`（非权威失败保留 prior）与 `:538`（登录刷新遇非权威失败保留 prior）语义。
- **GREEN**：改写后的用例 + 其余用例绿。
- **回归范围**：account 6 个 vitest 文件全量；`LoginView`/`AuroraHome` 相关测试。
- **完成标准**：离线探针三场景（expiredTopLevel / invalidExpiry / claimSuccessButAuthoritativeNoVip）按新语义输出；无"权威否定仍 true"路径。

#### 5c 领取交互：分阶段可见 + 可取消

- **问题**：最长 210s 广告循环无取消，观感与卡死混淆（F6/S5）。
- **修改范围**：`userStore.ts`（claim 流程暴露当前阶段：设备检查/广告第 N 轮/兜底通道/权益确认；提供取消入口，取消即停止后续轮次并中止重查）、领取按钮所在 Vue 组件（展示阶段与取消按钮）。**不引入新 UI 体系，只改领取流程相关的最小范围。**
- **RED**：测试——取消后不再发起下一轮广告上报；`loading` 状态随取消复位。
- **GREEN**：测试绿；手动验证取消即时生效。
- **回归范围**：account vitest 全量。
- **完成标准**：任何阶段用户可取消；最长等待有明确上界文案。

### Stage 6 — 歌曲链路语义与诊断（根因 R3 + 诊断 G2，P0 业务）

#### 6a `is_preview` 与实际播放 URL 一致

- **问题**：v6 选音质后不重算试听标志（F10）；前端二次选 URL 与标志错配（F11）。
- **根因**：试听标志绑定在"最初选中的 URL"上，而非"最终输出的 URL"。
- **修改范围**：`SongUrlService.cpp`（选音质替换 URL 时同步重算该 URL 的预览标志与元数据；`available_qualities` 每项携带各自的 `is_preview` 与 `delivery`），`playbackOrchestrator.ts`（选 `finalUrl` 时同步采用该项的标志，而非全局 `result.is_preview`）。`status=1 + is_preview=true` 的试听降级语义保留（F8），不改 status 口径。
- **每音质项 delivery 一致（2026-09-06 审查修订）**：`available_qualities` 的每一项必须携带与该项 URL 同源的 `is_preview`/`delivery`，顶层字段只描述"当前选中项"；防止"换一次音质、错配一次"。RED/夹具需覆盖"切换音质后新选中项的标志与 URL 仍一致"的断言。
- **为什么**：标志错配会让"试听"横幅错误消失/出现，也让 Stage 7 的完整播放验收失真。
- **RED**：把探针场景转契约测试——v6 响应含高音质完整 URL + 低音质片段 URL，请求低音质，断言输出 URL 为片段且 `is_preview=true`（当前红）。
- **GREEN**：契约测试 + `basic_contract_tests.cpp:1849-1868`（现有 v6 选音质契约）全绿。
- **回归范围**：`songurl_contract_test`、`basic_contract_tests`、播放相关 vitest。
- **完成标准**：任意音质选择下 URL 与试听标志同源一致。

#### 6b 试听判定去 `/full/` 单启发式

- **问题**：试听判定只看 URL 是否含 `/full/`（F9）。
- **根因**：未利用上游权利字段。
- **修改范围**：`SongUrlService.cpp`：v6/v5 均结合 `fail_process`、`hash_offset` 区间、URL 形态综合判定；输出新增明确字段（如 `delivery: "full"|"preview"|"unknown"`），`is_preview` 由其派生；未知结构归 `unknown` 而非猜 full。
- **RED**：夹具——无 `/full/` 但无 hash_offset 的未知 URL → `delivery=unknown`；`/yp/p_` + hash_offset ≤65s → `preview`（部分当前红）。
- **GREEN**：夹具全绿且现有契约不破。
- **回归范围**：同 6a。
- **完成标准**：判定逻辑有夹具矩阵覆盖；`unknown` 不在 UI 显示为"完整版"。

#### 6c 保留各次尝试的诊断（G2）

- **问题**：v5 回退覆盖原始响应，最初鉴权拒绝不进 FFI 输出（F12）。
- **根因**：`upstream` 被 `std::move` 覆盖，输出只取最终态。
- **修改范围**：`SongUrlService.cpp`：输出增加 `attempts` 数组（每次尝试：端点 v6/v5-main/v5-preview/v5-anon、HTTP 状态、errcode、fail_process、是否匿名、是否片段、音质），最终字段保持不变。
- **为什么**：Stage 7 在线验收必须能区分"哪一环被拒"；当前只能看日志。
- **RED**：契约测试断言——MAIN 20018 → PREVIEW 成功的链路，输出 `attempts` 含两条且第一条带 20018（当前红，无此字段）。
- **GREEN**：测试绿；现有输出字段零变化。
- **回归范围**：同 6a + 前端 `songUrlGateway` 测试。
- **完成标准**：任何试听/失败结果都能从 FFI 输出直接读出拒绝链。

### Stage 7 — 会话凭证链路定案（根因 R2，P0 业务；S1 核心）

- **问题**：保存会话 vip_token 为空 → v6 只回试听包（F13/F14）。
- **根因**：登录/刷新链路未产出并维护 `vip_token`；`vip_type` 未落库透传。
- **修改范围**：当前 18 个未提交 Native 文件（F21）。
  - **7a 拆分**：主线（`Crypto` AES-HEX/双族 RSA、`LoginService::RefreshSession`、`SongUrlService` vipType 透传、会话落库 `vip_type`）与 非主线（扫码版型等）分离；非主线单独评审，不随主线合入。
  - **7b 契约测试（离线）**：夹具回放——登录成功含 vip_token 的响应 → 落库 → v6 请求携带 viptoken → 返回完整 URL；RefreshSession 双族顺序与失败回退；vip_token 轮换。RED = 当前 HEAD（无此链路）下夹具断言失败；GREEN = 主线改动下通过。
  - **7c 在线人工验收**：三路径（旧保存会话恢复 / 全新登录 / token 轮换后）× 三曲目（一首免费对照 + 两首确认需会员且账号确有权益），核验：取链 `delivery=full`（Stage 6b 字段）、实际播放越过 60s、seek 至近尾、媒体时长与元数据一致。脱敏记录归档 `outputs/`。
- **为什么**：S1 的核心根因链；审计已证实"权益有效 + vip_token 空 + 试听"并存，修复材料在未提交改动中，必须验证后定案，不得直接当已修复。
- **R2 定性（2026-09-06 审查修订）**：`vip_token` 空**与**试听同时出现只是相关性证据，"补 token 必然恢复完整播放"未经因果验证（协议侧可能还有 dfid/签名/设备链等共同条件）。在 7c 对照验收通过前，R2 只能作为**待验证的故障解释**，任何报告/提交说明不得写成"已确认根因"。
- **RED**：当前保存会话（vip_token 空）的离线契约——该状态下输出必须如实标记 preview（结合 6b），且不存在"假装完整"路径；在线基线 = F13 探针结果。
- **GREEN**：7b 夹具全绿 + 7c 三路径三曲目全部完整播放验收通过；或明确记录剩余断点（哪条路径、哪个 errcode）。
- **回归范围**：`login_contract_test`、`youth_vip_contract_test`、`route_contract_test`、`songurl_contract_test`、全量 CTest、account/播放 vitest。
- **完成标准**：在线验收记录归档；S1 判定为"已修复"或"剩余断点清单"。**通过前不 commit 任何 7a 内容。**

### Stage 8 — 防回归夹具与实机 soak

- **问题**：修复无回归网；历史故障（断网恢复、切歌竞态、统计阻塞、关闭）无矩阵覆盖。
- **修改范围**：录制脱敏真实响应夹具（过期 / 鉴权拒绝 / 试听 / 完整流 / 混合音质 / 多阶段拒绝链）进 `native/tests` 与 vitest 夹具；故障机测试矩阵（断网恢复、快速切歌、统计页并发、播放中关闭）；≥2h 真实播放 soak（含 VIP 曲）。
- **RED**：夹具在 HEAD（未含 Stage 5-7 修复）下按新语义应红。
- **GREEN**：修复后全绿；soak 无新增 RTC/卡死/句柄泄露（live handle 计数归零）。
- **回归范围**：全部。
- **完成标准**：夹具入库（脱敏）；soak 报告归档；不得以"测试总数"宣称稳定性完成。

### Stage 9 — 收尾整理（仅限小范围）

- **范围**：仅当 S1-S8 全绿后，做直接服务于上述模块的小范围清理（如 SongUrlService 判定逻辑注释化/小函数提取）。**暂停**新一轮目录/facade/CSS 迁移与架构层扩写。
- **完成标准**：整理不触碰任何 Stage 0-8 锁定的契约测试语义。

---

## 5. 执行纪律（交给下一轮 Agent）

1. 严格按 Stage 顺序与依赖执行；每个 Stage 一次只针对一个根因；RED 先行并保留红的证据（测试输出），再 GREEN。
2. 不改测试迁就实现。唯一例外 Stage 5b：先改测试为正确业务规则再改实现，提交说明中必须引用本节。
3. 每 Stage 完成后：跑该 Stage 回归范围全量，附命令与结果；不写"应该没问题"。
4. 不 commit、不 push，除非用户逐 Stage 显式批准；Stage 7a 在 7c 在线验收通过前禁止合入。
5. 任何 Stage 中发现新事实与本文档冲突：停下，记录证据，更新计划后再继续；不得在报告中把推断写成事实。
6. 验收口径：完整播放 = `delivery=full` + 实际越过试听边界 + seek 近尾 + 时长核对；`status=1`、URL 字样、数据库 `completed` 字段均不能单独充当证据。
