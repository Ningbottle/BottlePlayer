# BottleMusic

BottleMusic 是一个面向 Windows 的原生音乐播放器重构项目，目标是把现有 Electron/Vue/Node 版本逐步迁移到 C++20、Win32、Direct2D、DirectWrite 和 Media Foundation 架构下，降低常驻内存并保留完整音乐客户端体验。

## 当前状态

- 已保留原 Electron/Vue 前端作为接口和体验参考。
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

原生工程位于 `native/`：

```powershell
cmake --preset bottlemusic-check
cmake --build native/out/bottlemusic-check --config Debug
ctest --test-dir native/out/bottlemusic-check --output-on-failure
```

如果本机没有配置 preset，可使用本地安装的 CMake/MSVC 生成 `native/out/bottlemusic-check`。

## 上游参考

- [KuGouMusicApi](https://github.com/MakcRe/KuGouMusicApi)
- 原 EchoMusic Electron/Vue 实现

## 免责声明

本项目仅用于个人学习和技术研究。音乐数据和版权归原平台及版权方所有，请尊重知识产权并支持正版音乐。
