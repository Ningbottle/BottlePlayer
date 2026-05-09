# BottleMusic 架构设计

## 总体结构

```text
BottleMusic
├── EchoCore
├── EchoStorage
├── EchoPlayback
├── EchoWin32
├── EchoImage
├── EchoAsync
├── EchoDiagnostics
└── EchoCompatServer
```

## 进程模型

最终产品默认是单进程：

```text
BottleMusic.exe
  ├─ EchoWin32
  ├─ EchoCore
  ├─ EchoStorage
  ├─ EchoPlayback
  ├─ EchoImage
  ├─ EchoAsync
  └─ EchoDiagnostics
```

`EchoCompatServer` 是开发期工具，用于让旧 Electron 前端继续连接 `127.0.0.1:6609` 做接口对照。正式客户端默认不启动它。

## 依赖方向

推荐依赖方向：

```text
EchoWin32
  ├─ EchoCore
  ├─ EchoPlayback
  ├─ EchoImage
  ├─ EchoAsync
  └─ EchoDiagnostics

EchoCore
  ├─ EchoStorage
  ├─ EchoAsync
  └─ EchoDiagnostics

EchoPlayback
  ├─ EchoAsync
  └─ EchoDiagnostics

EchoImage
  ├─ EchoStorage
  ├─ EchoAsync
  └─ EchoDiagnostics

EchoCompatServer
  ├─ EchoCore
  └─ EchoDiagnostics
```

禁止反向依赖：

- `EchoCore` 不依赖 `EchoWin32`。
- `EchoStorage` 不依赖 `EchoCore` 的业务流程，只提供存储接口和仓储实现。
- `EchoPlayback` 不依赖 UI 控件或页面状态。
- `EchoImage` 不依赖具体页面。
- `EchoDiagnostics` 不依赖业务模块。

## UI 到后端的调用模型

`EchoWin32` 只通过 `IBackendFacade` 调用核心能力：

```cpp
class IBackendFacade {
public:
    virtual Task<DeviceInfo> EnsureDeviceReady() = 0;
    virtual Task<SearchResultPage> SearchSongs(SearchQuery query) = 0;
    virtual Task<PlaylistTrackPage> GetPlaylistTracks(PlaylistTrackQuery query) = 0;
    virtual Task<SongUrlResult> ResolveSongUrl(SongResolveQuery query) = 0;
    virtual Task<LyricDocument> GetLyrics(LyricQuery query) = 0;
};
```

播放控制通过单独的播放 Interface：

```cpp
class IPlaybackController {
public:
    virtual void Play(PlaybackRequest request) = 0;
    virtual void Pause() = 0;
    virtual void Resume() = 0;
    virtual void Stop() = 0;
    virtual void Seek(Duration position) = 0;
    virtual PlaybackState Snapshot() const = 0;
};
```

UI 接收后端事件：

```cpp
class IBackendEventSink {
public:
    virtual void OnBackendEvent(BackendEvent event) = 0;
};
```

事件包括 API 完成、登录状态变化、播放状态变化、进度变化、输出设备变化、网络错误和 token 过期。

## 兼容服务定位

`EchoCompatServer` 只负责 HTTP 形态兼容：

- 复刻旧前端当前使用的路径和响应形状。
- 用于对照 Node `KuGouMusicApi`。
- 用于旧 Electron 前端过渡期联调。
- 不作为最终 UI 的内部通信方式。

所有最终客户端能力都应通过 typed Interface 暴露，而不是让 Win32 UI 调本地 HTTP。

## 架构原则

- 小 Interface，深 Implementation。
- 复杂性集中在模块内部，不扩散到调用方。
- UI 只持有当前页面 ViewModel 和可见区域数据。
- 后端分页接口不把历史结果全部留在内存。
- 图片解码、网络响应、播放状态都要有归口和预算。
