# BottleMusic

BottleMusic 是面向 Windows 10/11 x64 的音乐客户端。
前端使用 **Tauri 2.0 + Vue 3 + Vanilla CSS**，后端使用 **C++ EchoCompatServer** 作为 HTTP sidecar，两端通过 `127.0.0.1:6609` loopback 通信。

## 技术栈

| 层 | 技术 |
| --- | --- |
| 前端窗口壳 | Tauri 2.0（Rust + WRY WebView2） |
| UI 框架 | Vue 3（Composition API） |
| 样式 | Vanilla CSS（Newsprint 报纸风，无 CSS 框架） |
| 构建 | Vite 6 + pnpm 11 |
| 后端 sidecar | C++ EchoCompatServer（`127.0.0.1:6609`） |
| 业务模块 | EchoCore / EchoStorage / EchoPlayback / EchoImage / EchoAsync / EchoDiagnostics |
| 构建系统 | CMake + vcpkg（MSVC C++20） |

## 目录结构

```
EchoMusic-tauri/ui/     ← Tauri + Vue 3 前端（tauri-experiment 分支 git worktree）
EchoMusic-main/native/  ← C++ 后端（main 分支）
EchoMusic-main/server/  ← KuGouMusicApi 参考实现（接口迁移来源）
```

## 文档

- [`docs/REFERENCE.zh-CN.md`](docs/REFERENCE.zh-CN.md) — 技术栈、架构、迁移注意事项、内存预算、开发约束
- [`docs/WORKLIST.zh-CN.md`](docs/WORKLIST.zh-CN.md) — 长期任务队列、验证命令、历史内存基线

## 快速开始

### 前端开发（Tauri + Vue 3）

```powershell
cd ..\EchoMusic-tauri\ui
pnpm install
pnpm approve-builds --all
pnpm tauri dev       # 首跑编译 Rust crate ~5-10 分钟；之后 < 30 秒
```

### 后端构建（C++ EchoCompatServer）

```powershell
cd EchoMusic-main
pnpm --prefix ..\EchoMusic-tauri\ui backend:build
```

或手动 CMake：

```powershell
# 在 Visual Studio Developer Command Prompt 或 VsDevCmd.bat 初始化的 PowerShell 里
cmake -S native --preset bottlemusic-check
cmake --build native/out/bottlemusic-check --target EchoCompatServer
```

### 原生 C++ 客户端（历史，仅 main 分支）

```powershell
cmake --build native/out/bottlemusic-check --target EchoWin32
.\native\out\bottlemusic-check\EchoWin32.exe
```

## 免责声明

本项目仅用于个人学习和技术研究。音乐数据和版权归原平台及版权方所有，请尊重知识产权并支持正版音乐。
