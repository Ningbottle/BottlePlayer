# BottleMusic Agent 指南

本文是 BottleMusic 项目的通用 agent 工作规则。任何自动化编码助手在本仓库内工作时，都应先阅读本文，再阅读 `docs/REFERENCE.zh-CN.md`（技术栈、架构、迁移注意事项）和 `docs/WORKLIST.zh-CN.md`（执行队列）。

## 1. 项目定位

BottleMusic 是面向 Windows 10/11 x64 的音乐客户端。前端采用 **Tauri 2.0 + Vue 3 + Vanilla CSS**（`tauri-experiment` 分支），后端沿用 C++20 模块体系，通过 EchoCompatServer HTTP sidecar 连接两端。

- 前端：Tauri 2.0 壳 + Vue 3（Composition API）+ Vanilla CSS（Newsprint 报纸风）
- 后端：C++ EchoCompatServer（sidecar `127.0.0.1:6609`）+ EchoCore/Storage/Playback/Image/Async/Diagnostics
- `server/` 子模块保留为 KuGouMusicApi 行为参考与未完成接口迁移来源
- 播放中整进程内存目标：≤ 220 MB（WebView2 基线约 60-80 MB）
- 视觉方向：Newsprint 报纸风（`Music Player.html` 参考，纸色 `#f1ead8` + 红强调 `#a8311b`）

## 2. 模块结构

```text
前端（tauri-experiment 分支）
├── Tauri 2.0 壳         // 窗口、sidecar 启停、invoke bridge
├── Vue 3 组件树          // 页面、组件、Composition API
├── Vanilla CSS          // Newsprint token、自定义属性
└── src/api/backend.ts   // 唯一 HTTP 调用入口（→ EchoCompatServer）

后端（main 分支，C++）
├── EchoCompatServer  // HTTP sidecar，127.0.0.1:6609
├── EchoCore          // 酷狗业务接口、DTO、错误模型
├── EchoStorage       // SQLite、migration、cache metadata
├── EchoPlayback      // Media Foundation 播放状态机
├── EchoImage         // WIC decode + disk cache + memory LRU
├── EchoAsync         // thread pool、event queue、cancellation
└── EchoDiagnostics   // logging、trace、memory snapshot
```

技术栈、依赖方向、迁移注意事项与内存预算见 `docs/REFERENCE.zh-CN.md`。

## 3. 沟通与文档

- 默认用中文沟通和写项目文档
- 代码标识符使用英文
- 架构与设计基线写入 `docs/REFERENCE.zh-CN.md`
- 执行队列、验证命令、历史实测写入 `docs/WORKLIST.zh-CN.md`
- 技术栈与迁移注意事项写入 `docs/REFERENCE.zh-CN.md`
- 如果用户要求重新梳理，先停手沟通，不继续写代码

### 架构语言

架构讨论统一使用：Module、Interface、Implementation、Depth、Seam、Adapter、Leverage、Locality。

### 任务类型

- HITL：需要用户参与确认（视觉截图、交互取舍、架构决策）
- AFK：agent 可独立完成并验证

任务切片优先纵向：每个任务穿过数据 / Interface / UI 或测试中的完整路径，能独立验收，写清依赖；不写"只做某层"的横向任务，除非是明确的基础设施切片。

## 4. 实现前检查

每次动手前回答这几个问题：

- 这次改动属于哪个 Module？
- 调用方应该看到什么 Interface？
- 是否会增加长期内存占用？
- 是否需要 TDD tracer bullet？
- 是否会影响 Melody UI 参考方向？
- 是否只是 Electron 兼容期能力，还是最终客户端能力？

## 5. 实现规则

### UI

- UI 线程不做网络、SQLite、图片解码或 Media Foundation 调用
- UI 只持有当前页面 ViewModel 与可见区域数据
- 列表都准备虚拟化，尤其歌曲、歌单、搜索结果、队列、评论
- 控件状态变化不能导致布局跳动
- 文本必须适配中文、英文、长歌名和长艺人名

