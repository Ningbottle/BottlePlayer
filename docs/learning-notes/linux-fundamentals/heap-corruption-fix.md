# 关闭时 HEAP_CORRUPTION 修复记录

## 现象

- Windows + MSVC Debug CRT 下，关闭 `KuGouConceptPC.exe` 时弹出：
  - `HEAP CORRUPTION DETECTED`
  - `CRT detected that the application wrote to memory after end of heap buffer`
- 问题在退出阶段出现，正常浏览页面时不一定立刻复现。

## 这次确认的高风险根因

### 1. 播放器把临时缓冲区地址交给了 BASS

原来的 `PlaybackService::startResolvedUrl()` 直接这样调用：

- `BASS_StreamCreateURL(url.toUtf8().constData(), ...)`
- `BASS_StreamCreateFile(..., reinterpret_cast<const void*>(url.utf16()), ...)`

这两个地址都依赖局部临时对象或局部参数的生命周期。函数返回后，如果 BASS 仍然异步使用这些指针，就可能读取到已经失效的内存，最终在关闭或释放阶段表现为 heap corruption。

### 2. 关闭时异步回调仍可能命中已释放的 PlaybackService

`playCurrentSelection()` 里把 `this` 直接捕获进了播放地址解析回调。应用关闭时，如果对象已经析构，但网络回调稍后才返回，就可能继续访问已经释放的成员，形成 use-after-free。

### 3. 数据库连接释放顺序不安全

原来的 `DatabaseManager::~DatabaseManager()` 直接 `removeDatabase()`，但 `PlaybackRepository` 等对象仍可能还活着，或者仍然持有 `QSqlQuery/QSqlDatabase` 的连接句柄。Qt 对这种场景非常敏感，退出阶段容易出现未定义行为。

## 实际修复内容

### PlaybackService

- 新增持久化成员：
  - `m_streamUrlUtf8`
  - `m_streamFilePath`
- 在建流前先把 URL 或本地路径拷贝到成员里，再把成员缓冲区地址传给 BASS。
- 在 `clearStream()` 中，等流释放后再清空这些缓冲区。
- 析构时：
  - 先标记 `m_shuttingDown = true`
  - 再递增 `m_requestSerial`
  - 然后清流并 `BASS_Free()`
- 播放地址解析回调用 `QPointer<PlaybackService>` 做生命周期保护，避免关闭期间命中悬空对象。

### DatabaseManager

- 新增 `shutdown()`，显式关闭并移除连接。
- 先通过局部 `QSqlDatabase` 句柄执行 `close()`，等局部句柄离开作用域后，再 `removeDatabase()`。
- 析构函数改为调用 `shutdown()`，避免散落重复逻辑。

### AppController

- 取消完全依赖 `QObject` 默认子对象析构顺序。
- 改成在 `AppController::~AppController()` 里按安全顺序手动删除对象：
  1. ViewModel
  2. PlaybackService / CatalogService
  3. Repository
  4. Session / ApiClient
  5. DatabaseManager
  6. SettingsStore
- 在删除 `DatabaseManager` 之前先显式执行 `shutdown()`，确保 repository 已经先释放完。

## 本次涉及文件

- `src/services/PlaybackService.h`
- `src/services/PlaybackService.cpp`
- `src/storage/DatabaseManager.h`
- `src/storage/DatabaseManager.cpp`
- `src/app/AppController.cpp`

## 验证结果

### 编译

- `cmake --build --preset build-debug --parallel`
- 结果：通过

### 退出验证

- 启动程序
- 等待主窗口稳定
- 调用主窗口关闭
- 结果：`EXIT:0`

本轮验证里没有再复现关闭时的 heap corruption 弹窗。

## 后续经验

- 任何传给原生库的字符串指针，都不要直接来自临时 `QByteArray/QString`。
- 任何异步回调，只要捕获了 `this`，都要考虑对象关闭时的生命周期保护。
- Qt 的 `QSqlDatabase::removeDatabase()` 必须在所有相关 query / database 句柄都已经销毁后再调用。
- 对象较多时，退出顺序不要完全赌在 `QObject` 默认析构链上，关键资源最好手动收口。
