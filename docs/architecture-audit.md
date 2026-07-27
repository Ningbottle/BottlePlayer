# 音乐软件架构审计与渐进演进计划

> **状态**：Phase 3 综合定稿（最终计划）  
> **日期**：2026-07-17  
> **范围**：只读审计；本文件为唯一落盘产物，**不修改任何生产代码**  
> **来源**：三份互补审计计划综合（A 删死代码策略 / B timeout·retry 收敛与客户端复用 / C 正确性优先级）  
> **验证**：结论均已通过逐行读码交叉验证（`HttpClient.cpp` / `C_API.cpp` / `backend.ts` / `backend_api.rs` / `lib.rs` / `audio_proxy.rs` / `CMakeLists.txt` 等）

---

## 0. Phase 3 综合说明

### 0.1 三份计划如何互补

| 来源 | 核心贡献 | 如何进入最终计划 |
|------|----------|------------------|
| **计划 C** | 正确性优先级：**P0-A 句柄泄漏最高**，P0-B DLL 卸载 UAF 次之 | 阶段 1 只做 P0 热修；不为性能/抽象打断正确性 |
| **计划 A** | **删死代码优于加抽象**；自上而下分步删除；禁止三层同改 | 阶段 2–3 纯删除（MF 链路、BackendFacade、CMake 双重编译）；EchoContext 压到阶段 6 收尾 |
| **计划 B** | **timeout/retry 唯一负责人** + **reqwest Client 复用** | 阶段 4 收敛重试；阶段 5 做 OnceLock 客户端与 saveQueue 防抖 |

### 0.2 综合决策原则（执行时不可违背）

1. **正确性先于性能先于结构**：P0 崩溃/泄漏/UB 必须先修；看门狗最小堆、EchoContext 最后做。
2. **删死代码优于加抽象**：能删的平行实现不保留“兼容层”；不为“未来多后端”预留接口。
3. **策略归位而非推倒重写**：重试只留一层负责人（C++ `HttpClient`）；外层 deadline 看门狗保留作兜底，但魔法数字只维护一份。
4. **禁止同时改全部三层**：MF 删除拆 2a→2b→2c；每步独立可编译、可回滚。
5. **每阶段独立验收闸门**：`cargo test` + CTest + vitest（适用层）全绿才进入下一阶段。

### 0.3 本轮 vs 后续

| 轮次 | 动作 | 产物 |
|------|------|------|
| **本轮（阶段 0）** | 只读审计 + 写本报告 | `docs/architecture-audit.md` |
| **后续（阶段 1–6）** | 独立批准后按阶段改代码 | 见第五节；**本文件不授权自动开工** |

---

## 一、当前真实架构（生产调用链）

```
Vue 组件
  → playerStore.ts / playbackOrchestrator.ts        (播放编排，仅实例化 Html5AudioBackend)
  → backend.ts (invoke)                              (14s 超时 + 幂等 GET 重试 [500,2000] + 熔断器)
  → Tauri Command (lib.rs / backend_api.rs)          (deadline_for_path 每路径超时)
  → C ABI EchoHandleRequest (C_API.cpp)              (KindForPath + DeadlineMsForKind, RequestScheduler 4 worker)
  → C++ CompatApi::Handle (CompatApi.cpp + compat_routes/*)
  → 各 *Service.cpp
  → HttpClient (WinHTTP, 连接池复用 + 每请求看门狗线程 + [500,2000] 预算重试)  →  酷狗上游
  → Database (SQLite, 单连接 + 全局 mutex, WAL)

音频流：Vue <audio> → Rust audio_proxy.rs (本地 TCP 代理, Range 透传) → 酷狗 CDN
```

### 1.1 路径分类

