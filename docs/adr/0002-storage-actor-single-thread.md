# ADR-0002：Storage Actor 单线程访问 SQLite

- **状态**：Accepted
- **日期**：2026-07-23
- **决策者**：架构审计（code evidence）
- **关联文档**：[../wiki/storage-and-data.md](../wiki/storage-and-data.md)、[../wiki/native-cpp.md](../wiki/native-cpp.md)、[../wiki/evidence-report.md](../wiki/evidence-report.md)

## 上下文

BottleMusic 的统计功能（S5）需要持久化播放历史到 SQLite。涉及写入与查询的调用方包括：

1. C++ `PlayStatsService::RecordPlay` —— 每次 `play` 事件写入一行；
2. C++ `PlayStatsService` 的 5 个查询（summary / top / timeline / recent / recommendations）；
3. C++ `Database` 的 KV 存储（`kv_store`）与 API 缓存（`api_cache`）。

`RequestScheduler` 有 4 个 worker 线程并发处理请求，统计查询可能来自不同线程。SQLite 在多线程并发写入时需要严格的锁策略，否则触发 `SQLITE_BUSY` 或数据损坏。

## 决策

**SQLite 仅由 C++ 端 `Database` 类的 Storage Actor 单线程访问。前端与 Rust 不得直连数据库文件。**

### Actor 模型（代码核验）

`Database` 类内部运行一个专用 Actor 线程，所有公共方法通过 `Submit` 模板将操作封送到该线程执行：

```cpp
// 封送到 Actor 线程执行（无返回值）
void Execute(const std::string& sql) {
  Submit([this, sql] { ExecuteLocked(sql); });
}

// 封送到 Actor 线程执行（有返回值，通过 future 同步等待）
std::vector<std::vector<std::string>> ExecuteQueryBound(...) {
  return Submit([this, sql, params] { return ExecuteQueryBoundLocked(sql, params); });
}
```

证据：[native/storage/Database.cpp](../../native/storage/Database.cpp) 中 `Submit` 调用随处可见（`Execute` / `ExecuteQuery` / `SetJson` / `GetJson` / `PutApiCache` / `GetApiCache` / `PruneExpiredApiCache` 等均走 `Submit`）。

### SQL 注入防护

- **参数化查询**：值通过 `?N` 占位符绑定（如 `WHERE key=?1`），不拼接 SQL；
- **标识符白名单**：表名/列名等标识符通过白名单校验，不接受外部输入；
- **不存在 `SqlEscape` 类**（旧文档误称，已修正）。

### 线程安全层次

| 层 | 机制 | 保护对象 |
|---|---|---|
| Actor 线程 | 单线程串行执行 `Submit` 的 lambda | SQLite 句柄 |
| `g_stats` | `shared_lock(g_api_rwlock)` 读 / 独占写 | `PlayStatsService` 指针 |
| `Database::queue_mutex_` | `std::mutex` | Actor 队列与状态(`queue_` / `state_`) |
| SQLite | WAL 模式 + `busy_timeout` | 文件级并发 |

证据：`g_stats` 在 `EchoShutdown` 时于独占生命周期锁下重置；`Database::Submit` 在 `queue_mutex_` 下入队（见 `native/include/echo/storage/Database.h` L67）。注：`Database::Execute`/`ExecuteQuery` 是 public API,内部通过 `Submit` 封送到 Actor 线程,不直接持锁。

### 数据库文件

- **生产路径**：`<app_data_dir>/bottlemusic.db`（当 `EchoInitializeWithPathsV2(app_data_dir)` 传入非空路径时,见 `native/core/C_API.cpp` L84/L88）；
- **回退路径**：`<app_data_dir>/echomusic-native.db`（当 `app_data_dir` 为空时由 `GetDefaultDatabasePath()` 返回,见 `native/storage/AppPaths.cpp` L37）；
- 模式：WAL（Write-Ahead Logging）+ `busy_timeout`；
- 主要表：`play_history_v2`、`kv_store`、`api_cache`。

## 后果

### 正面

- **无并发写入问题**：所有 SQLite 操作串行化，`SQLITE_BUSY` 只可能来自外部进程；
- **单一写入点**：`Database` 是唯一持有 SQLite 句柄的组件，数据流清晰；
- **崩溃隔离**：Actor 线程崩溃不会直接影响 `RequestScheduler` worker；
- **前端/Rust 解耦**：前端与 Rust 完全不感知 SQLite 路径与 schema。

### 负面

- **Actor 成为瓶颈**：高并发查询时所有请求排队，延迟取决于 Actor 处理速度；
- **无法跨进程访问**：若未来需要外部工具读取统计，必须通过 C API，不能直连 db 文件；
- **同步等待**：`ExecuteQuery` 通过 `Submit` 返回值同步等待，调用线程阻塞直到 Actor 处理完成。

## 备选方案

| 方案 | 否决理由 |
|---|---|
| WAL + 多线程并发读写 | 需要在每个调用方管理连接与锁，容易出错；且 `PlayStatsService` 已有 `g_api_rwlock`，再加 SQLite 锁层次复杂 |
| Rust 端 `rusqlite` 直连 | 打破三层架构，Rust 需感知 schema 与迁移，且 C++ 统计逻辑需重复实现 |
| 内存数据库 + 定期持久化 | 崩溃丢数据风险，统计场景要求数据可靠 |

## 遵守方式

- 新增表或字段必须在 `Database` 的初始化逻辑中创建，并走 `Submit`；
- 查询必须用 `?N` 占位符，标识符必须走白名单；
- **禁止**在 Rust 或前端代码中出现 SQLite 文件路径或 `rusqlite` 依赖；
- PR 触碰存储层时需在描述中声明，并附 CTest 证据。
