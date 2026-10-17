# 原生后端

`C_API.cpp` 是导出 C ABI 的组合入口；Qt 的 `NativeGateway` 动态加载 `EchoCAPI.dll`。`CompatApi` 识别路由，`compat_routes/` 将请求分发给业务服务和存储。

公共头文件按职责位于 `include/echo/`：model 管理身份类型，execution 管理操作上下文和请求策略，ports 声明存储边界，application 提供应用用例。服务不得依赖 Qt 页面或控制器。

`EchoCAPI → EchoCore → EchoStorage / EchoApplication / EchoAsync / EchoDiagnostics` 的真实依赖以 `native/CMakeLists.txt` 为准。C ABI 响应必须由对应释放函数回收，请求取消、截止时间和身份版本必须传递到提交点。

见 [后端说明](../../docs/architecture/native-backend.md)、[存储](../../docs/architecture/storage-and-data.md) 和 [构建验证](../../docs/development/build-and-test.md)。