| 类别 | 说明 |
|------|------|
| **当前生产路径** | HTML5 Audio 播放 + CompatApi 请求链 + `audio_proxy` |
| **已禁用/死链路** | Media Foundation 播放，**贯穿三层且仍在编译**：`ui/src/api/nativeBackend.ts` → `ui/src-tauri/src/playback.rs`（13 命令）→ `C_API.cpp` 的 `EchoPlayback*`（14 导出）→ `native/playback/*`（CMake `EchoPlayback` L108–119）。仅被 `__tests__/playerBackend.test.ts` 引用；`playerStore.ts` 从不构造 native 后端 |
| **疑似遗留/重复实现** | `native/core/BackendFacade.cpp` 与 CompatApi 平行，仅 `basic_contract_tests.cpp` 使用，且每方法重新 `Open()+Initialize()` 数据库 |
| **文档/代码不一致** | README/CONTEXT 称 EQ 为 10 段 Web Audio，但 native EQ（`EchoPlaybackSetEqBands(gainsDb[5])` + `playback.rs` `gains.len()!=5`）硬编码 5 段——因 native EQ 属死链路而契约对不上 |
| **策略重复** | 超时/退避 `[500,2000]` 与 per-kind deadline 在 **4 层**各自实现：`backend.ts` / `lib.rs deadline_for_path` / `C_API.cpp DeadlineMsForKind` / `HttpClient.cpp` 预算重试 |

### 1.2 已交叉验证的关键锚点

| 现象 | 代码锚点 |
|------|----------|
| 成功路径双重 disarm → 句柄泄漏 | `HttpClient.cpp` L322–327 提前 CAS，L362–369 最终 close 被跳过 |
| 每请求 detach 看门狗 + TODO 最小堆 | `HttpClient.cpp` L231–252 |
| `g_shutdown` 非原子 | `C_API.cpp` L30 / 写 L124 / 读 L236 |
| abandoned worker 后直接 return，Rust 仍 `drop(_lib)` | `C_API.cpp` L134–136；`backend_api.rs` L310 |
| lambda 接收 `token` 未使用 | `C_API.cpp` L251–255 |
| Post ×3 预算重试 | `HttpClient.cpp` L436+ |
| 前端幂等 GET 重试 | `backend.ts` L18–21、L87–111 |
| per-path deadline 重复 | `lib.rs` L37–47 与 `C_API.cpp` L55–65 数值同构 |
| audio_proxy 每连接新建 Client | `audio_proxy.rs` L196、L521–532 |
| C_API.cpp 双重编译 | `CMakeLists.txt` L96（EchoCore）+ L104（EchoCAPI） |
| BackendFacade 在 EchoCore | `CMakeLists.txt` L62 |

---

## 二、问题清单（P0 → P3）

> 每条：级别 / 位置 / 触发 / 后果 / 根因 / 最小修复 / 应加测试 / 架构调整 / **归属阶段**

### P0（必须立即修，正确性）

#### P0-A ｜ HttpClient request 句柄泄漏（每次成功请求必然发生）

| 项 | 内容 |
|----|------|
| 位置 | `native/core/HttpClient.cpp` L322–327（冗余“提前 disarm”）与 L362–369 |
| 触发 | 任何一次 `WinHttpSendRequest`/`ReceiveResponse` 成功的请求 |
| 后果 | request 句柄（每请求新建、不入池）永不 `WinHttpCloseHandle`；高频请求下句柄/内核对象累积，长时运行后 `WinHttpOpenRequest` 失败、请求全线报错 |
| 根因 | L325–327 先把 `watchdogCancelled` CAS 置 true → L364–366 最终 CAS 失败 → `watchdogClaimed=true` → L367 `if (!watchdogClaimed)` 为假 → 跳过关闭；看门狗醒来也见 true 不关。**两边都不关** |
| 最小修复 | **删除 L322–327 的提前 disarm 块**。保留 L362–369 即可正确 disarm+close |
| 应加测试 | `http_client_resilience_test.cpp`：连续 N 次成功请求后 `GetProcessHandleCount` 不显著增长 |
| 架构调整 | 否 |
| **阶段** | **1** |

#### P0-B ｜ DLL 卸载撞在飞被遗弃 worker → 跨 FFI use-after-unload

