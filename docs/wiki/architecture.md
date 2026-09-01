# 架构总览

> 基于 [evidence-report.md](./evidence-report.md) 的事实重建。
> 本文档只描述**当前实现**;未来提案见 [maintenance.md](./maintenance.md)。

## 三层架构

BottleMusic 采用三层架构,通过 Tauri IPC 和 C ABI 串联:

```mermaid
graph TD
    A[Vue 3 前端<br/>ui/src/]
    B[Rust FFI 外壳<br/>ui/src-tauri/src/]
    C[C++ 核心<br/>native/ → EchoCAPI.dll]
    D[KuGou CDN/API]
    E[SQLite 本地存储]
    F[Windows DPAPI]
    G[DeepSeek API]

    A -- "Tauri invoke<br/>(native_request, stats_*, ai_analyze, audio_proxy_url, os_media_*)" --> B
    B -- "extern C FFI<br/>(EchoInitializeWithPathsV2,<br/>EchoHandleRequest, EchoStats*)" --> C
    C -- "WinHTTP" --> D
    C -- "WAL" --> E
    B -- "loopback 127.0.0.1" --> A
    B -- "reqwest" --> G
    C -- "DPAPI" --> F
```

### 各层职责

| 层 | 路径 | 职责 | 不负责 |
|---|---|---|---|
| Vue 3 前端 | `ui/src/` | UI、播放控制、EQ graph、状态管理、CircuitBreaker | 直接 HTTP、直接 SQLite |
| Rust FFI | `ui/src-tauri/src/` | Tauri 命令、DLL 加载、audio_proxy loopback、DeepSeek 调用、OS Media Session | 业务逻辑(在 C++ 或前端) |
| C++ 核心 | `native/` → `EchoCAPI.dll` | KuGou API 路由、WinHTTP、加密签名、SQLite stats、RequestScheduler | UI、Tauri IPC |

### 旁路:server/ 子模块

