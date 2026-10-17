# BottleMusic Qt

面向 Windows 10/11 x64 的桌面音乐播放器。界面使用 Qt Quick / QML，播放和应用逻辑使用 C++20，原生后端通过 `EchoCAPI` 提供服务。

[English](README.en.md) · [开发文档](docs/README.md) · [贡献指南](CONTRIBUTING.md) · [隐私说明](PRIVACY.md) · [安全政策](SECURITY.md)

当前是 Qt 开发预览版。仓库默认源码、构建和文档统一维护 Qt / C++ / QML；已有发布页中的文件不能据此视为 Qt 稳定版。

## 主要功能

- 搜索、歌曲队列、音质切换、循环播放、歌词跟随与定位。
- 每日歌曲、推荐专辑、歌单与红心连续推荐播放。
- 扫码登录、个人歌单、本地收藏、历史和聆听统计。
- 音频输出设备选择、PCM 输出与均衡器。
- 报刊、幕布和封面墙外观，窗口内沉浸歌词、可调侧栏。
- 设置和统计档案导入导出，以及用户主动触发的 AI 分析。

实现状态和人工验收边界见 [当前状态](docs/development/status.md)。

## 从源码构建

需要 MSVC x64、CMake 3.24+、Ninja、Qt 6.8.3 和 vcpkg 依赖。Qt 需要 Quick、QuickControls2、Concurrent、Multimedia、Network、Svg、ShaderTools、Sql、Test，以及 Quick/Gui private headers。

```powershell
& "$env:VCPKG_ROOT/vcpkg.exe" install --triplet=x64-windows --x-manifest-root=native --x-install-root=native/vcpkg_installed
./tools/build.ps1 -QtRoot C:/Qt/6.8.3/msvc2022_64
./tools/test.ps1 -QtRoot C:/Qt/6.8.3/msvc2022_64
```

应用输出在 `output/qt-build/BottleMusicQt.exe`。构建输出尚需部署 Qt 运行库后才能独立分发；完整步骤见 [构建与验证](docs/development/build-and-test.md)。字体、图标和许可随源码维护。

## 源码位置

| 目录 | 职责 |
| --- | --- |
| `desktop/qt/qml/` | 页面、组件、主题和交互 |
| `desktop/qt/src/` | Qt 控制器、媒体输出、平台能力 |
| `native/` | C ABI、业务服务、请求执行、存储和诊断 |
| `tools/` | 构建、验证、打包和仓库检查 |
| `docs/` | 当前架构、开发和数据边界 |

许可证见 [LICENSE](LICENSE)，第三方素材和字体归属见 [第三方许可](desktop/qt/licenses/THIRD_PARTY.md)。本项目为非官方客户端，不代表第三方服务或内容权利人；代码许可不构成对服务或音乐内容的授权。
