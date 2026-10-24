# 总体架构

应用由 QML 界面、Qt C++ 控制器和原生 C++ 后端组成。

```text
desktop/qt/qml → desktop/qt/src 的 QObject 控制器
                         ↓ NativeGateway（动态 C ABI）
native/core/C_API.cpp → CompatApi → compat_routes → 业务服务
                         ↓                      ↓
                   请求执行与诊断              存储
```

QML 负责布局、交互和可视状态，业务状态由对应控制器持有。`Main.qml` 组合页面，`Theme.qml` 统一主题，`ui/` 中的基础设施处理排版、像素对齐和窗口呈现。

主界面只有报刊导航；播放页和窗口内沉浸歌词共用 `MusicController` 的歌曲、进度和播放意图，并使用同一个 `QueuePanel`。Folia 幕布、封面墙及其独立聆听面板已移除，旧档案的 `presentation` 元数据不再决定界面入口。

自选主题色由 `AppearanceController` 保存，`Theme.qml` 按所选色相生成页面、侧栏、卡片、边框和按钮的颜色。浅色与深色各自保持表面的明度层级；按钮底色和文字按对比度成对生成，悬停与按下继续使用同一组颜色。沉浸背景仍取自封面，操作控件随主题色变化。错误、警告等语义色独立于自选色调。

报刊首页的主推信息由 `PaperFeature.qml` 呈现，标题、歌手、专辑和操作各有独立位置；`PaperPage.qml` 持有列表与页面导航。侧栏导航可独立滚动，底部设置入口固定可见，键盘焦点会带动导航滚动。基础图标沿用 Lucide，播放和歌词符号使用项目自有的几何图形；图标按钮通过提示说明用途，聚焦按钮优先处理空格键。

`MusicController` 维护播放意图和队列；`AccountController`、`LibraryController`、`HomeController`、`StatsController` 等维护各自领域的加载状态及过期结果抑制。网络服务策略和持久化约束归原生后端所有。

音频解码、时钟和输出也在 `desktop/qt`，由 `QMediaPlayer`、`MusicController`、`EqualizerController` 和 `PcmOutput` 协作完成。`native` 提供服务请求、执行策略和存储，不持有音频引擎。动态库与 Qt 宿主运行于同一进程，C ABI 是模块接口，不是进程隔离。

## 依赖、状态和线程边界

- `native` 不包含 Qt 头文件或链接 Qt 目标，独立 CMake 配置会检查实际目标依赖；CI 在安装 Qt SDK 前构建并测试后端。
- 控制器持有业务状态。`MusicController` 的歌曲和歌词向量是业务真值；供 QML 读取的 `QVariantList` 是内容变更时生成的只读投影，getter 不遍历整表。
- 队列内容、当前索引、红心模式和请求等待状态分别通知。进度继续使用 `positionChanged`；单纯切歌或等待推荐不应使整张队列重新转换。
- `NativeGateway` 在自己的线程池执行同步 C ABI 请求，再将结果送回控制器所属线程。QML 属性和模型更新仍在 GUI 线程，音频处理不借用该请求线程池。
- `PlaybackStore` 的单一 worker 只接收播放状态的值快照，合并积压并串行写入 `preferences.ini`，不持有控制器或模型。导入、导出及退出使用显式保存屏障；失败恢复和生命周期见 [存储与数据](storage-and-data.md)。

性能改动先用 QML Profiler 或有边界的测量定位。QML 可以保留显示计算、布局和动画；歌词行定位已由 C++ 二分查找，逐字填色使用已缓存的行边界。列表是否改为 `QAbstractListModel`，由规模、增量更新需求和测量结果决定。

Qt 的[性能指南](https://doc.qt.io/qt-6.8/qtquick-performance.html)建议测量热点并异步处理耗时工作；60Hz 一帧约 16ms，不能把每个函数统一限制为 2ms 当作性能验收。跨线程模型更新遵守[模型线程约束](https://doc.qt.io/qt-6.8/qabstractitemmodel.html#thread-safety)。

新增行为应先选择现有所有者。不要让 QML 直接拼接协议请求或写数据库；不要让原生服务依赖页面布局。共享操作策略位于 `native/include/echo/execution/`，Qt 网关使用同一策略。

具体链路见 [播放](playback-runtime.md)、[后端](native-backend.md) 和 [存储](storage-and-data.md)。

## 界面样式

Qt 界面通过 `Theme.qml` 使用语义颜色。`RadixColors.qml` 内置 Radix Colors 3.0.0 的原始 sRGB 浅色与深色色阶，不在运行时下载颜色，也不生成自定义 HSL 色阶。第 1–2 阶用于页面与面板，第 3–5 阶用于控件状态，第 6–8 阶用于边框与焦点，第 9–10 阶用于实心操作，第 11–12 阶用于文字。主题选择同时应用于报刊、播放页和弹出面板；旧的十六进制偏好映射到相近色系，保存值不变。封面图片保留原始颜色。

界面字号统一为 12、14、20、28px，使用字重、间距与颜色区分层级。歌词提供 20px 与 28px 两档，旧偏好按当前档位显示。线条和边框使用 0.5px，边框关闭 `pixelAligned` 取整并启用抗锯齿；嵌套圆角由外层圆角减去内边距计算。普通内容面板保持平面，阴影只用于悬浮面板；控件不做悬停放大或按下缩放，必要的展开和位置过渡使用 250ms ease-out。沉浸歌词不使用模糊，背景采用静态 Radix 深色表面。

12px、14px 的标题、列表与辅助文字使用 Inter 的界面字体链，中文在 Windows 优先使用 Microsoft YaHei UI；20px、28px 的报刊标题保留衬线字体。`displayFont(size)` 处理播放页响应式控件的字体选择。Windows 灰度原生文字保留 Qt 的覆盖率 gamma，避免削弱小字号细笔画；`BOTTLE_TEXT_GAMMA` 仅用于显式诊断对照。

界面文案不以中点分隔信息，常规操作不附加重复说明。导入、重置、付费或外发等操作保留必要的影响说明和确认。
