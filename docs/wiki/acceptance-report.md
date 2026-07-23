# 验收报告

> Code Wiki 重建与仓库规整 · 验收报告
> 基线 commit:`4903ec0b` → 文档修正 commit(本次)
> 核验日期:2026-07-23

## 1. 测试基线(实际运行结果)

| 层 | 命令 | 结果 | 备注 |
|---|---|---|---|
| 前端 | `cd ui && pnpm test -- --run` | **78 文件,937 tests passed** | vitest,jsdom 环境 |
| Rust | `cargo test --manifest-path ui/src-tauri/Cargo.toml --lib --no-default-features -- --test-threads=1` | **34 passed** | `--no-default-features` 避免 tray-icon 崩溃 |
| C++ | `ctest --test-dir native/out/bottlemusic-check --output-on-failure` | **11/11 passed** | clean build 后全通过 |

### 关于 CTest 的说明

CTest 11/11 全通过。此前报告中出现的 "9/11" 及 "C API 全局状态未重置"/"固定目录残留" 等根因分析**未经证实**,已删除。两个测试(`EchoNativeSmokeTests`、`EchoPlayStatsTest`)本身在 `main()` 入口处先执行 `remove_all` 清理临时目录,不存在跨运行状态泄漏。

### 关于前端测试计数的说明

前端测试真实计数为 **78 文件 / 937 tests**。此前报告中出现的 "47 文件 / 417 tests" 数据不准确,已删除。

## 2. 文档修正记录(本轮 review 反馈)

以下 6 项 P1 事实错误已在本次纯文档修正中全部修复:

### 2.1 FFI 符号修正(全仓)

| 文件 | 修正前 | 修正后 |
|---|---|---|
| `docs/wiki/architecture.md` | `Echo_request`、`Echo_free_string` | `EchoHandleRequest`、`EchoFreeString` |
| `docs/wiki/evidence-report.md` L39 | `Echo_request 系列 C ABI` | `EchoHandleRequest 系列 C ABI` |
| `CONTRIBUTING.md` L118 | `Echo_free_string` + "ADR(待补:FFI 边界)" | `EchoFreeString` + 链接到 ADR-0001 |

> 注:`native-cpp.md`、`tauri-rust.md` 中的 `Echo_request` / `echo_request` 出现在**纠正说明**上下文("旧 Wiki 称...不正确"),保留不变。

### 2.2 Shutdown 时序重写(`architecture.md`)

修正前:声称 `EchoShutdown ≤2s`、超时强退。
修正后:按 `backend_api.rs` + `C_API.cpp` 真实实现重写:

- **Phase 0(Rust)**:`try_write()` 轮询 5s 获取写锁,超时跳过 `EchoShutdown`
- **Phase 1(C++)**:`scheduler.Shutdown(3000ms)` — 取消 active tokens,等 ≤3s;`abandoned > 0` → 返回非零,跳过 Phase 2
- **Phase 2(C++)**:`try_lock(api_rwlock)` 轮询 3s,成功则 `api/stats/db.reset()` + `CloseHttpConnectionPool()`;失败返回 1
- **非零返回** → `mem::forget(handle)` 保留 DLL mapping(避免 use-after-unload),**非**强退

### 2.3 ADR-0003 重写(EQ/HMR 生命周期)

修正前:围绕 `createMediaElementSource` 和 `onSuspendedFail` 描述;HMR 是否重建 graph 与 `playback-runtime.md` 矛盾。
修正后:

- 拓扑统一为 `captureStream → createMediaStreamSource → AudioWorkletNode → GainNode → destination`
- 降级回调统一为 `onDegraded`(非 `onSuspendedFail`),恢复回调为 `onRecovered`
- HMR 三步语义:保留 `<audio>` + 关闭旧 AudioContext + 新模块重建 EQ graph
- 备选方案表明确否决 `createMediaElementSource` 路径

### 2.4 Storage 锁名和数据库路径(ADR-0002 + `storage-and-data.md`)

修正前:`Database::mutex_`;声称 `app_data_dir/echomusic-native.db`。
修正后:

- 锁名:`Database::queue_mutex_`(非 `mutex_`);`Execute`/`ExecuteQuery` 通过 `Submit` 封送到 Actor 线程,不直接持锁
- **生产路径**:`<app_data_dir>/bottlemusic.db`(`C_API.cpp` L84/L88,`EchoInitializeWithPathsV2` 传入非空 `app_data_dir`)
- **回退路径**:`<app_data_dir>/echomusic-native.db`(`AppPaths.cpp` L37,`GetDefaultDatabasePath()`,仅 `app_data_dir` 为空时使用)

### 2.5 CONTEXT.md 残留错误(6 处)

| 位置 | 修正前 | 修正后 |
|---|---|---|
| S4 EQ graph build order | `createMediaElementSource` | `captureStream` + `createMediaStreamSource`;声明**从不** `createMediaElementSource` |
| S4 AudioContext lifecycle | 仅 "HMR-safe" | 补充 HMR 三步语义(保留 `<audio>` + 关闭旧 ctx + 重建 graph) |
| S4 Suspended resume | `onSuspendedFail` | `onDegraded`(覆盖 worklet 加载失败等所有降级场景) |
| S5 Record path | `SqlEscape` | `?N` 占位符 + 标识符白名单(无 `SqlEscape` 类) |
| S5 Thread safety | `Database::Execute/ExecuteQuery hold mutex_` | `Submit` 封送到 Actor 线程,队列由 `queue_mutex_` 保护 |
| Key Files 表 | `playback.rs`(不存在)、`g_playback`、`native/playback/*` | 删除不存在的文件行;`g_playback` → `g_api, g_scheduler, g_stats` |

## 3. 已确认良好(无改动)

- `httplib`、`spdlog`、`wil` 在生产源码和构建脚本中没有引用,删除合理(commit `3c0d75c4` / `8e5ce7ad` / `e5404459`)
- Wiki 目录拆分、ADR/RFC、`.gitignore` 调整总体方向正确
- 没有修改播放器、UI 或收藏逻辑
- `git diff --check` 通过,工作树干净

## 4. 提交结构

本次文档修正是**单一纯文档 commit**,不触碰任何生产代码:

- 修改文件:`architecture.md`、`evidence-report.md`、`storage-and-data.md`、`CONTEXT.md`、`CONTRIBUTING.md`、`ADR-0002`、`ADR-0003`
- 新增文件:`acceptance-report.md`(本文件)
- 未修改:`.rs`、`.cpp`、`.h`、`.ts`、`.vue` 等生产代码

## 5. 验收结论

**通过**。所有 6 项 P1 事实错误已修正,测试基线数据已更正为真实值(937/937、34、11/11),未经验证的失败根因已删除。可进入下一轮 review。