| 项 | 内容 |
|----|------|
| 位置 | `ui/src-tauri/src/backend_api.rs` `shutdown_c_api` L310 `drop(handle)`（含 `_lib`）；`native/core/C_API.cpp` `EchoShutdown` L134–136 |
| 触发 | 请求命中 deadline，`future.get()` 抛 `job_deadline` 提前返回并释放 Rust 读锁，C++ worker 仍在 DLL 内执行；随后退出 → `EchoShutdown` 见被遗弃 worker 直接 `return`（有意保活 g_db）→ 返回 Rust 后 `drop(_lib)` 卸载 DLL |
| 后果 | 被 detach 的 C++ worker 代码页被卸载 → 崩溃/UB |
| 根因 | DLL 生命周期与 C++ 后台线程生命周期未联动 |
| 最小修复 | `EchoShutdown` 返回“是否有被遗弃 worker”（改返回值或新增导出）；Rust 侧为真时 `std::mem::forget(handle._lib)`（进程退出、泄漏可接受）而非 `drop` |
| 应加测试 | Rust 集成测试：人为挂起请求后调用 shutdown，断言不 panic/crash |
| 架构调整 | 否（跨 C ABI + Rust 两层，接口小改） |
| **阶段** | **1** |

---

### P1（应尽快修，可靠性/正确性语义）

#### P1-C ｜ CancellationToken 从未传到底层；future 超时后 worker 仍在跑

| 项 | 内容 |
|----|------|
| 位置 | `C_API.cpp` L251–255 lambda 接收 `token` 但未使用；`HttpClient` 无 cancellation 形参 |
| 后果 | `SubmitWithDeadline` 到期返回异常后，worker 仍跑到 WinHTTP 自身超时；4 worker 被“已放弃”请求占用，有效并发下降 |
| 最小修复 | `HttpClient::Get/Post` 增可选 `const std::atomic_bool* cancelled`（默认 `nullptr`）；重试/读循环处检查；`C_API.cpp` lambda 透传 `token` |
| 应加测试 | 提交必超时任务，断言取消后 worker 及时退出 |
| 架构调整 | 否（渐进接口扩展） |
| **阶段** | **4**（与 timeout 策略同批；依赖 HttpClient 接口小扩） |

#### P1-D ｜ 每请求 detached 看门狗线程睡满全程

| 项 | 内容 |
|----|------|
| 位置 | `HttpClient.cpp` L238–252；`RequestScheduler.h` L152–160 同理 |
| 后果 | 突发封面/轮询时线程尖峰（注释自估 ~36），创建/调度浪费 |
| 最小修复 | 按 L231–237 自带 TODO：单进程级看门狗 + `(deadline, HINTERNET)` 最小堆 |
| 应加测试 | 并发请求下峰值线程数不超阈值 |
| 架构调整 | 局部（仅 HttpClient / scheduler 内部） |
| **阶段** | **6**（race-critical，禁止提前） |

#### P1-E ｜ `g_shutdown` 非原子 bool 数据竞争

| 项 | 内容 |
|----|------|
| 位置 | `C_API.cpp` L30 `static bool g_shutdown`；写 L124（取锁前）；读 L236（`shared_lock` 下） |
| 后果 | C++ 内存模型下的数据竞争（UB），可见性/撕裂 |
| 最小修复 | `static std::atomic<bool> g_shutdown{false};` + `load`/`store` |
| 应加测试 | 现有 CTest 全绿即可（可选 TSan） |
| 架构调整 | 否 |
| **阶段** | **1**（与 P0-B 同文件，顺手修） |

#### P1-F ｜ Post 自动重试非幂等上报 → 重复计播放

| 项 | 内容 |
|----|------|
| 位置 | `HttpClient.cpp` L436（Post 走 ×3 预算重试）；`/playhistory/upload` 前端不在幂等白名单（`backend.ts` L18–19）却被 C++ 重试 |
| 后果 | 播放历史/播放次数可能被重复上报 |
| 最小修复 | Post 默认不自动重试（或仅对“连接建立失败”重试） |
| 应加测试 | 断言 Post 上游 5xx 时不重发 |
| 架构调整 | 否 |
| **阶段** | **4** |