### 图片

- 所有图片加载走 EchoImage
- 不允许页面自己持有无限 Direct2D bitmap
- 图片请求必须可取消
- 不可见列表项的图片应能释放
- 图片缓存有内存与磁盘上限

### 后端

- 酷狗接口细节集中在 EchoCore
- Authorization、dfid、mid、uuid、guid、mac、token 不泄漏到 UI
- 兼容 JSON 与 typed DTO 分开
- 大响应不长期保留原始 JSON
- 分页接口不一次性拉取全部历史

### 播放

- EchoPlayback 只暴露状态与命令
- UI 监听播放状态 snapshot，不直接持有 Media Foundation 对象
- 播放失败必须有明确错误
- 切歌时释放旧播放对象
- 不做整曲解码缓存

### 异步

- 所有后台任务必须可追踪
- 页面销毁或切换时取消无用任务
- 关闭程序时等待或取消后台任务
- 后台错误通过事件队列回到 UI

### 诊断

- 每个阶段保留可读日志
- 内存快照包含进程内存、图片缓存、队列长度、当前页面、播放状态
- 性能问题先诊断再改，不用猜测替代测量

### 兼容服务

- EchoCompatServer 仅开发调试
- 不把兼容 HTTP 当作最终 UI 内部协议
- 未迁移接口返回稳定错误，不伪造成功
- 兼容响应只保证旧前端需要的字段，不扩大成最终模型

### 依赖

- 引入依赖前写清楚理由、内存影响、体积影响、替代方案
- 优先 Windows 原生能力与小依赖
- 首版不默认引入 FFmpeg、libmpv、Chromium、WebView2

## 6. 禁止事项

- 不要把旧 Vue 组件结构当作最终原生 UI 结构
- 不要为了快速实现把网络、数据库、图片解码放到 UI 线程
- 不要无上限缓存图片、搜索结果、歌单歌曲或歌词对象
- 不要新增大体积依赖来绕开首版问题
- 不要让兼容 Electron 的临时接口污染最终 `IBackendFacade`
- 不要在没有内存影响说明的情况下引入长期驻留对象

## 7. TDD 与 Skill 使用

### TDD 原则

- 每次只验证一个用户可观察行为
- RED：写一个失败的行为测试
- GREEN：写最小实现让测试通过
- REFACTOR：在测试通过后整理模块与命名
- 测试通过 public Interface 验证行为，不测私有函数和内部调用顺序
- 不一次性写一整批未来测试
- 不为 volatile 字段做字节级比较

### Skill 调用

| Skill | 用途 |
| --- | --- |
| `tdd` | 新增可观察功能、修 bug、迁移后端接口、实现缓存与内存预算 |
| `improve-codebase-architecture` | 划分模块、发现依赖混乱、设计深 Module、解释 Interface 价值 |
| `design-an-interface` | 设计 `IBackendFacade`、`IPlaybackController`、`IImageLoader`、`IImageCache`、`IEventQueue`、`IMemorySnapshotProvider`、`IPage`、`IRenderContext` 等关键 Interface；至少比较三种方向（最少方法 / 最灵活 / 最常见路径优化） |
| `to-issues` | 把计划拆成可执行任务，包含 Title / Type（HITL or AFK）/ Blocked by / Acceptance criteria |

### Skill 规则

- skills 是流程工具，不替代项目事实；事实以 `docs/`、本文与用户最新指令为准
- skill 结论必须落回 `docs/`
- skill 输出不是已批准实现计划
- 用户说"先不动手"时只沟通和写文档，不修改 C++ 代码

## 8. 长时间任务

- 优先按 `docs/WORKLIST.zh-CN.md` 自动推进
- 上下文压缩时只保留 BottleMusic 相关事实，丢弃与项目无关的 GitHub 凭据、账号登录、网络证书、代理配置等临时排障信息
- 压缩规则与验证命令详见 `docs/WORKLIST.zh-CN.md`
