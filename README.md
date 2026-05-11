# BottleMusic

BottleMusic 是一个面向 Windows 的原生音乐播放器重构项目，目标是把现有 Electron/Vue/Node 版本逐步迁移到 C++20、Win32、Direct2D、DirectWrite 和 Media Foundation 架构下，降低常驻内存并保留完整音乐客户端体验。

## 当前状态

- 已删除原 EchoMusic 的 Electron/Vue 前端代码；当前项目以 BottleMusic 原生客户端为主。
- 已删除旧 Flutter 跨平台桌面工程残留；当前桌面原生客户端只面向 Windows 10/11 x64。
- 已保留 `server/` 子模块作为 KuGouMusicApi 行为参考和未完成接口迁移来源。
- 已新增 `native/` C++ 工程骨架。
- 已实现部分兼容 HTTP API、后端 DTO、SQLite 存储、WinHTTP 请求封装和 Win32 绘制壳。
- 原生 UI 仍在迭代中，当前重点是按参考图修正布局、播放栏、歌词页和响应式行为。

## 技术栈

- C++20
- Win32 API
- Direct2D
- DirectWrite
- WIC
- Media Foundation
- SQLite
- CMake
- vcpkg

## 原生模块规划

```text
EchoCore          酷狗业务接口、DTO、错误模型
EchoStorage       SQLite、migration、cache metadata
EchoPlayback      Media Foundation 播放状态机
EchoWin32         Win32 + Direct2D + DirectWrite UI
EchoImage         WIC decode + disk cache + memory LRU
EchoAsync         thread pool、event queue、cancellation
EchoDiagnostics   logging、trace、memory snapshot
EchoCompatServer  dev-only compatibility server
```

## 文档入口

- [产品愿景](docs/PRODUCT_VISION.zh-CN.md)
- [技术栈](docs/TECH_STACK.zh-CN.md)
- [架构设计](docs/ARCHITECTURE.zh-CN.md)
- [模块说明](docs/MODULES.zh-CN.md)
- [内存预算](docs/MEMORY_BUDGET.zh-CN.md)
- [开发规则](docs/IMPLEMENTATION_RULES.zh-CN.md)
- [TDD 计划](docs/TDD_PLAN.zh-CN.md)
- [技能使用规范](docs/SKILL_USAGE.zh-CN.md)
- [Melody UI 参考](docs/UI_REFERENCE_MELODY.zh-CN.md)

## 本地构建

原生工程位于 `native/`。建议先进入 Visual Studio Developer Command Prompt，或在 PowerShell 中通过 `VsDevCmd.bat` 初始化 MSVC 环境。

```powershell
cmake -S native --preset bottlemusic-check
cmake --build native/out/bottlemusic-check --target EchoNativeSmokeTests EchoWin32 EchoCompatServer
ctest --test-dir native/out/bottlemusic-check --output-on-failure
```

Release 构建：

```powershell
cmake -S native --preset bottlemusic-release
cmake --build native/out/bottlemusic-release --target EchoNativeSmokeTests EchoWin32 EchoCompatServer
ctest --test-dir native/out/bottlemusic-release --output-on-failure
```

启动原生 UI：

```powershell
.\native\out\bottlemusic-check\EchoWin32.exe
```

启动开发期兼容服务：

```powershell
.\native\out\bottlemusic-check\EchoCompatServer.exe --host 127.0.0.1 --port 6609
```

`EchoCompatServer` 只用于开发期验证旧接口形态；最终原生客户端不依赖本地 HTTP 服务。

## 当前已知缺口

- Melody 视觉还需要人工截图确认，尤其是 1600x1060 首页和播放详情页。
- 真实播放的长时间稳定性仍需补充：连续播放 4 小时、连续切歌 100 次。
- 真实酷狗网络接口仍有一部分处在兼容迁移阶段。
- 当前封面/推荐数据仍混有占位内容，最终需要接入真实图片和缓存淘汰验证。
- 安装包、自动更新、签名和正式发布流程尚未开始。
- 不再维护旧 Flutter 的 Linux/macOS/Windows 平台工程。
- 不再维护旧 Electron/Vue 前端；未完成接口以 `server/` 中的 KuGouMusicApi 实现作为参考。

## 上游参考

- [KuGouMusicApi](https://github.com/MakcRe/KuGouMusicApi)
- `server/` 中的 KuGouMusicApi 参考实现

## 免责声明

本项目仅用于个人学习和技术研究。音乐数据和版权归原平台及版权方所有，请尊重知识产权并支持正版音乐。
