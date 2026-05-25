# BottleMusic UI (Tauri 2 + Vue 3 + Vanilla CSS)

C++ 后端 + Web 前端的并行实验。**业务逻辑全部走 `EchoCompatServer.exe` (C++)，Tauri 仅作壳**。

> 这是 `tauri-experiment` 分支（git worktree），与主分支 `main` 共享 `.git` 仓库但工作目录独立。
> 主分支的 `native/EchoWin32` D2D 客户端不受影响。

---

## 架构

```
┌────────────────────────────┐
│  Tauri Window (Webview)    │  ← Vue 3 + Vanilla CSS（本目录 src/）
│  ────────────────────────  │
│  fetch / invoke            │
└─────────────┬──────────────┘
              │ HTTP loopback (127.0.0.1:6609)
┌─────────────▼──────────────┐
│  EchoCompatServer.exe      │  ← C++ 二进制，作为 Tauri sidecar 拉起
│  (KuGou API / Storage /    │     源码在 ../native/，复用既有 EchoCore
│   Playback hooks)          │
└────────────────────────────┘
```

## 一次性 setup

```powershell
# pnpm（Node 16+ 自带 corepack）
corepack enable
corepack prepare pnpm@latest --activate

# 装前端依赖（仅首次）
pnpm install
pnpm approve-builds --all     # 允许 esbuild 拉平台二进制
```

## 日常开发

```powershell
# 1) 编 C++ 后端 + 拷贝到 sidecar 位置（仅 native/ 改了才需要重跑）
pnpm backend:build

# 2) 起 Tauri dev 窗口（Vue 热更新 + 自动拉 sidecar）
pnpm tauri dev
#    首跑会编 ~250 个 Rust crate，耗时 5-10 分钟
#    之后 < 30 秒
```

只改了 `native/` 不想全 rebuild？

```powershell
# 在 ../native/out/bottlemusic-check 里 cmake --build 完之后
pnpm backend:sync             # 只拷贝 .exe 到 sidecar 位置
pnpm tauri dev
```

## 目录速览

```
ui/
├── src/                          ← Vue 3 + Vanilla CSS（你照 Music Player.html 抄这里）
│   ├── api/backend.ts            ← 唯一对接后端的入口
│   ├── App.vue                   ← 主组件（首版是联通自检）
│   ├── style.css                 ← Newsprint 颜色 / 字体 token
│   └── main.ts
├── src-tauri/                    ← Rust 壳（基本不用动）
│   ├── src/lib.rs                ← 启动 sidecar + 关窗 kill
│   ├── binaries/                 ← sidecar .exe 放这里（.gitignore）
│   ├── capabilities/default.json ← 允许执行 EchoCompatServer
│   ├── tauri.conf.json           ← 窗口尺寸 + externalBin
│   └── Cargo.toml
├── scripts/
│   ├── build-backend.ps1         ← cmake --build EchoCompatServer + sync
│   └── sync-backend.ps1          ← 只拷 .exe
├── package.json                  ← pnpm scripts: tauri / backend:build / backend:sync
└── pnpm-workspace.yaml           ← onlyBuiltDependencies: [esbuild]
```

## 加新接口的工作流

1. **改 C++**：在 `../native/compat_server/CompatServer.cpp` 加 HTTP endpoint
2. **重 build 后端**：`pnpm backend:build`
3. **加前端调用**：在 `src/api/backend.ts` 加 wrapper
4. **写 Vue 组件**：在 `src/` 引用 `apiGet('/your/endpoint')`

整个回路里 **Rust 一行不用改**。

## 切换回 D2D 主线

```powershell
cd D:\KuGouMusic\EchoMusic-main
# main 分支文件原样，C++ 编 EchoWin32 仍走原 CMake：
cmake --build native\out\bottlemusic-check --target EchoWin32
.\native\out\bottlemusic-check\EchoWin32.exe
```

两边互不干扰。

## 不要了

```powershell
cd D:\KuGouMusic\EchoMusic-main
git worktree remove ..\EchoMusic-tauri
git branch -D tauri-experiment
```