`server/` 是 git submodule,指向 `MakcRe/KuGouMusicApi`(MIT,作者 Lines)。**不进入生产链路**(见 [evidence-report.md § 1.3](./evidence-report.md#13-server-是否进入生产链路已确认不进入))。

- 用途:KuGou API 路由的 Node.js 参考实现,C++ CompatApi 翻译时对照
- 运行时:无(生产不启动 Node.js 进程)
- 策略提案:见 [testing-and-release.md](./testing-and-release.md) 中的 `server-strategy-rfc.md` 链接

## FFI 边界

### C ABI 导出(`EchoCAPI.dll`)

由 `native/core/C_API.cpp` 实现,通过 `libloading` 在 `ui/src-tauri/src/backend_api.rs` 加载:

| 符号 | 用途 | 调用方 |
|---|---|---|
| `EchoInitializeWithPathsV2` | 初始化 C++ 后端,传入 app_data_dir | `backend_api::init_with_paths` |
| `EchoHandleRequest` | 主请求入口 | `backend_api::handle_request` |
| `EchoStatsRecordPlay` | 记录播放 | `stats::stats_record_play` |
| `EchoStatsGetSummary` / `Top` / `Timeline` / `Recent` / `Recommendations` | 查询统计 | `stats::stats_get_*` |
| `EchoShutdown` | 有界关闭(Rust 写锁 5s + C++ scheduler 3s + 生命周期锁 3s) | `backend_api::shutdown_c_api` |
| `EchoFreeString` | 释放 C++ 分配的字符串 | `backend_api::CApiHandle::free_str` |

### 全局状态(`C_API.cpp`)

```cpp
struct EchoContext {
  std::shared_ptr<echo::core::CompatApi> api;
  echo::async::RequestScheduler scheduler{4};  // 4-worker 线程池
  // stats 和 storage 由 Database/PlayStatsService 持有
};
static EchoContext& Ctx();  // Meyers singleton,内部 shared_mutex 保护
```

**注意**:旧 `CONTEXT.md` 描述的 `g_playback` 已不存在(MFS 播放栈移除后清理)。

### Rust 端 `CApiHandle`(`backend_api.rs`)

- 用 `RwLock<Option<CApiHandle>>` 保护
- 读锁:并发 C API 调用(允许多读者)
- 写锁:shutdown 时独占,等所有 in-flight 调用完成才 unload DLL
- `_lib: Library` 字段持有 DLL 句柄,`CApiHandle` drop 时自动 unload

## 启动流程

```mermaid
sequenceDiagram
    participant App as Tauri App
    participant Lib as lib.rs
    participant BA as backend_api.rs
    participant AP as audio_proxy.rs
    participant OS as os_media_session.rs
    participant DLL as EchoCAPI.dll
    participant DB as SQLite

    App->>Lib: run()
    Lib->>Lib: 注册 plugins (opener/updater/process/global-shortcut)
    Lib->>OS: set_app_handle(handle)
    Lib->>OS: install_os_integrations (desktop-shell only)
    Lib->>AP: bind_listener() — 绑定 127.0.0.1 随机端口
    AP-->>Lib: Ok((listener, port))
    Lib->>Lib: manage AudioProxyState{port}
    Lib->>Lib: spawn audio_proxy::serve(listener, state)

    Lib->>Lib: 查找 EchoCAPI.dll
    Note over Lib: 1. resource_dir (生产)<br/>2. exe_dir (开发)<br/>3. native/out/{preset} (fallback)

    loop 尝试每个路径
        Lib->>BA: init_with_paths(path, app_data_dir)
        BA->>DLL: Library::new(dll_path)
        BA->>DLL: EchoInitializeWithPathsV2(app_data_dir)
        DLL->>DB: 打开/创建 bottlemusic.db (WAL)
        BA->>DLL: set_log_callback
        BA-->>Lib: Ok(()) or Err
    end

    Lib->>Lib: 注册 invoke_handler (19 个命令)
    Lib->>App: generate_context!().run()
    App->>App: 窗口创建,前端加载

    Note over App,DLL: 窗口关闭时
    App->>Lib: WindowEvent::CloseRequested
    Lib->>BA: shutdown_c_api()
    BA->>BA: get_handle().try_write() — 5s 内获取写锁
    BA->>DLL: EchoShutdown() — scheduler 3s + 生命周期锁 3s
    BA->>BA: 非零返回码 → forget(handle) 保留 DLL mapping
```

### 关键启动不变量

1. **audio_proxy 必须先于 DLL 加载**:audio_proxy 是独立的 Tokio task,不依赖 DLL;DLL 加载失败不影响 audio_proxy(但 EQ 会降级)
2. **DLL 路径查找顺序**:生产 `resource_dir` → 开发 `exe_dir` → fallback `native/out/{preset}`
3. **app_data_dir**:由 Tauri `path().app_data_dir()` 提供,DLL 用它定位 `bottlemusic.db`
4. **desktop-shell feature**:OS Media Session 集成仅在 `desktop-shell` feature 下启用(避免 `cargo test --lib` 时 tray-icon 崩溃)

## 请求流程

```mermaid
sequenceDiagram
    participant Vue as Vue 前端
    participant Rust as Rust lib.rs
    participant BA as backend_api.rs
    participant Sched as C++ RequestScheduler
    participant HTTP as C++ HttpClient
    participant KG as KuGou API

    Vue->>Rust: invoke('native_request', {method, path, query, headers, body})
    Rust->>Rust: deadline_for_path(path) — 外层 Tauri 超时
    Rust->>BA: handle_request(method, path, ...)
    BA->>BA: api_handle().read() — 读锁

    BA->>Sched: EchoHandleRequest(method, path, ...)
    Sched->>Sched: RequestKind + per-kind deadline
    Sched->>HTTP: dispatch (worker thread)
    HTTP->>HTTP: watchdog 启动
    HTTP->>KG: WinHTTP request (sign + encrypt)
    KG-->>HTTP: JSON response
    HTTP-->>Sched: parsed JSON
    Sched-->>BA: EchoResult (json string)
    BA->>BA: free_str(response)
    BA-->>Rust: Result<String, String>
    Rust-->>Vue: Result<String, String>

    alt 任一层超时
        Sched-->>BA: cancelled
        BA-->>Rust: error
        Rust-->>Vue: Err("request_deadline")
    end
```

### 三层 deadline

| 层 | 位置 | 触发 |
|---|---|---|
| 外层 | `lib.rs::deadline_for_path` | Tauri `tokio::time::timeout`,按 path 分类(10s/8s/6s/...) |
| 中层 | `native/async/RequestScheduler.cpp` | per-kind deadline,取消 token 传播 |
| 内层 | `native/core/HttpClient.cpp` | watchdog,超时关闭 WinHTTP 连接句柄 |

外层超时返回 `Err("request_deadline")`,前端 CircuitBreaker 计入失败次数。

## 关闭流程

```mermaid
sequenceDiagram
    participant User as 用户
    participant App as Tauri App
    participant Lib as lib.rs
    participant BA as backend_api.rs
    participant DLL as EchoCAPI.dll
    participant Sched as RequestScheduler

    User->>App: 关闭窗口
    App->>Lib: WindowEvent::CloseRequested
    Lib->>BA: shutdown_c_api()  (返回 void)
    BA->>BA: get_handle().try_write() — 5s deadline 轮询

    alt 5s 内获取写锁
        BA->>DLL: EchoShutdown()
        DLL->>Sched: scheduler.Shutdown(3000ms) — Phase 1

        alt abandoned == 0 (所有 worker 已完成)
            DLL->>DLL: Phase 2: try_lock(api_rwlock) — 3s deadline

            alt 3s 内获取独占锁
                DLL->>DLL: Ctx().api/stats/db.reset()<br/>+ CloseHttpConnectionPool()
                DLL-->>BA: shutdown_status = 0 (安全卸载)
                BA->>BA: drop CApiHandle — FreeLibrary
            else 3s 内未获取独占锁
                DLL-->>BA: shutdown_status = 1 (不安全)
                BA->>BA: mem::forget(handle) — 保留 DLL mapping
            end
        else abandoned > 0 (有 detached worker)
            DLL-->>BA: shutdown_status = abandoned (非零,跳过 Phase 2)
            BA->>BA: mem::forget(handle) — 保留 DLL mapping
        end
    else 5s 内未获取写锁
        BA->>BA: return — 跳过 EchoShutdown,保留 handle + DLL mapping
    end

    Lib->>App: 进程退出 (OS 回收所有资源)
```

### 关闭不变量

- **有界**:Rust 写锁 5s + C++ scheduler 3s + C++ 生命周期锁 3s,总上限 11s;超时不阻塞,保留 handle 由 OS 回收
- **独占**:Rust 端 `RwLock` 写锁保证 shutdown 期间无新 `handle_request` 调用;C++ 端 `shared_mutex` 独占锁保证 teardown 期间无 in-flight worker
- **安全卸载**:仅当 `EchoShutdown` 返回 0(所有 worker 已完成且独占锁获取成功)时才 `FreeLibrary`;非零返回保留 DLL mapping(避免 use-after-unload)

## 架构原则

1. **单一请求入口**:所有 KuGou API 调用经 `native_request` Tauri 命令 → `EchoHandleRequest` C ABI,无旁路
2. **签名不入 JS**:KuGou 签名盐和密钥在 C++ 编译期常量,JS 堆中无签名
3. **audio_proxy 服务端注入**:CDN Authorization header 由 Rust 端注入,JS 看不到
4. **HTML5-only 播放**:Media Foundation 播放栈已移除(2026-07-17),生产仅 `Html5AudioBackend`
5. **无 Pinia**:模块级 `reactive`/`ref` 单例 + HMR 共享引用(`window.__bottlemusic_audio__`)
6. **Storage Actor 串行化**:所有 SQLite 访问经 actor mailbox 串行化,无 TLS
7. **EQ 拓扑安全**:`captureStream → MediaStreamSource → AudioWorkletNode → GainNode → destination`,绝不 `createMediaElementSource`

## 子项目状态(S1-S5)

| 子项目 | 状态 | 关键交付 |
|---|---|---|
| S1 Resilience | ✅ Complete | 三层 deadline、CircuitBreaker、有界 Shutdown/Restart |
| S2 Auto-update/CI | ✅ Complete | ci.yml、release.yml、sync-version.mjs |
| S3 Skin system | ✅ Complete | themeStore、Aurora + Newsprint、FOUC 预防 |
| S4 Playback+EQ | ✅ Complete | HTML5 backend + Web Audio EQ、PlaySessionTracker |
| S5 Statistics | ✅ Complete | PlayStatsService、StatsView、DeepSeek AI |

详见各专题文档。
