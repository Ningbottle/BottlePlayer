# 原生后端与请求

`EchoCAPI` 是动态库边界，导出声明位于 `native/include/echo/core/C_API.h`。Qt 的 `NativeGateway` 负责加载库、提交请求、返回线程切换、取消和响应回收。

`CompatApi` 的路由表确定可识别路径；`compat_routes/` 按账户、媒体、歌单等领域分发，业务服务通过 `HttpClient` 访问上游，通过存储接口提交本地状态。`HomeService` 提供推荐歌曲和独立推荐专辑，`RadioService` 提供连续推荐请求。

`OperationPolicies.h` 定义路径匹配、方法、可用性、截止时间、取消、重试及提交策略。精确路径在前，前缀回退在后。身份或写操作不能因网络错误自动重试。调试探针不应出现在 Release 服务入口。

`OperationContext` 贯穿请求、取消和提交。会话与设备使用带版本的身份快照，异步结果只能在版本仍匹配时提交。退出、换号、取消或销毁后到达的结果不得覆盖新状态。

后端编译目标的依赖由 `native/CMakeLists.txt` 定义。叶层 model / execution / ports 不依赖页面，应用用例与持久化边界可以独立测试。测试集中在 `native/tests/`，包括身份提交、生命周期、HTTP 预算、存储及路由契约。

后端保持零 Qt 依赖。仓库检查拒绝 Native 源码中的 Qt include 和显式 Qt 包/链接；CMake 配置结束时再检查实际目标的直接和传递依赖，防止变量、别名或导入库绕过声明检查。守卫负例使用临时 CMake 夹具，不需要 Qt SDK，也不读取用户 profile。
若新增子目录并在其中声明非 `GLOBAL` 导入目标，该目录必须调用 `echo_native_guard_no_qt()`，让守卫在目标可见的作用域检查；根守卫会拒绝无法检查的私有导入依赖。

`tools/build-native.ps1` 是独立构建入口，显式禁用 Qt5/Qt6 包查找。`tools/test.ps1 -NativeOnly` 仅运行 Native 契约；CI 把这两步放在 Qt 安装之前。Qt 应用仍通过 `NativeGateway` 的线程池使用现有同步 C ABI，无需为此次依赖守卫改写服务接口。
