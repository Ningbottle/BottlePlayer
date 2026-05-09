# BottleMusic Agent 指南

本文是 BottleMusic 项目的通用 agent 工作规则。任何自动化编码助手在本仓库内工作时，都应先阅读本文，再阅读 `docs/README.zh-CN.md` 指向的设计文档。

## 项目定位

BottleMusic 是一个面向 Windows 10/11 x64 的原生音乐客户端。目标是用 C++20、Win32、Direct2D、DirectWrite、WIC、Media Foundation 和 SQLite 重写现有 Electron/Vue/Node 形态，降低常驻内存并提升响应速度。

最终客户端不应依赖 Electron、Chromium、WebView2 或 Node。旧 Electron 前端和兼容 HTTP 服务只用于迁移期验证。

## 当前产品目标

- 产品名：BottleMusic。
- 第一阶段视觉基准：用户提供的 Melody 风格首页和播放详情页截图。
- 第一阶段产品页面：首页、播放详情页、搜索结果、歌单详情、设置页。
- 第一阶段后端能力：搜索、歌曲 URL、歌词、歌单歌曲、专辑、歌手。
- 最终播放中整进程内存目标：180MB 以内。

## 模块结构

```text
BottleMusic
├── EchoCore          // 酷狗业务接口、DTO、错误模型
├── EchoStorage       // SQLite、migration、cache metadata
├── EchoPlayback      // Media Foundation 播放状态机
├── EchoWin32         // Win32 + Direct2D + DirectWrite UI
├── EchoImage         // WIC decode + disk cache + memory LRU
├── EchoAsync         // thread pool、event queue、cancellation
├── EchoDiagnostics   // logging、trace、memory snapshot
└── EchoCompatServer  // dev-only compatibility server
```

## 关键约束

- `EchoWin32` 不直接访问酷狗 HTTP、SQLite 或 Media Foundation 对象。
- `EchoCore` 不依赖 UI，不持有 Direct2D、DirectWrite 或窗口句柄。
- `EchoImage` 是图片解码、缩放、磁盘缓存和内存 LRU 的唯一归口。
- `EchoAsync` 是后台任务、取消和 UI 事件投递的唯一归口。
- `EchoDiagnostics` 是日志、trace、内存快照和缓存统计的唯一归口。
- `EchoCompatServer` 只用于开发期兼容旧 Electron 前端，最终默认不启动。

## 工作方式

- 开始任何实现前，先确认改动属于哪个模块。
- 新功能优先走 TDD tracer bullet：一个行为测试，一段最小实现，一次验证。
- 重要 Interface 先用 `design-an-interface` 方法比较多个方案。
- 架构变动先用 `improve-codebase-architecture` 语言说明 Module、Interface、Adapter、Depth、Locality。
- 大计划拆任务时使用 `to-issues` 的纵向切片，不写只有单层工作的横向任务。
- 所有面向用户和项目决策的文档默认使用中文。

## 禁止事项

- 不要把旧 Vue 组件结构当作最终原生 UI 结构。
- 不要为了快速实现把网络、数据库、图片解码放到 UI 线程。
- 不要无上限缓存图片、搜索结果、歌单歌曲或歌词对象。
- 不要新增大体积依赖来绕开首版问题，尤其不要默认引入 FFmpeg、libmpv、Chromium、WebView2。
- 不要让兼容 Electron 的临时接口污染最终 `IBackendFacade`。
- 不要在没有内存影响说明的情况下引入长期驻留对象。

## 必读文档

- `docs/TECH_STACK.zh-CN.md`
- `docs/ARCHITECTURE.zh-CN.md`
- `docs/MODULES.zh-CN.md`
- `docs/UI_REFERENCE_MELODY.zh-CN.md`
- `docs/MEMORY_BUDGET.zh-CN.md`
- `docs/TDD_PLAN.zh-CN.md`
- `docs/SKILL_USAGE.zh-CN.md`
- `docs/IMPLEMENTATION_RULES.zh-CN.md`