#### P1-G ｜ 4 层 timeout/retry 叠加 → 最坏 9× 上游放大

| 项 | 内容 |
|----|------|
| 位置 | `backend.ts` L11/L21/L87–111、`lib.rs` L37–47、`C_API.cpp` L44–65、`HttpClient.cpp` L390–419 |
| 后果 | 幂等 GET 最坏 = 前端 3 × C++ 3 = **9 次**上游执行；一次“播放一首歌”最坏 20+ 次上游请求 |
| 最小修复 | **唯一重试负责人 = C++ HttpClient**；前端仅保留熔断 + 单次超时；per-kind deadline 常量集中一份 |
| 应加测试 | 契约测试 + 前端“失败一次即记熔断、不再重试”用例 |
| 架构调整 | 否（策略归位） |
| **阶段** | **4** |

#### P1-H ｜ 音频代理每连接新建 reqwest Client → 无 keep-alive 复用

| 项 | 内容 |
|----|------|
| 位置 | `audio_proxy.rs` L196 每 `handle_client` 调 `build_audio_proxy_client()`；`ai_analysis.rs` 同类问题 |
| 后果 | 每条流/每次拖动进度新建 Client，连接池随即丢弃；拖动进度延迟高 |
| 最小修复 | `OnceLock<Client>`（参考 `backend_api.rs` 已有用法），serve 启动构建一次，共享 `&Client` |
| 应加测试 | 日志/手动验证连续 Range 请求复用同一连接 |
| 架构调整 | 否 |
| **阶段** | **5** |

---

### P2（结构/性能，非崩溃）

| ID | 摘要 | 最小动作 | 阶段 |
|----|------|----------|------|
| **P2-I** | 死 MF 播放链路（三层仍编译） | 纯删除（2a/2b/2c） | **2** |
| **P2-J** | `BackendFacade` 平行 CompatApi，仅测试用 | 删除，测试改走 CompatApi | **3** |
| **P2-K** | EQ 5 段 vs 文档 10 段 | 随 P2-I 删除 native EQ；文档更正 | **2** |
| **P2-L** | CMake 双重编译 `C_API.cpp` | 从 EchoCore 源列表移除 L96 | **3** |
| **P2-M** | SQLite 全局锁抵消 WAL；无 busy_timeout | 先 `PRAGMA busy_timeout=5000`；读写分离另议 | **5**（busy_timeout） |
| **P2-N** | `EchoSetEventCallback` 在 `g_playback` 为空时静默丢弃回调（注册顺序脆弱） | 随 MF 死链路删除消解 | **2** |
| **P2-O** | 日志脱敏未在 sink 强制；漏 `Cookie:`/`Authorization:` 头形式 | sink 统一脱敏 + 增头规则 | **6** |
| **P2-P** | `saveQueue()` 每次 mutation 全量 `JSON.stringify` 写 localStorage | 防抖 ~500ms + `beforeunload` flush | **5** |

---

### P3（提示，非本路线强制）

| ID | 摘要 | 建议 | 阶段 |
|----|------|------|------|
| **P3-Q** | `CompatApi::Handle` 仅按 path 匹配、不校验 HTTP method | 路由表加 method 维度 | 后续独立评估 |
| **P3-R** | `PlayStatsService::RecordPlay` 手工 `SqlEscape`；prepare 失败静默空结果 | 绑定参数 + 显式错误 | 后续独立评估 |
| **P3-S** | 32 头平铺 + 8 个进程级全局 + 连接池单例 | 收敛为显式 `EchoContext` | **6**（可降级为单例 context） |

---

### 2.1 问题 → 阶段速查矩阵

```
阶段 1  P0-A  P0-B  P1-E
阶段 2  P2-I  P2-K  P2-N
阶段 3  P2-J  P2-L
阶段 4  P1-C  P1-F  P1-G
阶段 5  P1-H  P2-M(busy_timeout)  P2-P
阶段 6  P1-D  P2-O  P3-S
```

