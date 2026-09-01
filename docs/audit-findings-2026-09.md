# BottleMusic 架构审计事实记录（2026-09-01 复核版）

> **证据基准**：HEAD = `2c36b0d7ca91cd58d1b067899aeb39c5dce1dd2c`（`chore(ui): remove unreferenced protocol experiments`）。
> 本记录所有条目均于 2026-09-01 二次复核，引用为**当前 HEAD 代码原文**（文件 + 行号），不依赖任何文档结论。
> 期间仓库的文档删除/归档（Code-Wiki.md、git-timeline、superpowers plans 等）不影响任何条目——
> 代码层（ui/、ui/src-tauri/、native/）在该期间无任何提交变更。
>
> 状态标记：**Confirmed** = 代码原文可直接复现该事实；**Suspected** = 代码结构支持该推断，但未端到端运行验证。

---

## 第 0 层：测试基线（实测，非文档）

| 层 | 命令 | 结果 |
|---|---|---|
| 前端 | `pnpm test`（ui/，vitest jsdom） | 112 文件 / 1340 测试全过，23.57s |
| Rust | `cargo test`（ui/src-tauri，链接真实 EchoCAPI.dll） | 38 测试全过 |
| C++ | `native/out/bottlemusic-check/*.exe` 14 个契约/弹性测试二进制 | 全部退出码 0 |

---

## 第 1 层：前端 TypeScript（ui/src）

### C1 · Confirmed · `server/` 为死代码
- 事实：`server/`（NeteaseCloudMusicApi 衍生）仍存在于仓库根，但前端零引用。
- 证据：对 `ui/src` 全量检索 `localhost:3000|:3000|server/` → **0 匹配**（2026-09-01 复核）。
- 前端所有后端调用经 `platform/tauri/nativeClient.ts → invokeTauri('native_request')` 走 FFI，无 HTTP Node 服务。
- 影响：构建/维护面冗余；`server/` 的安全面（Express 路由 157 个模块）与实际产品无关却仍在仓库中。

### C2 · Confirmed · EQ 开关决定音频走代理还是直连
- 文件：`ui/src/playback/eq/usePlayerEq.ts` L82-87
  ```ts
  async function preparePlaybackAudioSourceUrl(url: string) {
    if (!deps.getEqEnabled()) {
      return { url, crossOriginSafe: false };   // EQ 关 → 直连 CDN，绕过本地代理
    }
    return prepareAudioSourceUrl(url);           // EQ 开 → 127.0.0.1 音频代理
  }
  ```
- 同文件 `makeBackendEqHooks()`（L281-306）把该函数作为 `prepareSourceUrl` 注入 Html5AudioBackend——EQ 叶子组件实际掌握播放数据面的路由决定权。
- 组合影响：同一条播放链路存在两套网络行为（断点续传/重试/缓存只在代理路径存在）；音频故障的表现会因 EQ 开关而完全不同，加大排障歧义。

### C3 · Confirmed · 播放队列多处就地突变（绕过写入漏斗）
同一 `playerStore.queue` 数组被至少 5 处就地修改：
| 位置 | 代码 |
|---|---|
| `playback/fm/fmSession.ts` L353 | `latest.queue.push(...fresh)`（FM 补歌） |
| `playback/runtime/playbackOrchestrator.ts` L94 | `state.queue.push(normalized)`（switchTrack 插入） |
| `playback/playbackQueue.ts` L126 / L137 | `addToQueue` push / `removeFromQueue` splice |
| `playback/components/QueuePanel.vue` L70-71 | `track.Image = img` 后手写 `localStorage.setItem('player_queue', …)` |
| `playback/runtime/playbackOrchestrator.ts` L425-428 | `fetchMissingCover`：`state.currentTrack.Image = image; state.queue[queueIndex].Image = image` |
- 组合影响：
  1. fmSession 的会话替换检测依赖**数组同一性**（`latest.queue !== queueRef`）这一隐式契约——任何"用新数组替换 queue"的改动都会静默破坏 FM 会话逻辑。
  2. QueuePanel 直接写 `localStorage`，与 `playerPersistence` 的保存漏斗构成第二个持久化 owner，写时序互相不可见。

### C5 · Confirmed · 前端 14s 超时硬编码，缺少跨层守卫测试
- 文件：`ui/src/platform/tauri/nativeClient.ts` L11
  ```ts
  const FRONTEND_TIMEOUT_MS = 14_000;
  ```
