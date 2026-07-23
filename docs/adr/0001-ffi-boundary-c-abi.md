# ADR-0001：FFI 边界采用 C ABI 动态加载

- **状态**：Accepted
- **日期**：2026-07-23
- **决策者**：架构审计（code evidence）
- **关联文档**：[../wiki/architecture.md](../wiki/architecture.md)、[../wiki/tauri-rust.md](../wiki/tauri-rust.md)、[../wiki/native-cpp.md](../wiki/native-cpp.md)、[../wiki/evidence-report.md](../wiki/evidence-report.md)

## 上下文

BottleMusic 采用三层架构：Vue 3 前端 ⇄ Rust FFI 外壳（Tauri 2.0）⇄ C++ 核心（`EchoCAPI.dll`）。Rust 与 C++ 之间需要一条通信通道，用于：

1. 请求分发：前端 → Rust Tauri 命令 → C++ `RequestScheduler` → KuGou API；
2. 统计读写：前端 → Rust → C++ `PlayStatsService` → SQLite；
3. 生命周期：Rust 启动时初始化 C++ 全局状态，关闭时安全释放。

Rust 与 C++ 的 ABI、内存模型、字符串表示、错误处理方式均不同。直接链接 C++ 类（vtable、STL、异常）会引入跨语言 UB 风险，且 C++ 编译器版本绑定过紧。

## 决策

**Rust 与 C++ 之间仅通过 `EchoCAPI.dll` 导出的 C ABI 符号通信。**

### 导出符号表（代码核验）

| 符号 | 签名 | 职责 |
|---|---|---|
| `EchoInitializeWithPathsV2` | `int (const char* app_data_dir)` | 初始化 `EchoContext` Meyers singleton 的成员（`Ctx().api` / `Ctx().scheduler` / `Ctx().stats` / `Ctx().db`），传入数据目录 |
| `EchoHandleRequest` | `void (const char* method, const char* path, const char* query_json, const char* headers_json, const char* body, char** out_response)` | 主请求入口，`out_response` 由 C++ 分配 |
| `EchoStatsRecordPlay` | 统计写入 | 记录一次播放 |
| `EchoStatsGetSummary` / `GetTop` / `GetTimeline` / `GetRecent` / `GetRecommendations` | 统计查询（5 个） | 返回 JSON 字符串 |
| `EchoFreeString` | `void (char*)` | 释放 C++ 分配的字符串 |
| `EchoShutdown` | `int ()` | 有界关闭：返回 0 = 可安全卸载 DLL；非零 = 仍有 detached worker 或锁持有者，`Ctx().api`/`Ctx().stats`/`Ctx().db` 未重置 |

证据：[native/core/C_API.cpp](../../native/core/C_API.cpp) 定义导出；[ui/src-tauri/src/backend_api.rs](../../ui/src-tauri/src/backend_api.rs) 通过 `libloading::Library::get` 动态加载。

### 加载与生命周期

- Rust 端 `CApiHandle` 持有 `libloading::Library` + 函数指针，用 `RwLock` 保护；
- C++ 端使用 Meyers singleton `EchoContext` + `shared_mutex` 保证初始化与关闭的线程安全；
- 字符串所有权：C++ 通过 `EchoHandleRequest` 的 `out_response` 分配，Rust 读取后调用 `EchoFreeString` 释放，避免跨语言 `free` 不匹配。

### 硬约束

1. **不得**在 Rust 端直接链接 C++ 类或 STL 类型；
2. **不得**在 C++ 导出符号中暴露 STL 容器（`std::string` / `std::vector`）或异常；
3. **所有**跨边界字符串由 C++ 分配、由 `EchoFreeString` 释放。

## 后果

### 正面

- **语言隔离**：Rust 与 C++ 编译器版本解耦，可独立升级；
- **ABI 稳定**：C ABI 是最稳定的跨语言契约，符号表变更需显式同步两端；
- **DLL 可热替换**：理论上可替换 `EchoCAPI.dll` 而不动 Rust/前端（实践中未使用，但保留了可能性）；
- **崩溃隔离**：C++ 崩溃不会直接污染 Rust 栈（虽然进程级崩溃仍会退出）。

### 负面

- **手动内存管理**：`EchoFreeString` 容易遗漏，导致 DLL 堆内存泄漏；Rust 端需用 `Drop` 包裹；
- **无类型安全**：C ABI 只有 `const char*`，JSON 序列化/反序列化在两端重复；
- **错误传递粗糙**：C ABI 无异常，错误只能通过返回码或 JSON 字段表达；
- **测试约束**：`cargo test --lib` 必须带 `--no-default-features`，因为 `libloading` 在测试环境加载 DLL 时 tray-icon 会触发 `STATUS_ENTRYPOINT_NOT_FOUND`（见 [../wiki/maintenance.md](../wiki/maintenance.md)）。

## 备选方案

| 方案 | 否决理由 |
|---|---|
| Rust 直接链接 C++ 静态库 | C++ ABI 不稳定，STL/异常跨语言 UB，编译器版本强绑定 |
| [cxx](https://cxx.rs/) 自动绑定 | 引入额外构建依赖，且当前 C ABI 已足够稳定，迁移成本无收益 |
| IPC（进程间通信） | 延迟与复杂度显著上升，且 C++ 核心需直接访问 SQLite 文件 |

## 遵守方式

- 新增 C++ 导出函数必须：C ABI 签名、字符串走 `char**` out 参数 + `EchoFreeString`、不抛异常；
- Rust 端新增绑定必须：在 `CApiHandle` 注册、用 `RwLock` 保护、在 `Drop` 中释放字符串；
- PR 触碰 FFI 边界时需在描述中声明，并附跨层测试证据。
