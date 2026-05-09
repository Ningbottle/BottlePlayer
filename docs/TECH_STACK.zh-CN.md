# BottleMusic 技术栈

## 平台

- Windows 10 / Windows 11
- x64
- MSVC
- C++20
- CMake
- vcpkg

## UI

- Win32：窗口、消息循环、输入、DPI、窗口区域。
- Direct2D：形状、背景、卡片、进度条、图片合成。
- DirectWrite：中文和英文文本布局、歌词、列表文本。
- WIC：图片解码、缩放、格式转换。

UI 控件以自绘为主，不引入大型跨平台 UI 框架。首页、播放详情页、搜索页、歌单详情和设置页都应共享同一套布局、输入和绘制基础设施。

## 网络

- WinHTTP 作为默认 HTTP 客户端。
- 酷狗接口适配集中在 `EchoCore`。
- 统一处理超时、重试、错误分类、Header、Authorization 和设备参数。
- 不使用 libcurl 作为首选方案，避免扩大依赖和体积。

## 存储

- SQLite 作为本地数据库。
- schema migration 由 `EchoStorage` 管理。
- 数据包括设备信息、登录态、设置、播放历史、缓存元数据、图片索引。
- 所有缓存必须有 TTL、容量限制或淘汰策略。

## 播放

- Media Foundation 作为首选播放管线。
- 支持 URL 流式播放、暂停、恢复、停止、seek、音量、播放速度。
- 首版不引入 FFmpeg 或 libmpv。
- Media Foundation 不支持的格式先记录兼容性缺口，不用大依赖绕开。

## 图片

- WIC 解码。
- 磁盘缓存保存原始或规范化图片。
- 内存 LRU 保存已解码位图或缩略图。
- 首版内存图片缓存默认 16MB，上限 32MB。

## 异步

- `EchoAsync` 提供 thread pool、event queue、cancellation。
- UI 线程只处理输入、布局和绘制。
- 网络、数据库、图片解码、播放控制都通过后台任务执行。
- 结果通过事件队列投递回 UI 线程。

## 诊断

- `EchoDiagnostics` 负责 logging、trace、memory snapshot、cache stats、request timing。
- 每个可验收功能都应能说明对内存和缓存的影响。

## 轻量依赖建议

- `sqlite3`：本地数据库。
- `nlohmann-json`：JSON 解析和兼容期响应构造。
- `cpp-httplib`：开发期 `EchoCompatServer`。
- `spdlog`：日志。
- `wil`：Win32 和 COM RAII。

依赖必须能解释引入理由、体积影响、替代方案和退出策略。