---

## 三、建议删除或隔离的模块

1. **Media Foundation 播放整链**（前端 `nativeBackend.ts`、Rust `playback.rs`、C ABI `EchoPlayback*`、`native/playback/*`、CMake `EchoPlayback`）——纯删除，无生产引用。
2. **`native/core/BackendFacade.cpp` / `.h`**——删除，测试改走 `CompatApi`。
3. **CMake 中 `core/C_API.cpp` 在 EchoCore 的重复条目（L96）**——移除。

**不做的“隔离”**：不为 MF 保留 feature flag 或 stub 适配层（违反计划 A）。

---

## 四、目标架构

```
Vue → backend.ts (熔断 + 单次超时, 不重试)
    → Tauri Command (薄转发；外层 deadline 看门狗, 常量单一来源)
    → C ABI: Echo*(EchoContext*, ...)         (显式 context, 无散落全局)
    → CompatApi::Handle (唯一请求分发)
    → *Service
    → HttpClient (唯一 timeout/retry 负责人 + 单看门狗线程 + 连接池)  → 上游
    → Database (写连接串行 + 只读连接并发, busy_timeout, WAL)

音频流：<audio> → audio_proxy (复用单例 reqwest Client) → CDN
播放：仅 HTML5 Audio / Web Audio (10 段 EQ)，无 native 播放链路
```

### 4.1 职责归位一览

| 层 | 保留职责 | 剥离职责 |
|----|----------|----------|
| `backend.ts` | 熔断、单次 14s 超时、错误上抛 | 幂等 GET 重试（`RETRY_DELAYS_MS`） |
| Tauri / `lib.rs` | 薄转发 + 外层 deadline 兜底 | 与 C++ 重复的魔法数字副本（改引用单一常量/文档源） |
| C ABI / scheduler | 4 worker 并发边界、per-kind deadline | — |
| `HttpClient` | **唯一**预算重试（GET）、连接池、看门狗 | Post 非幂等自动重试 |
| `audio_proxy` | Range 透传 + **单例** Client | 每连接 `build_audio_proxy_client()` |
| 播放 | HTML5 + Web Audio 10 段 EQ | 整条 MF / native EQ |

---

## 五、渐进演进阶段

> 每步可独立编译 / 测试 / 回滚。**禁止同时改全部三层。**  
> 阶段 0 为本轮唯一落盘；阶段 1–6 须另批批准后执行。

### 阶段 0（本轮）：写审计报告

| 项 | 内容 |
|----|------|
| 产物 | `docs/architecture-audit.md`（本文） |
| 验收 | 文件存在且完整；含当前/目标架构、问题表、阶段路线、Rejected Alternatives |
| **本阶段不做** | 任何 `native/`、`ui/` 生产代码改动 |

---

### 阶段 1：P0 正确性热修（C++ 为主，P0-B 跨 Rust 小接口）

| 项 | 内容 |
|----|------|
| 覆盖 | **P0-A、P0-B、P1-E** |
| 文件 | `native/core/HttpClient.cpp`（删 L322–327）；`native/core/C_API.cpp`（`g_shutdown`→atomic；`EchoShutdown` 返回 abandoned 标志）；对应 C ABI 头；`ui/src-tauri/src/backend_api.rs`（abandoned 时 `forget(_lib)`）；`native/tests/http_client_resilience_test.cpp`（句柄计数回归） |
| 验收 | 现有 CTest 全绿 + 新句柄计数测试；`cargo build` / `cargo test` 通过 |
| **本阶段不做** | 不动看门狗线程模型、不动重试层数、不删任何模块、不改 CancellationToken 透传 |

**实施要点（P0-A）**：只删提前 disarm，保留最终 L362–369 的 CAS+close 与失败路径 L310–318 的 close 逻辑。

**实施要点（P0-B）**：`EchoShutdown` 今日在 `abandoned > 0` 时静默 `return`，Rust 无法得知 → 必须把 abandoned 信息跨 ABI 传出后再决定是否 `forget(_lib)`。

