# BottleMusic Code Wiki

> 本目录是 BottleMusic 项目的权威代码文档,基于 `22ba7951` 基线 commit 重建。
> 所有结论以 [evidence-report.md](./evidence-report.md) 为事实来源,旧 `Code-Wiki.md`(根目录)已废弃。
> 文档使用仓库相对链接,不依赖固定行号,优先引用文件和类/函数名。

## 文档索引

| 文档 | 内容 |
|---|---|
| [architecture.md](./architecture.md) | 三层架构总览、FFI 边界、启动流程 |
| [frontend.md](./frontend.md) | Vue 3 前端:状态管理、路由、组件层级 |
| [tauri-rust.md](./tauri-rust.md) | Rust FFI 外壳:Tauri 命令、DLL 加载、audio_proxy |
| [native-cpp.md](./native-cpp.md) | C++ 核心:CompatApi 路由、HttpClient、Crypto |
| [playback-runtime.md](./playback-runtime.md) | 播放运行时:Html5Backend、EQ graph、Orchestrator |
| [storage-and-data.md](./storage-and-data.md) | 存储与数据:SQLite WAL、Actor、Stats、DPAPI |
| [testing-and-release.md](./testing-and-release.md) | 测试体系、CI/CD、Release 流程 |
| [security-and-privacy.md](./security-and-privacy.md) | 安全设计、CSP、SSRF、隐私数据处理 |
| [maintenance.md](./maintenance.md) | 维护:已知问题、模糊点、清理候选、未来提案 |
| [evidence-report.md](./evidence-report.md) | 事实核验报告(本 Wiki 的事实来源) |

## 阅读路径

### 新人 30 分钟导读

1. 本 README + [architecture.md](./architecture.md)(10 分钟)— 建立三层心智模型
2. [playback-runtime.md](./playback-runtime.md)(5 分钟)— 理解播放核心
3. [maintenance.md § 已知问题](./maintenance.md#已知问题)(5 分钟)— 避免踩坑
4. [testing-and-release.md](./testing-and-release.md)(10 分钟)— 跑通测试

### 改 Bug 路径

1. 用 [architecture.md § 启动流程](./architecture.md#启动流程) 判断问题层
2. 用 [playback-runtime.md](./playback-runtime.md) 找播放相关代码
3. 用 [testing-and-release.md](./testing-and-release.md) 跑对应层测试
4. 用 [maintenance.md](./maintenance.md) 排查已知陷阱

### 加新功能路径

1. 读 [architecture.md](./architecture.md) 确认属于哪层
2. 读对应层文档(frontend/tauri-rust/native-cpp)
3. 读 [security-and-privacy.md](./security-and-privacy.md) 确认安全约束
4. 读 [storage-and-data.md](./storage-and-data.md) 确认数据持久化方式

## 文档规范

- **语言**:中文,关键英文符号保留(类名、函数名、路径)
- **链接**:仓库相对链接(如 `[lib.rs](../../ui/src-tauri/src/lib.rs)`),不使用 `file:///` 和绝对路径
- **引用**:优先引用文件和类/函数名,不依赖固定行号
- **图表**:使用 Mermaid 描述启动、请求、播放、存储、关闭流程
- **区分**:明确区分**当前实现**、**已知风险**、**未来提案**
- **测试数量**:动态测试数量放在 [evidence-report.md](./evidence-report.md) 和 [testing-and-release.md](./testing-and-release.md) 的生成报告中,不写成长期架构事实

## 与旧文档的关系

| 文档 | 状态 |
|---|---|
| 根目录 `Code-Wiki.md`(2317 行,2026-07-23 上午生成) | **已废弃**,事实错误见 [evidence-report.md § 7.7](./evidence-report.md#77-旧-code-wikimd-的其它错误) |
| `CONTEXT.md` | 仍有效,但部分内容过时(见 [maintenance.md](./maintenance.md)),本轮会同步修正 |
| `README.md` | 仍有效,但 "Media Foundation" 描述过时,本轮会修正 |
| `PRIVACY.md` | 当前有效(2026-07-17 更新) |
| `CHANGELOG.md` | 严重滞后,自动化方案见 [testing-and-release.md](./testing-and-release.md) |

## 修订记录

- 2026-07-23:首次生成(基于 `22ba7951` 基线,由 `codex/wiki-audit` worktree 产出)
