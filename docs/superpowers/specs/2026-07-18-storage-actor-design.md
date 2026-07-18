# Storage Actor 设计规格 (P1 #1)

> 状态：已批准 + 审查修订
> Revision：r2（2026-07-18）
> 实施分支：`codex/storage-actor`（worktree：`.worktrees/storage-actor`）
> 关联：架构审查 P1 #1（WAL 并发未证明安全）；r1 探索实现因 Close/Submit TOCTOU 废弃，按 TDD 重写

## 1. 问题

`Database` 当前用 `write_mutex_` 串行写、`thread_local` 只读连接（`TlsReads()`）并发读：
- `Close()` 只能关闭当前线程的 TLS 连接，其他线程的连接残留。
- 多线程并发读触发 WAL SIGSEGV（`database_wal_concurrency_test.cpp` 已从 CTest 摘掉）。
- 跨线程 set-then-read 无强一致性（TLS 快照隔离）。

r1 探索实现用双 atomic（`running_`/`accepting_`）+ 锁外检查，引入 **Submit/Close TOCTOU**：任务可排在 `CloseLocked` 之后，在 `db_==nullptr` 上执行。r2 用单锁生命周期状态机消除该问题。

## 2. 目标

单一专用 DB 线程拥有所有 sqlite 连接，所有读写经消息队列串行执行。消除 TLS、消除并发 SIGSEGV、保证 Close/迁移/事务一致性。`Database` **公共 API 不变**（调用方零改动）。

## 3. 架构

### 3.1 Storage Actor

`Database` 内部新增：
- `std::thread actor_`：专用线程，拥有 `db_`（写连接；读复用同一连接串行）。
- `std::queue<std::function<void()>> task_queue_` + `std::mutex queue_mutex_` + `std::condition_variable queue_cv_`。
- **单一生命周期状态**（受 `queue_mutex_` 保护，可用 atomic 镜像仅用于无锁快速路径否定，但 **入队决策必须以持锁状态为准**）：

```text
enum class ActorState { Closed, Starting, Open, Closing, Failed };
```

| 状态 | 含义 |
|------|------|
| Closed | 无线程；可 Open/Start |
| Starting | 线程创建中；Submit 拒绝或等待 Open 完成（实现选：拒绝 `database_not_accepting`） |
| Open | 接受 Submit |
| Closing | 已入队/正在执行关库与排空；**新 Submit 拒绝** |
| Failed | 启动失败；Submit 拒绝；可再次尝试 Open |

### 3.2 Submit（持锁检查 + 入队）

```text
Submit(fn):
  promise = shared_ptr<promise<R>>
  task = [fn = move(fn), promise] { try set_value / set_exception }
  lock(queue_mutex_)
    if state != Open: unlock; throw database_not_accepting
    if on actor thread: unlock; throw actor_reentrancy  // 禁止死锁
    queue.push(move(task))
  unlock; notify
  return future.get()
```

- 队列元素 **按值拥有** callable 与 `shared_ptr<promise>`（禁止捕获栈引用）。
- `path_` 的写入与读取仅在 actor 线程（Open 通过 Submit/Start 把 path 按值传入任务），调用线程不直接写 `path_`。

### 3.3 Close 协议（同锁切换 Closing）

```text
Close():
  lock(queue_mutex_)
    if state is Closed or Closing: unlock; return  // 幂等
    if state is Failed: join if needed; state=Closed; unlock; return
    state = Closing
    // 可选：入队 CloseLocked 作为最后任务，或在排空后由 Stop 路径执行
    queue.push(CloseLocked + complete close_promise)
    // 不在此处设 running=false 直到排空
  unlock; notify
  wait close_promise
  lock; running=false; notify; unlock
  join actor
  lock; state=Closed; unlock
```

**关键不变式：**
1. 一旦 `state == Closing`，**任何新 Submit 在持锁检查时失败**，不能再入队到 `CloseLocked` 之后。
2. Actor 在 `Closing` 期间 **排空队列**（含 CloseLocked 及此前已入队任务），再退出循环。
3. CloseLocked 之后队列中不应再有写任务（由 1 保证）。

### 3.4 Open / Initialize

- `Open(path)`：若 Closed，在锁内 `state=Starting`，创建线程（异常 → `Failed` 并恢复 Closed，**禁止**留下 running 无线程）；成功后 `state=Open`；`Submit`/`直接入队` 执行 `OpenLocked(path 按值)`。
- `Initialize()`：`Submit` → `InitializeSchema()`。
- 线程启动顺序：先构造 `std::thread`（或 try），**成功后再** 将 state 设为 Open；若构造抛异常，state=Failed/Closed，不接受 Submit。

### 3.5 Actor 循环

```text
ActorLoop():
  while true:
    lock
      wait until !queue.empty() OR state is Closing/Failed and drain done OR stop
      if stop && queue.empty(): unlock; return
      task = pop
    unlock
    task()
```

### 3.6 移除 TLS

删除 `TlsReadConnections` / `TlsReads()` / `ReadDb()` 的 TLS。读改为 Submit 到 actor。删除 `write_mutex_`。

### 3.7 Fallback（无 sqlite）

fallback 也走 actor；`fallback_` 由 actor 独占。

## 4. 测试（必须 RED 先于实现）

新建 `native/tests/database_actor_lifecycle_test.cpp` 并注册 CTest：
1. **Submit 与 Close 并发**：多线程写 + 一线程 Close，不崩溃、无永久阻塞；Close 后新调用抛 `database_not_accepting` 或安全失败。
2. **Close 后禁止新任务**：Close 完成后 SetJson/GetJson 不触碰已关连接。
3. **Open/Close 循环 100 次**。
4. **多线程 1,000 次读写**：交叉 SetJson/GetJson，最终一致（actor 串行化）。
5. **Close 幂等**：连续 Close 不崩溃。

恢复注册 `database_wal_concurrency_test.cpp`（或并入 actor 压力测试）；在 actor 下多线程提交应不再 SIGSEGV。

现有 `basic_contract_tests`、`EchoPlayStatsTest` 等不回退。

## 5. 非目标

- 不改 `Database` 公共 API 签名。
- 不加连接池。
- 不改 SQL/schema。
- 不改调用方。
- **不做** 播放状态机（P1 #4）——本分支仅 Storage Actor。

## 6. 风险与缓解

- **死锁**：actor 不回调持调用方锁的代码；Submit 检测 reentrancy。
- **性能**：串行化可接受（settings/session/stats/cache 低负载）。
- **异常安全启动**：thread 构造失败不得留下 Open/accepting。
- **r1 作废**：worktree 中若残留 r1 实现，实现前 **恢复 HEAD 基线**，禁止“在不安全实现上打补丁当 TDD”。