- 超时三级防线：前端 14s（字面量）> Rust per-path deadline（build.rs 从 C++ `RequestDeadlines.h` 生成）> C++ 内层调度器。
- Rust 侧有测试 `rust_outer_deadlines_are_at_least_cpp_inner`（lib.rs L305-310，其中含 `kFrontendTimeoutMs >= kDeadlineGenericMs` 断言），但**该常量生成于 build.rs，前端 `14_000` 是独立字面量**——若有人单独改前端或单独改 C++ 头，"前端 ≥ Rust 最大 deadline"这一不变量没有测试守卫。
- 后果形态：前端先超时 → 熔断计一次失败 → 请求其实仍在 C++ 内飞行 → 5 次后熔断打开 30s，误伤整桶请求。

---

## 第 2 层：Tauri / Rust（ui/src-tauri）

### C4 · Confirmed · `native_request` 超时不取消 `spawn_blocking` 任务
- 文件：`ui/src-tauri/src/lib.rs` L76-92
  ```rust
  match tokio::time::timeout(deadline,
      tauri::async_runtime::spawn_blocking(move || { backend_api::handle_request(...) }),
  ).await {
      Ok(join_result) => join_result.unwrap_or_else(...),
      Err(_) => Err("request_deadline".to_string()),  // 任务不被取消，继续在阻塞池运行
  }
  ```
- 组合影响：超时后僵尸任务继续占用阻塞线程池线程，且继续持有 `backend_api` 的 FFI 读守卫；大量超时时（网络黑洞场景）会同时耗尽阻塞池线程、拖慢 `EchoShutdown` 的写守卫排空（C_API.cpp 中 3s 排空窗口），极端情况下退出时触发"遗弃即泄漏"分支。

### C6 · Confirmed · 死命令与死代码残留
- `ui/src-tauri/src/lib.rs` L14-17：
  ```rust
  #[tauri::command]
  fn backend_base_url() -> &'static str {
      "native-ipc" // Returning a dummy value to avoid breaking frontend immediately
  }
  ```
  前端已无任何调用方（全 `ui/src` 检索 `backend_base_url` 为 0 引用，命令仅留在 `invoke_handler` 注册表）。
- `ui/src-tauri/src/backend_api.rs` L7-9：
  ```rust
  // Retained so setup can hand us an AppHandle (log/event paths may use later).
  #[allow(dead_code)]
  static APP_HANDLE: OnceLock<AppHandle> = OnceLock::new();
  ```
  `#[allow(dead_code)]` 显式承认其无读取方。

### C7 · Confirmed · 音频代理路由容量淘汰可击中正在播放的曲目
- 文件：`ui/src-tauri/src/audio_proxy.rs` L38、L79-86
  ```rust
  const MAX_ROUTES: usize = 128;
  ...
  while routes.len() >= MAX_ROUTES {
      if let Some(oldest_id) = routes.iter()
          .min_by_key(|(_, route)| route.created_at)
          .map(|(id, _)| id.clone()) {
          routes.remove(&oldest_id);   // 最老路由先死
      }
  ```
- 组合影响：**正在播放的曲目路由通常正是最老的**（先注册）。触发链：EQ 开启（走代理）+ 长 FM 会话注册 ≥128 首 + 用户对当前曲目 seek 到未缓冲位置 → 路由已淘汰 → 代理返回 404 → 播放中断。
- 现有测试 `route_table_stays_bounded_by_max_routes`（L1136）只验证容量上界；没有"活跃路由不得被容量淘汰"的守卫测试。

---

## 第 3 层：C++ 原生库（native/）

### C8 · Confirmed · Database 单 actor 线程是全系统串行化点
- 文件：`native/include/echo/storage/Database.h` L42-45
  ```
  // All public DB access is serialized on a single storage actor thread (no TLS
  // snapshot isolation). ...
  ```
- 组合影响：以下负载全部排队进同一条 actor 队列——
  - 每个 API 请求的 session/device 读取（请求关键路径）；
  - `stats_record_play` 写入（切歌时触发）；
  - 统计聚合查询（`stats_get_summary` / `stats_get_top` / `stats_get_timeline`，可含全表扫描）。
  统计页打开期间的聚合查询会与切歌的 session 读取竞争同一队列，切歌延迟与统计负载耦合。无性能测试守卫该耦合。
- 备注：actor 串行化本身是正确的线程安全设计（SQLite 未设 FULLMUTEX，靠串行化保证），问题仅在**单队列无优先级**的组合效应。

