# BottleMusic Agent 上下文

## 领域词汇

- BottleMusic：最终原生客户端名称。
- EchoCore：酷狗业务接口、DTO、错误模型。
- EchoStorage：SQLite、migration、cache metadata。
- EchoPlayback：Media Foundation 播放状态机。
- EchoWin32：Win32 + Direct2D + DirectWrite UI。
- EchoImage：WIC decode、disk cache、memory LRU。
- EchoAsync：thread pool、event queue、cancellation。
- EchoDiagnostics：logging、trace、memory snapshot。
- EchoCompatServer：开发期 HTTP 兼容服务。
- Melody 参考界面：用户提供的首页和播放详情页视觉基准。

## 架构词汇

本项目在架构讨论中优先使用：

- Module
- Interface
- Implementation
- Depth
- Seam
- Adapter
- Leverage
- Locality

## 当前事实

- 旧 Electron/Vue 前端和旧 Flutter 跨平台残留已经删除。
- `server/` 子模块保留为 KuGouMusicApi 行为参考和未完成接口迁移来源。
- `native/` 是当前 BottleMusic 原生客户端主工程。
- 新方向以 BottleMusic 文档为准。
- `EchoCompatServer` 是过渡工具，不是最终架构核心。
- 原生 UI 应按 Melody 截图重新设计。

## 用户偏好

- 使用中文沟通。
- 先梳理，再实现。
- 不喜欢跑偏到旧界面。
- 关心内存占用是否真实下降。
- 接受使用 skills，但要求把结论写入文档。
