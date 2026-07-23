# tauri-rust.md — Rust FFI 外壳层

> 本文档描述 BottleMusic 的 Rust FFI 外壳层(Tauri 2.0 应用壳),基于 `22ba7951` 基线。
> 事实来源:[evidence-report.md](./evidence-report.md)。源码位于 [`ui/src-tauri/`](../../ui/src-tauri/)。
> 本文明确区分**当前实现**、**已知风险**、**未来提案**。

## 概览

Rust FFI 外壳是 BottleMusic 三层架构的中间层,承担五个职责:

1. **Tauri 2.0 命令**:向前端暴露 19 个 `#[tauri::command]`,桥接 Vue 与 C++ 核心。
2. **DLL 加载**:通过 `libloading` 动态加载 `EchoCAPI.dll`,解析 C ABI 符号并持有函数指针。
3. **audio_proxy**:本地 loopback HTTP 代理,绕过浏览器 CORS/媒体限制,流式转发酷狗 CDN 音频。
4. **OS Media 集成**:托盘菜单 + 全局媒体快捷键,将物理按键转换为前端事件。
5. **DeepSeek AI 分析**:调用 DeepSeek Chat API,基于听歌统计生成自然语言洞察。

入口为 [`lib.rs`](../../ui/src-tauri/src/lib.rs) 的 `pub fn run()`,注册 `invoke_handler!` 并在 `setup` 闭包中完成 DLL 加载、audio_proxy 绑定、OS 集成安装。`run()` 同时注册三个 Tauri 插件:`tauri_plugin_opener`(外链打开)、`tauri_plugin_updater`(自动更新)、`tauri_plugin_process`(进程重启);`desktop-shell` feature 下额外注册 `tauri_plugin_global_shortcut`。

`run()` 还注册了窗口关闭事件处理器:`on_window_event` 监听 `WindowEvent::CloseRequested`,触发 `backend_api::shutdown_c_api()` 执行有界关闭(详见 CApiHandle 章节)。这是 DLL 卸载的唯一触发点。

### Cargo.toml 依赖

见 [`Cargo.toml`](../../ui/src-tauri/Cargo.toml):

| 依赖 | 版本 | 用途 |
|---|---|---|
| `tauri` | 2 | 应用框架、IPC、窗口、托盘 |
| `tauri-plugin-opener` | 2 | 打开外链(限定 `m.kugou.com/*`) |
| `tauri-plugin-updater` | 2 | GitHub Release 自动更新 |
| `tauri-plugin-process` | 2 | 进程重启 |
| `tauri-plugin-global-shortcut` | 2(optional) | 媒体快捷键,`desktop-shell` feature 启用 |
| `tokio` | 1(rt/rt-multi-thread/macros/time/net/io-util) | 异步运行时、`timeout`、`spawn_blocking` |
| `reqwest` | 0.12(json/stream) | audio_proxy 上游请求、DeepSeek 请求 |
| `libloading` | 0.9.0 | 动态加载 `EchoCAPI.dll` |
| `sysinfo` | 0.30 | `get_memory_usage` 命令读取进程内存 |
| `chrono` | 0.4 | 日志时间戳、stats 测试时间构造 |
| `serde` / `serde_json` | 1 | 命令参数序列化 |
| `futures-util` | 0.3 | audio_proxy 流式 `bytes_stream` |
| `getrandom` | 0.2 | audio_proxy 随机路由 ID |

`build-dependencies` 仅 `tauri-build = { version = "2", features = [] }`,负责 Tauri 构建期代码生成(`generate_context!`、资源打包、deadlines_generated.rs 提取)。

## 模块结构

[`lib.rs`](../../ui/src-tauri/src/lib.rs) 顶部声明 **5 个**模块:

```rust
mod ai_analysis;
mod audio_proxy;
mod backend_api;
mod os_media_session;
mod stats;
```

