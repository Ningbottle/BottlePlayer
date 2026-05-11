# BottleMusic TDD 计划

## 原则

BottleMusic 使用 tracer bullet 方式推进，不做一次性大迁移。

每次只验证一个用户可观察行为：

```text
RED: 写一个行为测试，确认当前失败
GREEN: 写最小实现，让测试通过
REFACTOR: 保持测试通过，整理模块和命名
```

测试通过 public Interface 验证行为，不测试私有函数和内部调用顺序。

## 首批 public Interface

- `IBackendFacade`
- `IPlaybackController`
- `IImageLoader`
- `IImageCache`
- `IEventQueue`
- `IMemorySnapshotProvider`

## 首批 tracer bullet

1. 设备准备
   - 行为：首次启动能生成并保存设备信息。
   - Interface：`IBackendFacade::EnsureDeviceReady`。

2. 搜索歌曲
   - 行为：输入关键词能返回歌曲列表。
   - Interface：`IBackendFacade::SearchSongs`。

3. 解析歌曲 URL
   - 行为：给定歌曲 hash 能返回可播放 URL 或明确错误。
   - Interface：`IBackendFacade::ResolveSongUrl`。

4. 获取歌词
   - 行为：给定歌曲信息能返回 LRC 行和时间戳。
   - Interface：`IBackendFacade::GetLyrics`。

5. 播放状态机
   - 行为：播放命令使状态从 Idle 到 Opening，再到 Playing 或 Failed。
   - Interface：`IPlaybackController`。

6. 图片缓存
   - 行为：加载多张图片后内存 LRU 不超过预算。
   - Interface：`IImageLoader` / `IImageCache`。

7. 首页 ViewModel
   - 行为：首页只持有当前需要绘制的数据。
   - Interface：页面 ViewModel 创建函数。

8. 虚拟列表
   - 行为：一万个列表项只生成可见行绘制数据。
   - Interface：虚拟列表布局模块。

9. 播放详情歌词定位
   - 行为：播放进度变化后当前歌词行正确更新。
   - Interface：歌词 ViewModel。

10. 内存快照
    - 行为：诊断模块能输出 Working Set、Private Bytes 和缓存大小。
    - Interface：`IMemorySnapshotProvider`。

## 测试分层

快速测试：

- DTO 映射。
- Authorization 解析。
- 设备持久化。
- cache metadata。
- 歌词解析。
- 播放状态机。
- 虚拟列表布局。

集成测试：

- C++ compat route 与旧 Node fixture 对照。
- 搜索到播放 URL 的完整路径。
- 图片加载、缓存、淘汰路径。
- SQLite migration。

手动或慢速测试：

- 真实 Media Foundation 播放。
- 连续播放 4 小时。
- 连续切歌 100 次。
- 大歌单滚动。
- UI 截图和视觉对齐。

## 当前稳定验证命令

在 PowerShell 中建议用 `cmd.exe /d /s /c` 包住 Visual Studio Developer Command，避免 `VsDevCmd.bat` 初始化的 MSVC 环境丢失。

Debug 快速验证：

```powershell
cmd.exe /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && "D:\QT\Tools\CMake_64\bin\cmake.exe" -S native --preset bottlemusic-check && "D:\QT\Tools\CMake_64\bin\cmake.exe" --build native\out\bottlemusic-check --target EchoNativeSmokeTests EchoWin32 EchoCompatServer && "D:\QT\Tools\CMake_64\bin\ctest.exe" --test-dir native\out\bottlemusic-check --output-on-failure --timeout 30'
```

Release 验证：

```powershell
cmd.exe /d /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && "D:\QT\Tools\CMake_64\bin\cmake.exe" -S native --preset bottlemusic-release && "D:\QT\Tools\CMake_64\bin\cmake.exe" --build native\out\bottlemusic-release --target EchoNativeSmokeTests EchoWin32 EchoCompatServer && "D:\QT\Tools\CMake_64\bin\ctest.exe" --test-dir native\out\bottlemusic-release --output-on-failure --timeout 30'
```

## 禁止测试方式

- 不要测试私有函数。
- 不要为内部类之间的调用顺序写脆弱 mock。
- 不要一次性写一整批未来测试。
- 不要把旧 Electron 响应的 volatile 字段做字节级比较。
