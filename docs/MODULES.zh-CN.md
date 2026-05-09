# BottleMusic 模块设计

本文定义 BottleMusic 的 Module、Interface、Adapter、依赖和测试方式。后续代码目录应逐步与这些模块对齐。

## EchoCore

职责：

- 酷狗业务接口适配。
- DTO 和错误模型。
- 设备注册、登录态、搜索、歌曲、歌单、专辑、歌手、歌词等业务能力。
- 同时支持兼容 HTTP 原始 JSON 和最终 Win32 typed DTO。

不负责：

- 不绘制 UI。
- 不直接操作 Media Foundation。
- 不管理图片位图缓存。
- 不创建 Win32 窗口或控件。

主要 Interface：

- `IBackendFacade`
- `IKuGouClient`
- `ISessionRepository`
- `IDeviceRepository`

测试：

- 通过 public Interface 做行为测试。
- 对兼容接口做 JSON path contract 测试。
- 忽略时间戳、签名、临时 URL、随机设备字段。

## EchoStorage

职责：

- SQLite 连接和 schema migration。
- 设备信息、登录态、设置、播放历史、缓存元数据。
- API cache metadata 和图片 cache index。

不负责：

- 不知道 UI 页面。
- 不直接发网络请求。
- 不保存长期大对象到内存。

主要 Interface：

- `IStorage`
- `IMigrationRunner`
- `IKeyValueStore`
- `ICacheMetadataStore`
- `IPlaybackHistoryStore`

测试：

- migration 从空库到最新 schema。
- 设备信息保存和读取。
- cache TTL 和容量淘汰元数据。

## EchoPlayback

职责：

- Media Foundation 播放管线。
- 播放状态机：Idle、Opening、Playing、Paused、Buffering、Stopped、Failed。
- URL 流式播放、暂停、恢复、停止、seek、音量、播放速度。
- 输出设备枚举和切换。

不负责：

- 不解析酷狗业务响应。
- 不持有 UI 控件。
- 不缓存整首解码音频。

主要 Interface：

- `IPlaybackController`
- `IPlaybackEventSink`
- `IAudioDeviceEnumerator`

测试：

- 状态机行为测试。
- 播放命令顺序测试。
- 设备失联策略测试。
- 真实 Media Foundation 集成测试独立于快速单元测试。

## EchoWin32

职责：

- Win32 窗口、消息循环、DPI、输入。
- Direct2D 绘制形状和图片。
- DirectWrite 绘制文本。
- 页面路由和轻量 ViewModel。
- 首页、播放详情页、搜索页、歌单详情、设置页。

不负责：

- 不直接发酷狗 HTTP。
- 不直接访问 SQLite。
- 不长期持有所有歌曲、歌单或图片位图。
- 不直接操作 Media Foundation 对象。

主要 Interface：

- `IAppShell`
- `IPage`
- `IRenderContext`
- `IInputRouter`
- `IViewModelStore`

测试：

- 布局函数可测。
- 虚拟列表可测。
- 关键页面做截图或像素级 smoke 验证。
- UI 线程无阻塞操作检查。

## EchoImage

职责：

- WIC 图片解码。
- 图片缩略图生成。
- 磁盘缓存。
- 内存 LRU。
- 图片请求去重和取消。

不负责：

- 不知道页面业务含义。
- 不保存歌单或歌曲 DTO。
- 不无限保留 Direct2D bitmap。

主要 Interface：

- `IImageLoader`
- `IImageCache`
- `IImageDecodeQueue`

测试：

- 相同 URL 去重。
- LRU 超预算淘汰。
- 解码失败返回占位图。
- 取消不可见图片请求。

## EchoAsync

职责：

- thread pool。
- event queue。
- cancellation token。
- UI 线程投递。
- 后台任务生命周期。

不负责：

- 不包含业务逻辑。
- 不吞掉错误。
- 不直接绘制。

主要 Interface：

- `ITaskScheduler`
- `IEventQueue`
- `ICancellationSource`
- `IUiDispatcher`

测试：

- 任务完成投递到 UI 队列。
- cancellation 可阻止后续回调。
- 关闭时不泄漏后台任务。

## EchoDiagnostics

职责：

- logging。
- trace。
- memory snapshot。
- cache stats。
- request timing。
- playback state dump。

不负责：

- 不改变业务行为。
- 不持有大对象。
- 不依赖 UI 页面。

主要 Interface：

- `ILogger`
- `ITraceRecorder`
- `IMemorySnapshotProvider`
- `IDiagnosticsReporter`

测试：

- snapshot 字段稳定。
- cache stats 汇总正确。
- 日志失败不影响主流程。

## EchoCompatServer

职责：

- 开发期 HTTP 兼容服务。
- 监听 `127.0.0.1:6609`。
- 对旧 Electron/Vue 前端暴露兼容路径。
- 未迁移接口返回稳定 `native_not_implemented`。

不负责：

- 不作为最终客户端内部通信机制。
- 不定义最终 typed DTO。
- 不持有 UI 状态。

主要 Interface：

- `ICompatApi`
- `ICompatRouteRegistry`

测试：

- HTTP route contract。
- Authorization 解析。
- 旧前端已声明路径识别。