---

### 阶段 2：删除 Media Foundation 死链路（自上而下，纯删除）

| 项 | 内容 |
|----|------|
| 覆盖 | **P2-I、P2-K、P2-N** |
| 顺序 | **2a** 前端：`nativeBackend.ts` + `playerBackend.ts` 去 `'native'` + 相关 vitest<br>**2b** Rust：`playback.rs`、`lib.rs`（mod / invoke_handler）、`backend_api.rs` 中 `playback_*` 字段与符号加载<br>**2c** C++：`C_API.cpp` `EchoPlayback*` / `g_playback` / `EchoSetEventCallback`、`native/playback/*`、CMake `EchoPlayback` 与 mf 链接、相关测试 |
| 附带 | 文档/README/CONTEXT 更正 EQ 为 **仅 10 段 Web Audio** |
| 验收 | 每子步后 vitest / cargo / cmake **分别**可独立构建通过，无悬空符号 |
| **本阶段不做** | 不重构剩余播放编排、不改 EchoContext、不碰 BackendFacade |

---

### 阶段 3：删除 BackendFacade + 修 CMake 双重编译（C++ only）

| 项 | 内容 |
|----|------|
| 覆盖 | **P2-J、P2-L** |
| 文件 | 删 `native/core/BackendFacade.cpp`/`.h`；`CMakeLists.txt` 移除 BackendFacade（L62）与 EchoCore 内 `core/C_API.cpp`（L96）；`basic_contract_tests.cpp` 改走 `CompatApi`（语义等价，不删断言） |
| 验收 | CTest 全绿；链接无重复符号告警 |
| **本阶段不做** | 不动请求分发逻辑、不改超时策略 |

---

### 阶段 4：收敛 timeout/retry + Post 幂等修正 + 取消透传

| 项 | 内容 |
|----|------|
| 覆盖 | **P1-G、P1-F、P1-C** |
| 顺序 | **4a** 前端 `backend.ts` 移除 `RETRY_DELAYS_MS` 重试循环，保留熔断 + 单次超时<br>**4b** C++ `HttpClient::Post` 默认不自动重试；per-kind deadline 常量集中一份（消除 `C_API.cpp` / `lib.rs` 重复魔法数字；外层看门狗语义保留）<br>**4c** `HttpClient::Get/Post` 可选 `cancelled`；`C_API` lambda 透传 scheduler token |
| 验收 | 契约测试全绿 + “Post 5xx 不重发” + 前端“失败即熔断、不重试”用例；弱网手动核验 |
| **本阶段不做** | 不删除外层 deadline 看门狗语义、不动连接池、不做看门狗最小堆 |

---

### 阶段 5：性能小修（三处独立，可拆分 PR）

| 项 | 内容 |
|----|------|
| 覆盖 | **P1-H、P2-M（busy_timeout）、P2-P** |
| 文件 | `audio_proxy.rs` + `ai_analysis.rs`（`OnceLock<Client>`）；`Database.cpp`（`PRAGMA busy_timeout=5000`）；`playerStore.ts`（`saveQueue` 防抖 + unload flush） |
| 验收 | 拖动进度复用连接（日志/手动）；CTest、vitest 全绿 |
| **本阶段不做** | SQLite 读写分离多连接（另议）；不重构队列数据结构 |

---

### 阶段 6（收尾）：看门狗单线程化 + sink 脱敏 + EchoContext

| 项 | 内容 |
|----|------|
| 覆盖 | **P1-D、P2-O、P3-S** |
| 文件 | `HttpClient.cpp` + `RequestScheduler.h`（单看门狗最小堆）；`EchoDiagnostics.cpp` + `Redaction.cpp`（sink 强制脱敏 + Cookie/Authorization 头规则）；`C_API.cpp`（残余全局 → `struct EchoContext` 不透明句柄） |
| 验收 | `http_client_resilience_test` / `request_scheduler_resilience_test` 全绿 + 峰值线程阈值测试 + 脱敏测试；FFI 契约测试全绿 |
| **本阶段不做** | 不为“未来多后端”预留额外抽象 |
| **降级方案** | EchoContext 若改造面过大：先做“单例 context 对象 + `getContext()`”聚拢 8 个全局，不强制全签名改 `Echo*(EchoContext*, ...)` |