### S3 · Suspected · C_API.cpp 关闭协议注释与实际行为存在表述偏差
- `native/core/C_API.cpp` `EchoShutdown`：3s 排空后若仍有在飞任务，函数直接返回——此时 DLL 故意**不卸载**（Rust 侧 `mem::forget` 泄漏句柄）。该"宁可泄漏不 use-after-unload"策略是正确的，但注释层面容易被读成"关闭失败"而非"受控遗弃"。属可维护性风险，非功能缺陷。

---

## 第 4 层：跨层组合问题（单看任一文件都无错）

### S1 · Suspected · 退出时最后一条播放统计可能丢失
- 链：`playSessionTracker`（finalize 门槛 60s，finalized 标志防双发）→ `stats_record_play`（Tauri 命令）→ Rust `stats.rs` → C++ Database actor。
- `pageLifecycle` 的 `pagehide → disposePlayerRuntime` 是同步编排，而 `stats_record_play` 是异步跨层调用；窗口关闭竞速下，最后一条满足 60s 门槛的记录可能在 actor 写入完成前进程退出。未见同步刷盘守卫。

### S2 · Suspected · SMTC 状态推送乱序
- `osMediaBridge` 经多个独立 `invoke`（`os_media_set_now_playing` / `os_media_set_playback_status`）推送 SMTC 状态；快速切歌时两次推送在 Rust 侧无序号/代际，系统媒体面板可能短暂显示"新曲目 + 旧播放状态"组合。

### S4 · Suspected · HMR 丢失进行中的播放会话统计
- `mediaRuntime` 通过 `window.__bottlemusic_media_runtime__` 全局槽跨 HMR 复用同一 `<audio>`，但 `playSessionTracker` 的会话计时器是模块级状态；开发环境 HMR 重载该模块时，进行中会话的累计时长归零，该曲统计不满 60s 门槛而丢失。仅影响开发环境数据。

### S5 · Suspected · 熔断窗口与重试回滚的交互无测试
- `nativeClient` 熔断 30s 打开期间，上层（如 `songUrlGateway` 的回滚/降质逻辑）的行为组合未被任何测试覆盖；熔断打开恰是网络异常期，恰是需要验证降质路径的时机。

### 已排查、确认无问题（记录备查）
- **日志回调 DLL 卸载后 use-after-free**：回调目标是 Rust 静态函数（活到进程退出），且遗弃场景下 Rust 选择 `mem::forget` 泄漏 DLL 而非卸载——设计已缓解。
- **裸 `isPlaying`/`isLoading` 写入**：`patchPlayerState` 漏斗强制丢弃并改由 `playbackPhase` 派生。
- **HMR 双 `<audio>`**：全局槽单实例 + `detachForHmr` 幂等标志已守卫。

---

## 第 5 层：测试盲区清单

| # | 盲区 | 说明 |
|---|---|---|
| B1 | 真实媒体引擎全程缺席 | vitest 在 jsdom 假 `HTMLAudioElement` 上运行；`ended`/`error`/`stalled`/seek 真实时序从未被测过 |
| B2 | 活跃路由 × 容量淘汰组合 | 见 C7，只有容量上界测试 |
| B3 | 超时防线前端一侧无守卫 | 见 C5，14s 字面量独立于 build.rs 生成链 |
| B4 | 无跨层端到端播放测试 | 前端→Rust→C++→外部服务→返回 全链没有任何一层集成测试；三层测试各自全绿但互不覆盖接口契约 |
| B5 | 单 actor 延迟无性能测试 | 见 C8，统计聚合 vs 切歌关键路径的排队延迟无基线 |
| B6 | 熔断打开期间的上层降级路径 | 见 S5 |

---

## 最终判断（复核后维持）

项目**基本健康，无核心架构溃烂**：依赖方向（UI→Tauri→C ABI→C++）、单一 audio owner、相位状态机、FFI 关闭协议均在代码中真实成立，三层测试实测全绿。

风险集中在**组合边缘**：最值得担心的排序——
1. B4 端到端播放链无测试（最大的未知数）
2. C7 活跃路由可被容量淘汰（可构造出确定复现路径）
3. C5 超时防线前端一侧无守卫（单点改动即可破坏不变量）
4. C4 超时僵尸任务（网络黑洞下的资源耗尽放大器）
5. C1 死代码（删除成本最低，收益立得）

若进入整改，建议顺序：端到端播放链测试 → 路由淘汰保护活跃路由 → 14s 防线守卫测试 → 删死代码。
