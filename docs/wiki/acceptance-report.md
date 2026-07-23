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

CTest 11/11 全通过。此前报告中出现的 "9/11" 及 "C API 全局状态未重置"/"固定目录残留" 等根因分析**未经证实**,已删除。本轮 review 期间该故障无法复现,当前 11/11 稳定通过;不排除测试间存在尚未定位的状态依赖,但不在本轮文档修正范围内下结论。

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
- **回退路径**:由 `GetDefaultDatabasePath()` 返回(`AppPaths.cpp` L37)。路径解析顺序:① `ECHO_NATIVE_DATA_DIR`;② `%LOCALAPPDATA%\EchoMusicNative\echomusic-native.db`;③ 系统 temp 目录回退。仅 `app_data_dir` 为空时使用。

### 2.5 CONTEXT.md 残留错误(6 处)

| 位置 | 修正前 | 修正后 |
|---|---|---|
| S4 EQ graph build order | `createMediaElementSource` | `captureStream` + `createMediaStreamSource`;声明**从不** `createMediaElementSource` |
| S4 AudioContext lifecycle | 仅 "HMR-safe" | 补充 HMR 三步语义(保留 `<audio>` + 关闭旧 ctx + 重建 graph) |
| S4 Suspended resume | `onSuspendedFail` | `onDegraded`(覆盖 worklet 加载失败等所有降级场景) |
| S5 Record path | `SqlEscape` | `?N` 占位符 + 标识符白名单(无 `SqlEscape` 类) |
| S5 Thread safety | `Database::Execute/ExecuteQuery hold mutex_` | `Submit` 封送到 Actor 线程,队列由 `queue_mutex_` 保护 |
| Key Files 表 | `playback.rs`(不存在)、`g_playback`、`native/playback/*` | 删除不存在的文件行;`g_playback` → `Ctx().api/scheduler/stats/db`(第一轮表格此前误写为 `g_api, g_scheduler, g_stats`,已在 `3b417553` 中再次修正) |

## 3. 已确认良好(无改动)

- `httplib`、`spdlog`、`wil` 在生产源码和构建脚本中没有引用,删除合理(commit `3c0d75c4` / `8e5ce7ad` / `e5404459`)
- Wiki 目录拆分、ADR/RFC、`.gitignore` 调整总体方向正确
- 没有修改播放器、UI 或收藏逻辑
- `git diff --check` 通过,工作树干净

## 4. 提交结构

本次文档修正确认**全部为纯文档提交,不触碰任何生产代码**。实际经过三轮 review,共三个纯文档修正 commit:

| Commit | 范围 | 修正内容 |
|---|---|---|
| `80a69e90` | 第一轮 review 6 个 P1 | FFI 符号、shutdown 时序、ADR-0003 EQ/HMR、Storage 锁名/DB 路径、CONTEXT.md 残留、测试数据 |
| `3b417553` | 第二轮 review 5 个 P1 + 4 个 P2 | `g_*` → `Ctx().*`、IPC 17→19、AudioContext 分析链路、shutdown 图分支、DeepSeek Key、回退路径、CTest 结论、Drawer.vue、Actor 崩溃表述 |
| `d76aed9c` | 第三轮 review 3 个 P2 | stats 命令构成(5 查询 + 1 记录)、验收报告提交记账、ADR-0003 HMR 安全限定为 EQ 链路 |

- 未修改:`.rs`、`.cpp`、`.h`、`.ts`、`.vue` 等生产代码
- 测试基线:vitest 78 文件 / 937 tests、Rust 34、CTest 11/11

## 4.1 第二轮 review 修正(commit `3b417553`)

针对第二轮 review 发现的 5 个 P1 + 4 个 P2 问题,本次纯文档提交修正:

### P1 修正

| # | 问题 | 修正范围 |
|---|---|---|
| 1 | DeepSeek Key 仍写成 localStorage | `CONTEXT.md` L98 改为内存 `ref('')` + 模块加载清理旧 Key;与 `security-and-privacy.md`、`PRIVACY.md` 对齐 |
| 2 | C++ 全局状态使用不存在的 `g_*` | `CONTEXT.md`、`ADR-0001`、`ADR-0002` 全仓改为 `Ctx().api`/`Ctx().scheduler`/`Ctx().stats`/`Ctx().db`/`Ctx().api_rwlock`(EchoContext Meyers singleton) |
| 3 | ADR-0003 AudioContext 硬约束与生产冲突 | 新增"分析链路"章节记录 `audioLevelMonitor.ts` 独立 AudioContext;约束改为允许两条已记录链路(EQ + 分析),新增第三条需 ADR |
| 4 | Tauri IPC 数量 17 → 19 | `architecture.md`、`evidence-report.md`、`tauri-rust.md`、`security-and-privacy.md`、`server-strategy-rfc.md`、`testing-and-release.md` 全仓统一 |
| 5 | Shutdown 图缺失提前返回分支 | `architecture.md` 关闭流程图重写:三个 `alt` 分支(写锁超时/abandoned>0/Phase 2 锁超时);删除不存在的 `Ok` 返回值,标注 `void` |

### P2 修正

| # | 问题 | 修正 |
|---|---|---|
| 1 | 回退路径占位符 `<app_data_dir>/echomusic-native.db` 不准确 | `ADR-0002`、`storage-and-data.md`、本报告改为三阶路径解析:`ECHO_NATIVE_DATA_DIR` → `%LOCALAPPDATA%\EchoMusicNative` → 系统 temp |
| 2 | CTest 清理结论过度 | 本报告改为"故障无法复现,当前 11/11 稳定通过;不排除尚未定位的状态依赖" |
| 3 | `Drawer.vue` 已删除 | `CONTEXT.md` 改为 `EqualizerView.vue` |
| 4 | ADR-0002 Actor 崩溃表述过度 | 改为"lambda 异常被 future 捕获;未捕获线程级异常仍可能触发 `std::terminate`" |

### ADR-0001 附带修正

- `EchoShutdown` 签名从 `void ()` 改为 `int ()`(返回 0 = 安全卸载,非零 = 不安全)

## 4.2 第三轮 review 修正(本次提交)

针对第三轮 review 发现的 3 个 P2 问题,本次纯文档提交修正:

| # | 问题 | 修正 |
|---|---|---|
| 1 | `CONTEXT.md` L91 称 "6 个 `EchoStatsGet*`" | 实际是 5 个 `EchoStatsGet*` + 1 个 `EchoStatsRecordPlay`,共 6 个 Rust stats 命令;已更正措辞 |
| 2 | 验收报告第一轮表格"修正后"仍写 `g_api/g_scheduler/g_stats`;§4 称"单一 commit" | 表格更正为 `Ctx().api/scheduler/stats/db` 并标注 `3b417553` 二次修正;§4 重写为两个 commit(`80a69e90` + `3b417553`)的提交结构 |
| 3 | ADR-0003 "HMR 安全"未限定 EQ 链路 | 正面后果限定为"EQ 链路 HMR 安全";分析链路 HMR 风险(`sharedCtx` 孤儿引用)列入负面后果作为已知开发态残余 |

## 5. 验收结论

**通过**(第三轮)。三轮 review 共修正 6 个 P1(第一轮)+ 5 个 P1 + 4 个 P2(第二轮)+ 3 个 P2(第三轮)= 18 个文档事实错误,测试基线数据不变(937/937、34、11/11)。生产代码与测试无任何改动。可合并。