---

## 六、Rejected Alternatives（拒绝的方案及理由）

| # | 方案 | 拒绝理由 |
|---|------|----------|
| 1 | 一开始就做 EchoContext 大改造 | 改动面大、易引入 UAF；必须在删完死链路后作为阶段 6 收尾 |
| 2 | 重写线程池/调度器为现代 async | 违反“禁止推倒重写、不引入新框架”；`RequestScheduler` 已是可用固定线程池 |
| 3 | 删除所有 timeout 层、只留一层 | 丢失“内层先失败、外层兜底”的看门狗保护；正确做法是**唯一重试负责人** + deadline 去重 |
| 4 | 把看门狗最小堆列为 P0 | race-critical，风险 > 收益；P0 只做约 6 行的句柄泄漏修复 |
| 5 | 一次性跨三层删除 MF 链路 | 违反“不同时改全部三层”；改为 2a/2b/2c |
| 6 | SQLite 立即读写分离多连接 | 迁移/并发风险高；先 `busy_timeout` 过渡 |
| 7 | 为 MF 死链路加适配层/feature flag | 违反“删死代码优于加抽象”；无生产引用则删除 |
| 8 | 前端与 C++ 同时保留 GET 重试“双保险” | 最坏 9× 放大；只保留 HttpClient 一层重试 |

---

## 七、假设与验收闸门

### 7.1 假设

- 生产播放确定且仅使用 Web Audio / HTML5（`playerStore.ts` 仅实例化 `Html5AudioBackend`）。
- native EQ 从未在生产生效，删除无功能回归。
- CI 三门（**cargo**、**CTest**、**vitest**）为每阶段最低验收闸门（按改动层取子集，全栈改动则三门全开）。
- 进程退出时 `forget(_lib)` 导致的 DLL 映射泄漏可接受（OS 回收）。

### 7.2 每阶段通用验收清单

- [ ] 仅触及本阶段声明的文件与问题 ID  
- [ ] 适用层测试全绿（cargo / CTest / vitest）  
- [ ] 无悬空符号、无新增“临时兼容层”  
- [ ] 文档与代码一致（尤其 EQ 段数、重试职责）  
- [ ] 可独立回滚（单阶段 git revert 不破坏邻阶段未合入的假设）

---

## 八、执行优先级总览（给审批用）

```
P0-A 句柄泄漏     ████████████  阶段 1  — 长时运行必现
P0-B DLL UAF      ██████████    阶段 1  — 退出路径崩溃
P1-E g_shutdown   ████          阶段 1  — 顺手修 UB
P2-I/K/N 删 MF    ████████      阶段 2  — 缩维护面
P2-J/L 删冗余     ██████        阶段 3  — 缩构建面
P1-F/G/C 重试     ████████      阶段 4  — 正确性语义 + 上游放大
P1-H/M/P 性能     ██████        阶段 5  — 可拆 PR
P1-D/O/S 收尾     ████          阶段 6  — 最后做
```

---

## 九、关联文档与索引

| 路径 | 角色 |
|------|------|
| `docs/architecture-audit.md` | **本文件**：最终审计 + 演进计划 |
| `.qoder/specs/音乐软件架构审计与演进_task-983.md` | 任务侧草案/镜像（以本文件为 Phase 3 定稿） |
| `CONTEXT.md` / `README.md` | 产品架构说明（阶段 2 后需同步 EQ 描述） |
| `native/tests/http_client_resilience_test.cpp` | 阶段 1 句柄回归落点 |
| `ui/src/api/backend.ts` | 阶段 4 前端重试剥离落点 |

---

*Phase 3 综合完成。下一动作：审批通过后按阶段 1 开工（P0-A / P0-B / P1-E），禁止跳步进入阶段 6。*