> **事实纠正**:`playback.rs` **不存在**。旧 `Code-Wiki.md` 描述的 "13 Tauri playback commands" 已随 MFS 播放栈移除而清理(见 [evidence-report.md § 7.3](./evidence-report.md#73-uisrc-taurisrcplaybackrs已确认不存在))。当前 mod 声明仅 5 个,无 playback。

## Tauri 命令注册

`run()` 中的 `invoke_handler!` 注册 **19 个命令**,按模块分组:

| 分组 | 命令 | 定义位置 |
|---|---|---|
| 通用(4) | `ping`, `backend_base_url`, `get_memory_usage`, `native_request` | [`lib.rs`](../../ui/src-tauri/src/lib.rs) |
| audio_proxy(1) | `audio_proxy_url` | [`audio_proxy.rs`](../../ui/src-tauri/src/audio_proxy.rs) |
| ai_analysis(1) | `ai_analyze` | [`ai_analysis.rs`](../../ui/src-tauri/src/ai_analysis.rs) |
| stats(6) | `stats_record_play`, `stats_get_summary`, `stats_get_top`, `stats_get_timeline`, `stats_get_recent`, `stats_get_recommendations` | [`stats.rs`](../../ui/src-tauri/src/stats.rs) |
| os_media(7) | `os_media_bind`, `os_media_unbind`, `os_media_set_now_playing`, `os_media_set_playback_status`, `os_media_set_enabled_controls`, `os_media_inject_button`, `os_media_debug_snapshot` | [`os_media_session.rs`](../../ui/src-tauri/src/os_media_session.rs) |

> **重要纠正**:旧 `Code-Wiki.md` 称主请求命令为 `echo_request` — **错误**,实际命令名是 **`native_request`**(见 [evidence-report.md § 1.2](./evidence-report.md#12-tauri-ipc-命令已确认19-个)))。前端通过 `invoke('native_request', ...)` 调用。

### 通用命令说明

除 `native_request`(下一节详解)外,三个轻量通用命令定义在 [`lib.rs`](../../ui/src-tauri/src/lib.rs):

- `ping() -> &'static str`:返回 `"pong"`,前端启动时探活 IPC 通道。
- `backend_base_url() -> &'static str`:返回 `"native-ipc"` 占位值。注释说明这是为避免前端立即报错而保留的 dummy 值 —— 真实请求全部走 `native_request` FFI,不走 HTTP base URL。
- `get_memory_usage() -> u64`:用 `sysinfo` 读取当前进程内存(`Pid::from_u32(std::process::id())` + `refresh_process`),供前端监控内存占用。

## `native_request` 命令详解

`native_request` 是前端发往 C++ 核心的主请求通道,签名:

```rust
async fn native_request(
    method: String,
    path: String,
    query_json: Option<String>,
    headers_json: Option<String>,
    body: Option<String>,
) -> Result<String, String>
```

### 外层超时分类(`deadline_for_path`)

[`lib.rs`](../../ui/src-tauri/src/lib.rs) 的 `deadline_for_path(path: &str) -> Duration` 按 URL 前缀将请求分到不同 deadline 桶,值来自 `native/include/echo/core/RequestDeadlines.h`(由 `build.rs` 在编译期生成到 `OUT_DIR/deadlines_generated.rs`,通过 `mod deadlines` 引入):

```rust
#[allow(non_upper_case_globals, dead_code)]
mod deadlines {
    include!(concat!(env!("OUT_DIR"), "/deadlines_generated.rs"));
}
```

注释说明:常量名镜像 C++ 的 `kCamelCase` 以实现跨语言身份对齐。`deadline_for_path` 仅作**外层看门狗**,不做业务逻辑路由。

| 路径前缀 | deadline 桶 | 值(由测试断言确认) |
|---|---|---|
| `/song/url` | `kDeadlineSongUrlMs` | 10s |
| `/images/` | `kDeadlineImageMs` | 8s |
| `/login/qr/` | `kDeadlineLoginPollMs` | 6s |
| `/search` | `kDeadlineSearchMs` | 独立桶(值由 C++ 头定义) |
| `/playlist` / `/rank` / `/top/` / `/album` / `/artist` | `kDeadlinePlaylistMs` | 共用桶 |
| 其它 | `kDeadlineGenericMs` | 12s |

### 执行模型

`native_request` 用 `tokio::time::timeout(deadline, spawn_blocking(...))` 包装 `backend_api::handle_request`:

- `spawn_blocking` 把 FFI 调用移到阻塞线程池,避免阻塞 tokio 异步运行时。
- 超时返回 `Err("request_deadline".to_string())`;`JoinError` 返回 `Err("Task panic: ...")`。
- 这是**外层看门狗**,仅作兜底;真正的超时控制由 C++ `RequestScheduler` 内层处理(见 [native-cpp.md](./native-cpp.md))。
- 测试 `rust_outer_deadlines_are_at_least_cpp_inner` 断言外层 deadline 不短于 C++ 内层预算。

`handle_request` 在 `backend_api.rs` 中实现:先在锁外构建所有 `CString`(method/path/query/headers/body),缩小读锁持有窗口;再获取读锁,调 `EchoHandleRequest` 并通过 out 参数 `*mut *mut c_char` 接收响应。响应 null 时返回 `Err("Empty response from C API")`,非 null 时 `CStr::to_string_lossy` 转换后立即调 `EchoFreeString` 释放。

## CApiHandle 与 DLL 加载

[`backend_api.rs`](../../ui/src-tauri/src/backend_api.rs) 负责动态加载 `EchoCAPI.dll` 并持有解析后的 C ABI 函数指针。

### 数据结构

```rust
pub struct CApiHandle {
    _lib: Library,                              // 持有 DLL 句柄,生命周期 = 进程
    pub(crate) handle_req: ...,                  // EchoHandleRequest
    pub(crate) free_str: ...,                    // EchoFreeString
    shutdown: ...,                               // EchoShutdown
    pub(crate) stats_record_play: ...,           // EchoStatsRecordPlay
    pub(crate) stats_get_summary: ...,           // EchoStatsGetSummary
    pub(crate) stats_get_top: ...,               // EchoStatsGetTop
    pub(crate) stats_get_timeline: ...,          // EchoStatsGetTimeline
    pub(crate) stats_get_recent: ...,            // EchoStatsGetRecent
    pub(crate) stats_get_recommendations: ...,   // EchoStatsGetRecommendations
}

static C_API_HANDLE: OnceLock<RwLock<Option<CApiHandle>>> = OnceLock::new();
```

**并发模型**:`RwLock<Option<CApiHandle>>` —— 读锁允许多个请求并发调用 C ABI(每个 `handle_request` / stats 命令持有读锁贯穿整个 FFI 调用);写锁仅在 `shutdown_c_api` 时获取,会等待所有在飞读锁释放后才卸载 DLL。

### 符号解析(`init_with_paths`)

`init_with_paths(dll_path: &str, app_data_dir: Option<&str>)` 在写锁内完成:

1. `Library::new(dll_path)` 加载 DLL。
2. **先解析全部必需符号**再初始化 —— 若 DLL 不兼容,直接 drop `Library` 是安全的(无 C++ 线程存在)。解析的符号:`EchoInitializeWithPathsV2`、`EchoInitializeV2`、`EchoGetLastError`、`EchoShutdown`、`EchoHandleRequest`、`EchoFreeString`、`EchoStats*`(6 个)。
3. 调用 `EchoInitializeWithPathsV2(app_data_dir)`(传入时)或 `EchoInitializeV2()`(回退);`app_data_dir` 决定 SQLite `bottlemusic.db` 的位置。
4. 初始化失败(`init_status != 0`)时读取 `EchoGetLastError`,调用 `EchoShutdown`,返回错误 —— **不发布 handle**。
5. 成功则构造 `CApiHandle` 写入 `RwLock`。

### DLL 路径查找(3 候选)

[`lib.rs`](../../ui/src-tauri/src/lib.rs) `setup` 闭包按顺序尝试 3 个路径,首个存在的胜出:

1. **生产**:`resource_dir.join(dll_name)` —— Tauri 资源目录(`tauri.conf.json` `bundle.resources` 打包)。
2. **开发(exe 同级)**:`exe_dir.join(dll_name)` —— `current_exe` 父目录,由 `build.rs` 拷贝。
3. **开发回退**:`CARGO_MANIFEST_DIR/../../native/out/{preset}/{dll_name}` —— 源码树构建产物;`preset` = `bottlemusic-check`(debug)或 `bottlemusic-release`(release)。

加载成功后立即调用 `backend_api::set_log_dir(&app_data_dir)` 和 `backend_api::set_log_callback()`。**顺序关键**:`set_log_dir` 必须在 `set_log_callback` 之前,因为日志回调一旦触发就会惰性初始化 `LOG_FILE`,路径由 `LOG_DIR` 决定。

### 日志回调

`set_log_callback` 通过 `EchoSetLogCallback` 注册 `ffi_log_callback`(Rust `extern "C" fn`),将 C++ 日志转发到 Rust stdout/stderr 和按天分文件(`<app_data_dir>/logs/bottlemusic-YYYYMMDD.log`)。

### shutdown(`shutdown_c_api`)

窗口关闭事件触发 `shutdown_c_api`,采用**有界非阻塞**策略:

- 用 `try_write()` 轮询最多 5s(每 50ms 一次),**不**退化为阻塞 `write()` —— 避免在飞 `spawn_blocking` 任务持有读锁时永久挂起。
- 5s 内拿不到写锁则放弃,留给 OS 在进程退出时回收。
- 拿到后调用 `EchoShutdown()`;若返回非零(表示 C++ 仍有 worker 在 DLL 内执行),`std::mem::forget(handle)` **泄漏 DLL 映射**,避免 use-after-unload。
- 若返回零,正常 `drop(handle)` 释放 `Library`(触发 `FreeLibrary`)。

### 日志目录与回退

`LOG_DIR` 由 `set_log_dir` 设置(通常为 `app_data_dir`),`log_file()` 按优先级枚举候选根目录:

1. `LOG_DIR/logs`(宿主指定,跨平台、用户可写、与 SQLite 同根 —— 安装版唯一可靠位置)
2. `exe_dir/logs`(exe 同级)
3. `logs/`(当前工作目录)

第一个 `create_dir_all` 成功的胜出;日志文件按天命名 `bottlemusic-YYYYMMDD.log`,`OpenOptions::create + append`。`LOG_FILE` 用 `OnceLock<Mutex<Option<File>>>` 惰性初始化,首次写入决定路径。

### 并发安全设计

`CApiHandle` 的并发模型核心是**读写锁分离**:

- **读路径**(`api_handle()` / `handle_request` / stats 命令):获取 `RwLockReadGuard`,允许多个 FFI 调用并发执行。读锁贯穿整个 C ABI 调用,确保 DLL 不会被中途卸载。
- **写路径**(`init_with_paths` / `shutdown_c_api`):获取 `RwLockWriteGuard`,独占。初始化只发生一次(`OnceLock` + 写锁内 `if guard.is_some() return Ok`);shutdown 用有界 `try_write` 避免死锁。
- **测试守卫**:`TEST_C_API_GUARD: Mutex<()>` + `lock_test_c_api()` 在测试间串行化 DLL 初始化,防止多测试争抢全局 handle。

`test_m3_concurrency` 是关键并发验证:20 线程各发 50 对请求(`/health` + `/settings/device`),共 2000 次请求。RequestScheduler `maxQueue = workers*4`,20 并发下可能返回 504 `queue_full`(背压而非崩溃),`request_until_ok` 最多重试 80 次(指数退避 2-22ms)。断言零线程 panic,验证 FFI 边界在并发下的稳定性。

```mermaid
sequenceDiagram
    participant Setup as lib.rs setup
    participant BA as backend_api
    participant DLL as EchoCAPI.dll
    Setup->>Setup: 枚举 3 候选路径(resource_dir/exe_dir/native/out)
    loop 每个候选路径
        Setup->>Setup: path.exists()?
        alt 存在
            Setup->>BA: init_with_paths(path, app_data_dir)
            BA->>BA: 写锁;Library::new(path)
            BA->>DLL: 解析 EchoInitializeWithPathsV2 等 12 符号
            BA->>DLL: EchoInitializeWithPathsV2(app_data_dir)
            alt init_status == 0
                BA->>BA: CApiHandle 写入 RwLock
                BA-->>Setup: Ok
                Setup->>BA: set_log_dir(app_data_dir)
                Setup->>BA: set_log_callback()
                BA->>DLL: EchoSetLogCallback(ffi_log_callback)
            else init_status != 0
                BA->>DLL: EchoGetLastError + EchoShutdown
                BA-->>Setup: Err(message)
            end
        else 不存在
            Setup->>Setup: 记录 "missing",下一候选
        end
    end
```

## audio_proxy

[`audio_proxy.rs`](../../ui/src-tauri/src/audio_proxy.rs) 实现本地 loopback HTTP 代理,解决前端 `<audio>` 直接请求酷狗 CDN 时的 CORS 与签名泄漏问题。

### 绑定与启动

- `bind_listener()`:`StdTcpListener::bind(("127.0.0.1", 0))` —— **随机端口**,仅 loopback;`set_nonblocking(true)` 以便转交 tokio。
- `setup` 闭包拿到 `(listener, port)` 后构造 `AudioProxyState::new(port)`、`app.manage(state)` 注册为 Tauri managed state,并 `spawn(audio_proxy::serve(listener, state))`。
- 绑定失败则 `app.manage(AudioProxyState::disabled())`,`audio_proxy_url` 命令会返回 `audio_proxy_unavailable`。
- `serve` 将 `StdTcpListener` 转为 `tokio::net::TcpListener`(from_std),进入 accept 循环;每个连接 `spawn(handle_client(stream, state))` 独立处理。accept 失败时打印 WARN 并退出循环。

### AudioProxyState

```rust
#[derive(Clone)]
pub struct AudioProxyState {
    inner: Arc<AudioProxyInner>,
}
struct AudioProxyInner {
    port: u16,
    routes: Mutex<HashMap<String, RouteEntry>>,
}
struct RouteEntry { url: String, created_at: Instant }
```

- `MAX_ROUTES = 128`:路由表上限,满时按 `created_at` 淘汰最旧条目(无 TTL,容量驱动)。
- `register(url)`:校验 `is_supported_audio_url`,生成 16 字节随机 hex(32 字符)路由 ID,返回 `http://127.0.0.1:{port}/audio/{id}`。
- `resolve(id)`:查表返回上游 URL。

### Tauri 命令

`audio_proxy_url(url: String, state: State<'_, AudioProxyState>) -> Result<String, String>`:前端传入 CDN URL,拿到 loopback 代理 URL。

### SSRF allowlist

`is_supported_audio_url` + `is_allowed_kugou_cdn_host` 严格限定上游 host:

- `imge.kugou.com`,或
- `fs.{字母数字}.kugou.com`(`fs.` 前缀 + 单段 alnum label + `.kugou.com` 后缀)

拒绝 `file:///`、`127.0.0.1`、`169.254.169.254`(云元数据)、`cdn.example`、后缀攻击(`fs.kugou.com.evil.com`)、空 label(`fs..kugou.com`)。测试 `allowlist_rejects_suffix_and_trailing_domain_attacks` 覆盖这些场景。host 在匹配前先 `to_ascii_lowercase()`,因此大小写混合的 `FS.YouthAndroid.kugou.com` 也被接受(测试 `supported_audio_url_allows_only_kugou_file_cdn_hosts` 验证)。`redirect_policy_rejects_local_private_and_non_kugou_targets` 测试覆盖 `localhost`、`127.0.0.1`、`169.254.169.254`、`10.0.0.4` 等内网地址的重定向拒绝。

### 重定向策略

`shared_audio_proxy_client()` 返回进程级 `OnceLock<reqwest::Client>`(P1-H 优化:复用 CDN keep-alive),`redirect::Policy::custom` 配合 `audio_redirect_decision`:

- 最多 `MAX_AUDIO_REDIRECTS = 5` 跳;
- 每跳目标必须通过 `is_supported_audio_url`;
- 拒绝则 `attempt.stop()`(不暴露重定向目标 URL)。

### CORS 头

`append_cors_headers` 仅对 4 个允许的 origin 反射 `Access-Control-Allow-Origin`:`tauri://localhost`、`http://tauri.localhost`、`https://tauri.localhost`、`http://localhost:1420`。**绝不使用通配符 `*`**;无 Origin 头时不输出 ACAO。测试 `options_omits_access_control_allow_origin_when_origin_header_absent` 守护此安全关键点。

### Range / 断点续传

`handle_client` 支持 HTTP Range 请求与断点续传:

- 透传前端 `Range` 头到上游,回传 `Content-Range` / `Accept-Ranges` / `Content-Length`。
- `ResumePlan::from_headers` 解析 206 响应的 body 区间;上游 body 读失败时按 `forwarded_bytes` 计算 `retry_range`,用新 Range 重发(最多 `BODY_RETRY_LIMIT = 2` 次)。
- `validate_retry_response` 校验重试响应的 206 状态与 `Content-Range` 起始字节。
- 错误信息经 `redact_url_queries` 脱敏,签名 URL 的 query 部分(含 `auth`/`ssig`/`token`)替换为 `<redacted>`。

### 请求处理流程

`handle_client` 的处理顺序:

1. `read_http_request` 读取原始 HTTP 请求(上限 16KB,防头部膨胀),按 `\r\n\r\n` 截断。
2. `OPTIONS` 预检请求返回 204 + CORS 头;非 `GET` 返回 405。
3. 路径必须匹配 `/audio/{id}`,否则 404;`resolve(id)` 失败(路由不存在)返回 404 "audio route expired"。
4. 构造上游请求:固定 `User-Agent: BottleMusic/1.0 audio proxy`、`Accept: audio/*,*/*`,透传 `Range`。
5. 流式转发:`upstream.bytes_stream()` 逐 chunk `write_all` 到客户端,累加 `forwarded_bytes`。
6. 客户端断开(`is_client_disconnect` 检测 "client write failed")降级为 DEBUG 日志,其余为 WARN。

### 错误诊断格式

`proxy_error` 生成结构化错误串:`route={id} upstream={host} status={code} phase={phase} bytes={n}: {detail}`。`phase` 标识失败阶段(`upstream_client` / `upstream_request` / `upstream_body` / `response_headers` / `client_body` / `upstream_retry_*`),`bytes` 标识已转发字节数,便于定位断点位置。

```mermaid
sequenceDiagram
    participant Vue as Vue <audio>
    participant Proxy as audio_proxy (127.0.0.1:rand)
    participant CDN as 酷狗 CDN
    Vue->>Vue: invoke('audio_proxy_url', cdnUrl)
    Vue->>Proxy: 注册路由,拿到 /audio/{id}
    Vue->>Proxy: GET /audio/{id}  Range: bytes=100-109
    Proxy->>Proxy: resolve(id) → cdnUrl;SSRF allowlist 校验
    Proxy->>CDN: GET cdnUrl  Range: bytes=100-109
    CDN-->>Proxy: 206 Partial Content + Content-Range
    Proxy-->>Vue: 206 + CORS 头 + 流式 body chunk
    Note over Proxy,CDN: body 读失败时:按已转发字节<br/>计算 retry_range,重发 Range(≤2 次)
    Proxy-->>Vue: 续传剩余字节直到完成
```

## stats 命令

[`stats.rs`](../../ui/src-tauri/src/stats.rs) 的 6 个 `#[tauri::command]` 一一映射到 6 个 C ABI 符号,每个命令获取 `backend_api::api_handle()` 读锁后调用对应函数指针,返回的 `*mut c_char` 用 `CStr::to_string_lossy` 转 `String` 后立即调 `free_str` 释放(C++ 分配,Rust 释放):

| Tauri 命令 | C ABI 符号 | 签名要点 |
|---|---|---|
| `stats_record_play(json)` | `EchoStatsRecordPlay` | 写入,无返回值;无效 JSON 静默 no-op |
| `stats_get_summary(range)` | `EchoStatsGetSummary` | 返回汇总 JSON |
| `stats_get_top(kind, range, limit)` | `EchoStatsGetTop` | 按 song/artist/album 维度排行 |
| `stats_get_timeline(range)` | `EchoStatsGetTimeline` | 按日聚合的播放时间线 |
| `stats_get_recent(limit, offset)` | `EchoStatsGetRecent` | 分页最近播放 |
| `stats_get_recommendations(limit)` | `EchoStatsGetRecommendations` | 基于历史的推荐 |

所有命令在 handle 为 `None` 时返回 `Err("C API not loaded")`;返回 null 指针时返回 `Err("null ...")`。集成测试 `test_stats_ffi_end_to_end` 需真实 DLL,覆盖写入、汇总、排行、时间线、最近、推荐全链路。

测试要点:

- `test_stats_ffi_end_to_end`:构造 6 条记录(album-1 五条 + album-2 一条,同名 "Album One" 但不同 `album_id`),验证按 `album_id` 分组不合并;验证 `total_plays=6`、`unique_songs=3`、`unique_artists=2`;验证无效 JSON 静默 no-op(计数不变)。
- `test_stats_returns_err_without_dll`:`shutdown_c_api()` 后调用 `stats_get_summary` 应返回 `Err`。
- 测试用 `backend_api::lock_test_c_api()` 获取进程级 `TEST_C_API_GUARD` Mutex,串行化 DLL 初始化(避免多测试争抢同一全局 handle)。

## ai_analysis

[`ai_analysis.rs`](../../ui/src-tauri/src/ai_analysis.rs) 的 `ai_analyze` 是 `async` Tauri 命令,调用 DeepSeek Chat API。

### 当前实现

- `DEEPSEEK_API_URL = "https://api.deepseek.com/chat/completions"`,`DEEPSEEK_MODEL = "deepseek-chat"`,`REQUEST_TIMEOUT = 30s`。
- `shared_ai_client()`:`OnceLock<reqwest::Client>`,30s 超时,进程级复用。
- 签名:`ai_analyze(api_key: String, stats_json: String, custom_prompt: Option<String>) -> Result<String, String>`。
- 请求体:`ChatRequest { model, messages: [system, user], stream: false }`。系统提示为固定的 `DEFAULT_SYSTEM_PROMPT`(英文,要求用用户提示语言回复);用户内容 = `custom_prompt` + `stats_json`,或默认 "Please analyze my listening statistics..."。
- `api_key` 通过 `Authorization: Bearer {api_key}` 头发送;**从前端传入,Rust 不持久化**(见 [security-and-privacy.md](./security-and-privacy.md) 与 [evidence-report.md § 5](./evidence-report.md#5-deepseek-key-真实存储生命周期)))。
- 响应解析:`ChatResponse { choices: Vec<Choice> }`,`Choice.message.content` 取首条;非 2xx 返回 `Err("DeepSeek API error ({status}): {body}")`;空 choices 返回 `Err("Empty response from DeepSeek API")`。
- 空 `api_key` 立即返回 `Err("API key is required")`,不发请求(测试 `test_ai_analyze_rejects_empty_key` 守护)。

### Key 生命周期

前端 [`StatsView.vue`](../../ui/src/views/StatsView.vue) 用 `ref('')` 在内存中持有 key,模块加载时 `localStorage.removeItem('deepseek_api_key')` 清理升级用户的旧数据。Key 仅在当前页面会话内存,不写 localStorage/磁盘,调用 `ai_analyze` 时通过 IPC 参数传入 Rust,Rust 用完即弃。详见 [evidence-report.md § 5](./evidence-report.md#5-deepseek-key-真实存储生命周期)。

### 已知风险

> **`DEEPSEEK_API_URL` 缺少 `/v1` 前缀**。当前值 `https://api.deepseek.com/chat/completions`,而 DeepSeek 官方文档的标准端点是 `https://api.deepseek.com/v1/chat/completions`。这要么是一个待修正的偏差,要么依赖 DeepSeek 服务端的重定向兼容(未验证)。详见 [maintenance.md](./maintenance.md)。

## os_media_session

[`os_media_session.rs`](../../ui/src-tauri/src/os_media_session.rs) 暴露 7 个 Tauri 命令,管理一个进程级 `SessionState`,并通过托盘 + 全局快捷键将 OS 级输入转发为前端事件 `os-media-button`。

### 数据结构

```rust
pub enum MediaButton { Play, Pause, PlayPause, Next, Prev }
pub enum PlaybackStatus { Playing, Paused, Stopped }
pub struct NowPlaying { title, artist, album?, artwork_url? }
pub struct EnabledControls { play_pause, next, prev }
struct SessionState { bound, now_playing?, status, controls, pending_buttons }
static SESSION: OnceLock<Mutex<SessionState>>;
```

### 7 个命令

| 命令 | 行为 |
|---|---|
| `os_media_bind` | `bound = true`,启用其余命令 |
| `os_media_unbind` | 重置 `SessionState` 为默认 |
| `os_media_set_now_playing` | 未 bind 返回 `session_not_bound` |
| `os_media_set_playback_status` | 设置 Playing/Paused/Stopped |
| `os_media_set_enabled_controls` | 设置可用按键 |
| `os_media_inject_button` | 调 `inject_button` 下发按钮 |
| `os_media_debug_snapshot` | 返回诊断 JSON(含 `desktop_shell` 标志) |

`os_media_debug_snapshot` 返回的 JSON 包含:`bound`(是否已绑定)、`status`(当前播放状态)、`has_track`(是否有 now_playing)、`controls`(已启用控件)、`pending_len`(待处理按钮队列长度)、`desktop_shell`(编译期 `cfg!(feature = "desktop-shell")` 值)。这是排查 OS 集成状态的主要诊断入口。

### 事件下发(`inject_button`)

- `desktop-shell` feature 开启且 `APP` 已设置时,通过 `app.emit("os-media-button", button)` 直接推送到前端。
- 否则压入 `pending_buttons` 队列(测试用 `take_pending_buttons` 取出)。

### desktop-shell 集成

`install_os_integrations(app)` 仅在 `#[cfg(feature = "desktop-shell")]` 下编译,做两件事:

1. **托盘菜单**(`install_tray`):菜单项 "播放/暂停"、"下一首"、"上一首"、"显示窗口"、"退出";点击菜单项调 `inject_button` 或显示主窗口;"退出" 调 `shutdown_c_api()` 后 `app.exit(0)`。左键单击托盘图标显示主窗口。图标取 `app.default_window_icon()`。
2. **全局媒体快捷键**(`install_media_key_shortcuts`):注册 `MediaPlayPause` / `MediaTrackNext` / `MediaTrackPrevious`,按下时调 `inject_button`。

### 事件契约

前端通过 `listen('os-media-button', callback)` 接收 `MediaButton` 枚举(PascalCase 序列化:`Play` / `Pause` / `PlayPause` / `Next` / `Prev`)。这是 OS 物理输入到前端播放控制的唯一通道:托盘菜单点击 → `inject_button` → emit;全局快捷键按下 → `inject_button` → emit;前端 `os_media_inject_button` 命令 → `inject_button` → emit。

> **事实说明**:当前 `os_media_session.rs` **未使用** Windows `SystemMediaTransportControls` 原生 API,而是通过 Tauri 托盘 + `tauri-plugin-global-shortcut` 实现等价功能。模块名为 "os_media_session" 但实现层是托盘 + 快捷键 + 事件下发。

## desktop-shell feature

[`Cargo.toml`](../../ui/src-tauri/Cargo.toml) 定义:

```toml
[features]
default = ["desktop-shell"]
desktop-shell = ["tauri/tray-icon", "dep:tauri-plugin-global-shortcut"]
```

`desktop-shell` 启用 `tauri/tray-icon`(托盘图标支持)和 `tauri-plugin-global-shortcut`(全局快捷键)。默认开启,但在测试场景下必须关闭。

> **测试约束**:`cargo test --lib` **必须带 `--no-default-features`**,否则 `desktop-shell` 启用 `tray-icon`,导致 `STATUS_ENTRYPOINT_NOT_FOUND` 崩溃测试 harness(见 [evidence-report.md § 3.3](./evidence-report.md#33-rust-测试约束已确认))。CI 用 `cargo test --lib --no-default-features -- --test-threads=1` 规避。

## 配置

### tauri.conf.json

见 [`tauri.conf.json`](../../ui/src-tauri/tauri.conf.json):

- `productName = "BottleMusic"`,`identifier = "com.bottlemusic.app"`,`version = "1.0.0"`。
- `build.beforeDevCommand = "pnpm dev"`,`beforeBuildCommand = "pnpm build"`,`frontendDist = "../dist"`,`devUrl = "http://localhost:1420"`。
- 窗口:`decorations: false`(无边框,前端自绘标题栏),`1280×820`,`minWidth/minHeight = 1024/700`,`resizable: true`。
- **严格 CSP**:`default-src 'self'`;`connect-src` 仅 `ipc:`、`http://ipc.localhost`、`http://127.0.0.1:*`(audio_proxy);`object-src 'none'`、`frame-src 'none'`、`form-action 'none'`;`media-src` 允许 `http://127.0.0.1:*` 以支持 loopback 代理;`img-src` 允许 `http`/`https`/`blob`/`data`(封面图);`script-src 'self' blob'`;`style-src 'self' 'unsafe-inline'`(Vue 运行时样式)。
- `devCsp` 额外放行 dev server(`localhost:1420`/`1421`、`ws://`)用于 HMR。
- 安全头:`X-Content-Type-Options: nosniff`,`Permissions-Policy: camera=(), microphone=(), geolocation=()`。
- `bundle.targets = "all"`(旧 Wiki 称 `["nsis"]` —— 错误);`bundle.category = "Music"`,`publisher = "Ningbottle"`;`windows.nsis.installMode = "currentUser"`(免管理员)。
- `bundle.resources`:`libs/EchoCAPI.dll` → `EchoCAPI.dll`,`libs/sqlite3.dll` → `sqlite3.dll`(**不含 server/**,见 [evidence-report.md § 1.3](./evidence-report.md#13-server-是否进入生产链路已确认不进入)))。
- `bundle.createUpdaterArtifacts = true`:生成更新签名产物。
- `plugins.updater`:endpoint 指向 GitHub Releases `latest.json`,内置 minisign 公钥(`pubkey`),由 `TAURI_SIGNING_PRIVATE_KEY` 签名(见 [testing-and-release.md](./testing-and-release.md))。

### capabilities/default.json

见 [`capabilities/default.json`](../../ui/src-tauri/capabilities/default.json),最小权限白名单,仅 `windows: ["main"]`:

- `core:event:allow-listen` / `allow-unlisten`(监听 `os-media-button` 等事件)
- `core:window:allow-start-dragging` / `allow-minimize` / `allow-toggle-maximize` / `allow-close`(无边框窗口拖动 + 窗口控制)
- `core:tray:default` / `core:menu:default`(托盘菜单)
- `opener:allow-open-url` 限定 `https://m.kugou.com/*`
- `updater:allow-check` / `allow-download-and-install`
- `process:allow-restart`

## 测试

### 单元/集成测试分布

src/ 内共 **34 个** `#[test]` / `#[tokio::test]`(见 [evidence-report.md § 3.2](./evidence-report.md#32-真实用例统计2026-07-23-基线))),按模块:

- [`lib.rs`](../../ui/src-tauri/src/lib.rs):6 个 —— `deadline_for_path` 各桶断言、`native_request_times_out_when_handler_sleeps`、外层 ≥ 内层断言。
- [`backend_api.rs`](../../ui/src-tauri/src/backend_api.rs):初始化失败不发布 handle、`test_m3_concurrency`(20 线程 × 50 请求并发压测,需 DLL)。
- [`audio_proxy.rs`](../../ui/src-tauri/src/audio_proxy.rs):~11 个 —— SSRF allowlist、重定向策略、CORS 反射、Range 续传、路由表容量、错误脱敏。
- [`stats.rs`](../../ui/src-tauri/src/stats.rs):端到端 FFI + 无 DLL 返回 err。
- [`ai_analysis.rs`](../../ui/src-tauri/src/ai_analysis.rs):3 个 —— 序列化、反序列化、空 key 拒绝。
- [`os_media_session.rs`](../../ui/src-tauri/src/os_media_session.rs):bind/metadata 往返、debug_snapshot。

### 关键测试用例

- `native_request_times_out_when_handler_sleeps`([`lib.rs`](../../ui/src-tauri/src/lib.rs)):用 100ms timeout 包装 60s sleep,验证超时分支返回 `Err`,不依赖 DLL。
- `initialization_failure_is_returned_without_publishing_a_handle`([`backend_api.rs`](../../ui/src-tauri/src/backend_api.rs)):传入普通文件作为 `app_data_dir`,验证初始化失败返回错误且**不发布 handle**(`api_handle().is_err()`)。
- `test_m3_concurrency`([`backend_api.rs`](../../ui/src-tauri/src/backend_api.rs)):20 线程并发压测,需 DLL,验证零 panic。
- `get_streams_upstream_body_without_buffering_entire_response`([`audio_proxy.rs`](../../ui/src-tauri/src/audio_proxy.rs)):验证流式转发不缓冲整个响应,首个 chunk 在上游完成前到达。
- `upstream_body_error_resumes_partial_content_from_failed_offset`([`audio_proxy.rs`](../../ui/src-tauri/src/audio_proxy.rs)):验证 206 断点续传,重试请求的 Range 从失败偏移恢复。

### 集成测试

[`tests/playback_ffi_test.rs`](../../ui/src-tauri/tests/playback_ffi_test.rs) 是唯一的 `tests/` 集成测试文件,**需要 `EchoCAPI.dll` 存在**,默认不跑(CI 只跑 `cargo test --lib`)。详见 [maintenance.md](./maintenance.md)。

### CI 命令

```
cargo test --lib --no-default-features -- --test-threads=1   # 单元测试(规避 tray-icon 崩溃)
cargo check --lib                                             # 默认 features 编译检查
cargo clippy --no-default-features -D warnings                # lint,warnings 视为错误
```

`--no-default-features` 关闭 `desktop-shell`,从而不引入 `tray-icon` 链接,避免 `STATUS_ENTRYPOINT_NOT_FOUND`。`--test-threads=1` 串行化执行,因为多个测试共享全局 `C_API_HANDLE`(通过 `TEST_C_API_GUARD` Mutex 互斥)。`cargo check --lib` 用默认 features 确保生产配置可编译;`cargo clippy` 用 `--no-default-features` 与测试一致。三条命令互补,共同覆盖编译正确性、lint 合规与单元测试。

### crate-type

[`Cargo.toml`](../../ui/src-tauri/Cargo.toml) `[lib]` 声明 `crate-type = ["staticlib", "cdylib", "rlib"]`,lib 名为 `ui_lib`。注释说明 `_lib` 后缀在 Windows 上是必要的,以避免与 bin 名冲突(见 [cargo#8519](https://github.com/rust-lang/cargo/issues/8519))。

## 已知风险

| 风险 | 说明 | 详见 |
|---|---|---|
| DeepSeek URL 缺 `/v1` 前缀 | `DEEPSEEK_API_URL = "https://api.deepseek.com/chat/completions"`,标准端点应为 `.../v1/chat/completions`。可能依赖服务端兼容或导致请求失败 | [maintenance.md](./maintenance.md) |
| `playback_ffi_test.rs` 默认不跑 | 集成测试需 DLL 存在,CI 不执行;本地 `cargo test` 会跑(若 DLL 在位) | [maintenance.md](./maintenance.md) |
| shutdown 5s 超时后泄漏 DLL | `shutdown_c_api` 拿不到写锁则放弃,依赖 OS 回收;非零 shutdown status 时 `mem::forget` 有意泄漏 | 本文 CApiHandle 章节 |
| audio_proxy 无 TTL | 路由仅靠 `MAX_ROUTES = 128` 容量淘汰,无时间过期;长时间运行可能积累陈旧路由 | 本文 audio_proxy 章节 |

## 未来提案

| 提案 | 说明 | 详见 |
|---|---|---|
| 三层 deadline 简化 | 当前存在 Rust 外层 `deadline_for_path` + C++ `RequestScheduler` 内层 + 前端 `kFrontendTimeoutMs` 三层超时,语义重叠、难以推理。提案:统一为单一权威源,Rust 仅作兜底 | [maintenance.md](./maintenance.md) |
| 接入 Windows SMTC | 当前 OS 媒体集成仅用托盘 + 全局快捷键,未接入 `SystemMediaTransportControls`(锁屏媒体控制、系统音量混音器元数据)。未来可考虑原生 SMTC 以支持锁屏控制 | [maintenance.md](./maintenance.md) |

---

> 本文所有结论以 [evidence-report.md](./evidence-report.md) 与源码为准。与旧 `Code-Wiki.md` 冲突处(如 `echo_request` vs `native_request`、`playback.rs` 是否存在、`bundle.targets` 值)均以本文为准。
