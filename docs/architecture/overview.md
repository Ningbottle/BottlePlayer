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

`MusicController` 维护播放意图和队列；`AccountController`、`LibraryController`、`HomeController`、`StatsController` 等维护各自领域的加载状态及过期结果抑制。网络服务策略和持久化约束归原生后端所有。

新增行为应先选择现有所有者。不要让 QML 直接拼接协议请求或写数据库；不要让原生服务依赖页面布局。共享操作策略位于 `native/include/echo/execution/`，Qt 网关使用同一策略。

具体链路见 [播放](playback-runtime.md)、[后端](native-backend.md) 和 [存储](storage-and-data.md)。
