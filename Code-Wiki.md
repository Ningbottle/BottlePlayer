# BottleMusic — Code Wiki

> **版本**:1.0.0 · **平台**:Windows 10/11 x64 · **License**:MIT · **仓库**:[Ningbottle/BottlePlayer](https://github.com/Ningbottle/BottlePlayer)
> **定位**:Windows 上的酷狗概念版(appid=3116,Lite 盐)非官方桌面客户端
> **本 Wiki 生成日期**:2026-07-23 · **基线 git HEAD**:`22ba7951` · **总提交数**:694

> ⚠ **重要声明**(摘自 README):本项目是非官方软件,与酷狗及任何第三方服务方无隶属或代表关系,不声称已获授权。任何公开上架、分发或商业使用前,使用者必须自行取得必要的第三方服务和内容授权。本 Wiki 仅用于个人学习和技术研究。

---

## 目录

- [1. 项目概览](#1-项目概览)
- [2. 整体架构](#2-整体架构)
- [3. 快速上手](#3-快速上手)
- [4. 项目地图](#4-项目地图)
- [5. native/ — C++ 核心层](#5-native--c-核心层)
- [6. ui/ — Vue 3 前端 + Tauri 2.0 Rust 外壳](#6-ui--vue-3-前端--tauri-20-rust-外壳)
- [7. server/ — Node.js KuGou API 代理层](#7-server--nodejs-kugou-api-代理层)
- [8. 跨层数据流](#8-跨层数据流)
- [9. 依赖关系](#9-依赖关系)
- [10. 测试体系](#10-测试体系)
- [11. CI/CD](#11-cicd)
- [12. 项目时间线](#12-项目时间线)
- [13. GitHub 改进历史](#13-github-改进历史)
- [14. 子项目 S1–S5 + 双界面重设计](#14-子项目-s1s5--双界面重设计)
- [15. 领域语言词汇表](#15-领域语言词汇表)
- [16. 安全与隐私](#16-安全与隐私)
- [17. 已知问题与遗留事项](#17-已知问题与遗留事项)
- [18. 模糊点 / 易误解处](#18-模糊点--易误解处)
- [19. 本 Wiki 调用的 Skills 与子代理](#19-本-wiki-调用的-skills-与子代理)
- [20. 自检与歧义声明](#20-自检与歧义声明)

---

## 1. 项目概览

BottleMusic 是基于 **Tauri 2.0 + Vue 3 + C++** 的非官方酷狗概念版 Windows 音乐播放器。功能特性(摘自 [README.md](file:///c:/BottleMusic/README.md)):

| 域 | 特性 |
|---|---|
| **播放** | HTML5 Audio 引擎,播放队列,单曲/列表循环/随机,拖拽进度条,音质切换,切歌立即停旧曲 |
| **均衡器** | 10 频段 Web Audio API(31/62/125/250/500/1K/2K/4K/8K/16K Hz),6 种预设,本地音频代理处理跨域 CDN 媒体,代理不可用时显示降级提示 |
| **双皮肤** | **Aurora** 沉浸式粒子动效 + 渐变光晕 + 全屏歌词沉浸;**Newsprint** 报纸风排版 + 极简编辑风 + 暗色模式 |
| **歌词** | 自动跟随播放进度(3 秒空闲后恢复),全屏沉浸,点击歌词行跳转播放进度 |
| **统计** | 播放历史仪表盘(总播放次数/实际听歌时长/完成率/独立歌曲数/歌手数),Top 榜单(按 album_id 分组),每日播放次数时间线,可选 DeepSeek AI 听歌分析(API Key 仅当前页面会话使用) |
| **搜索** | 歌曲/歌手/专辑搜索,结果直接播放或加入队列 |
| **歌单** | 加载用户歌单(收藏/自建),点击整列表为播放队列 |
| **登录** | 扫码登录,用户信息/VIP 状态显示 |
| **自动更新** | 内置 Tauri 更新器,启动时检查 GitHub Releases |

### 技术栈

| 层 | 技术 |
|---|---|
| 前端 | Vue 3, Vite 6, Vanilla CSS, GSAP, Web Audio API, vue-router |
| Rust FFI | Tauri 2.0, reqwest, tokio, libloading, sysinfo |
| C++ 核心 | MSVC C++20, WinHTTP, Media Foundation, SQLite (WAL) |
| CI/CD | GitHub Actions, CMake, vcpkg, CTest, Vitest, Cargo |
| 参考后端 | Node.js + Express(酷狗 API 代理,非生产运行时) |

### 致谢

后端接口实现参照 [MakcRe/KuGouMusicApi](https://github.com/MakcRe/KuGouMusicApi);项目基线为酷狗概念版(appid=3116,Lite 盐)。

---

## 2. 整体架构

BottleMusic 采用**三层架构 + 一份参考实现**:

```
┌────────────────────────────────────────────────────────────────┐
│  Vue 3 Frontend (ui/src/)                                       │
│  ├─ playerStore / playbackOrchestrator / playSessionTracker    │
│  ├─ webAudioEq / eqWorkletProcessor (10-band Web Audio EQ)     │
│  ├─ backend (Tauri invoke 包装 + 熔断 + 14s 超时)               │
│  ├─ themeStore (Aurora + Newsprint 双皮肤)                      │
│  ├─ html5Backend (唯一生产播放后端 + 事件源)                     │
│  ├─ audioProxy (调用 Tauri audio_proxy_url 绕开 CORS)           │
│  └─ favoriteStore / homeFeedStore / fmSession / ...            │
└────────────────┬───────────────────────────────────────────────┘
                 │ Tauri IPC (invoke_handler)
┌────────────────▼───────────────────────────────────────────────┐
│  Rust FFI (ui/src-tauri/src/)                                   │
│  ├─ lib.rs           — Tauri app setup, invoke_handler 注册     │
│  ├─ backend_api.rs   — CApiHandle (DLL 符号加载),有界 shutdown   │
│  ├─ audio_proxy.rs   — 127.0.0.1 本地音频代理 (CORS + range +    │
│  │                     resume + SSRF allowlist,签名 URL 不入 JS) │
│  ├─ stats.rs         — 6 个 Tauri 统计命令                       │
│  ├─ ai_analysis.rs   — DeepSeek AI 分析 (30s, shared reqwest)   │
│  └─ os_media_session.rs — OS Media Session + tray + 媒体键      │
└────────────────┬───────────────────────────────────────────────┘
                 │ extern "C" FFI (EchoCAPI.dll)
┌────────────────▼───────────────────────────────────────────────┐
│  C++ Core (native/) → EchoCAPI.dll                              │
│  ├─ core/C_API.cpp       — Echo* 导出,Meyers 单例 EchoContext   │
│  │                          (api/scheduler/stats/db + rwlock)   │
│  ├─ core/HttpClient.cpp  — WinHTTP + watchdog + 重试预算 + 连接池│
│  ├─ core/CompatApi.cpp   — KuGou API 路由中央调度 (~60 条路由)    │
│  ├─ core/compat_routes/  — 7 个路由文件 (Login/Media/Playlist/   │
│  │                         User/Register/YouthVip/Diagnostics)  │
│  ├─ async/RequestScheduler.cpp — 4-worker 线程池,per-kind deadline│
│  ├─ async/RequestWatchdog.cpp  — lazy-drop 截止时间 watchdog      │
│  ├─ storage/Database.cpp — SQLite WAL + busy_timeout + Actor 模式│
│  │                          (play_history_v2 schema)            │
│  └─ stats/PlayStatsService.cpp — record + 5 查询 SQL             │
└────────────────────────────────────────────────────────────────┘

┌────────────────────────────────────────────────────────────────┐
│  server/ — Node.js KuGou API 代理 (非生产运行时,参考实现)        │
│  Express + axios,crypto-js + node-forge 签名,pako KRC 解码      │
│  130+ module/*.js (login/search/song_url/playlist/lyric/...)    │
│  → 由 native/ C++ 层重新实现,生产中不调用 server/                │
└────────────────────────────────────────────────────────────────┘
```

**核心架构原则**(详见 [CONTEXT.md](file:///c:/BottleMusic/CONTEXT.md)):

1. **FFI 边界单一**:所有 KuGou API 调用走 `EchoHandleRequest` 一个 C 导出函数,由 `CompatApi` 中央调度
2. **三层 deadline**:Rust `deadline_for_path`(外层) → C++ `RequestScheduler` per-kind(中层) → `HttpClient` watchdog(内层)
3. **EQ 拓扑安全**:`captureStream → MediaStreamSource → AudioWorkletNode → GainNode → destination`,**绝不** `createMediaElementSource`(避免与 `<audio>` 双重路由)
4. **本地音频代理**:KuGou CDN 不发 CORS 头,Tauri 127.0.0.1 代理重新服务 CDN 媒体,签名 URL 不暴露给前端 JS
5. **无 Pinia**:所有 store 是模块级 `reactive`/`ref` 单例,靠 HMR 共享引用(`window.__bottlemusic_audio__`)保证 zombie-audio 安全
6. **Meyers 单例**:C++ 全局状态用 `static EchoContext& Ctx()` 聚合,`shared_ptr<CompatApi>` 让 worker 在 shutdown 期间也能安全完成请求

---

## 3. 快速上手

### 3.1 环境要求

| 工具 | 版本 | 说明 |
|---|---|---|
| Node.js | ≥ 22 | 实测 v24.17.0 |
| pnpm | 11 | 实测 v11.8.0 |
| Rust | stable | 实测 1.96.0 |
| CMake + MSVC | C++20 | VS 2022+ (实测 VS 18 Community) |
| vcpkg | 任意近期版本 | triplet: x64-windows |
| Windows | 10/11 x64 | 仅 Windows 平台 |

### 3.2 克隆与首次启动

```powershell
# 1. 克隆(必须 --recurse-submodules,因为 server/ 是子模块)
git clone --recurse-submodules https://github.com/Ningbottle/BottlePlayer.git
cd BottlePlayer\ui

# 2. 安装前端依赖
pnpm install

# 3. 启动开发环境(同时启动 Vite + Tauri + 自动构建 EchoCAPI.dll)
pnpm tauri dev
```

`pnpm tauri dev` 会自动:
- 调用 `beforeDevCommand: pnpm dev` 启动 Vite(:1420)
- Tauri 加载 `EchoCAPI.dll`(若不存在则按 `bottlemusic-check` preset 构建)
- 启动 Rust 进程,绑定本地音频代理 127.0.0.1 随机端口
- 弹出 1280×820 无边框窗口

### 3.3 三层独立构建(便于定位问题)

```powershell
# === C++ 层(需要 vcvars64)===
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake -S C:\BottleMusic\native --preset bottlemusic-check && cmake --build C:\BottleMusic\native\out\bottlemusic-check --config Debug'

# === Rust 层 ===
cargo build --manifest-path C:\BottleMusic\ui\src-tauri\Cargo.toml

# === 前端层 ===
cd C:\BottleMusic\ui
pnpm dev                    # 仅 Vite 开发服务器
pnpm build                  # 类型检查 + 生产构建
pnpm exec vue-tsc --noEmit  # 仅类型检查

# === 同步 DLL(C++ 重编但 Rust 未重编时)===
pnpm backend:sync           # 把 EchoCAPI.dll + sqlite3.dll 拷到 src-tauri/libs/
pnpm backend:build          # 一键构建 + 同步
```

### 3.4 测试

```powershell
# === C++ ctest(11 个测试)===
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && set PATH=C:\BottleMusic\native\vcpkg_installed\x64-windows\bin;%PATH% && ctest --test-dir C:\BottleMusic\native\out\bottlemusic-check --output-on-failure'

# === Rust cargo test(22 个测试)===
cargo test --manifest-path C:\BottleMusic\ui\src-tauri\Cargo.toml --lib

# === 前端 vitest(76 文件 / 917 用例,2026-07-22 基线)===
cd C:\BottleMusic\ui
pnpm test -- --run          # 单次运行
pnpm test:watch             # watch 模式

# === 回放压力测试(真实 PlaybackCommandCoordinator,默认 1000 命令)===
node scripts/playback-stress.mjs
# 或自定义命令数
node scripts/playback-stress.mjs --commands 5000
```

### 3.5 发布构建

```powershell
# 触发 release:推 v* 标签
git tag v1.0.0
git push origin v1.0.0
# GitHub Actions release.yml 自动:
#   1. sync-version.mjs 同步版本号到 package.json/Cargo.toml/CMakeLists.txt/vcpkg.json
#   2. 构建 Release EchoCAPI.dll
#   3. 运行全部测试作为门禁
#   4. tauri-action 生成 NSIS 安装包并上传 GitHub Releases
#   5. 生成 latest.json 供应用内 updater 拉取
```

本地手动打包:`pnpm tauri build`(需先 `pnpm backend:build` 生成 Release DLL)。

### 3.6 新人 30 分钟导读路径

1. **0–5 分钟**:读 [CONTEXT.md](file:///c:/BottleMusic/CONTEXT.md)(项目入门必读)
2. **5–10 分钟**:浏览 [ui/src/main.ts](file:///c:/BottleMusic/ui/src/main.ts) + [App.vue](file:///c:/BottleMusic/ui/src/App.vue) 理解前端启动顺序与 FOUC 防护
3. **10–15 分钟**:读 [ui/src/api/playerStore.ts](file:///c:/BottleMusic/ui/src/api/playerStore.ts) + [playbackOrchestrator.ts](file:///c:/BottleMusic/ui/src/api/playbackOrchestrator.ts) 理解播放核心
4. **15–20 分钟**:读 [ui/src-tauri/src/lib.rs](file:///c:/BottleMusic/ui/src-tauri/src/lib.rs) 理解 Tauri app 装配
5. **20–25 分钟**:读 [native/core/C_API.cpp](file:///c:/BottleMusic/native/core/C_API.cpp) + [native/core/CompatApi.cpp](file:///c:/BottleMusic/native/core/CompatApi.cpp) 理解 C++ FFI 边界与路由分发
6. **25–30 分钟**:运行 `pnpm tauri dev`,点击播放一首歌,观察控制台 `ECHO_LOG` 输出,在 [ui/src/api/playbackDiagnostics.ts](file:///c:/BottleMusic/ui/src/api/playbackDiagnostics.ts) 的环形缓冲中查看 boundary 事件

---

## 4. 项目地图

### 4.1 顶层目录结构

```
c:\BottleMusic\
├─ .github\workflows\        # 主仓库 CI/CD(ci.yml + release.yml)
├─ assets\icons\             # 根级图标(icon.ico/png)
├─ native\                   # ★ C++ 核心层(→ EchoCAPI.dll)
│  ├─ CMakeLists.txt         # 顶层 CMake(6 个库目标 + 11 测试目标)
│  ├─ CMakePresets.json      # bottlemusic-check / bottlemusic-release
│  ├─ vcpkg.json             # vcpkg manifest(cpp-httplib/nlohmann-json/sqlite3/spdlog/wil)
│  ├─ core\                  # 核心服务 + CompatApi + C_API FFI
│  │  ├─ C_API.cpp           # ★ extern "C" Echo* 导出
│  │  ├─ CompatApi.cpp       # ★ 路由中央调度
│  │  ├─ HttpClient.cpp      # WinHTTP + 重试 + 连接池
│  │  ├─ Crypto.cpp          # 全部加解密原语(MD5/AES/RSA/签名)
│  │  ├─ KuGouAndroidRequest.cpp  # 拼装签名 URL + Android 头
│  │  ├─ KuGouProfile.cpp    # Concept/Standard 双 profile
│  │  ├─ LoginService.cpp / SearchService.cpp / SongUrlService.cpp
│  │  ├─ PlaylistService.cpp / LyricService.cpp / LyricParser.cpp
│  │  ├─ HomeService.cpp / RankService.cpp / PrivilegeService.cpp
│  │  ├─ CatalogService.cpp / UserService.cpp / UserCloudService.cpp
│  │  ├─ DeviceService.cpp / DeviceRegisterService.cpp / PlayHistoryService.cpp
│  │  ├─ Authorization.cpp / CompatRequestContext.cpp
│  │  ├─ HttpUtils.cpp / JsonHelpers.cpp / StringUtils.cpp
│  │  └─ compat_routes\      # 7 个路由文件(Diagnostics/Login/Media/Playlist/Register/User/YouthVip)
│  ├─ async\                 # 异步调度
│  │  ├─ RequestScheduler.cpp  # 4-worker 线程池 + per-kind deadline
│  │  ├─ RequestWatchdog.cpp   # lazy-drop watchdog
│  │  ├─ TaskScheduler.cpp     # std::async + CancellationToken
│  │  └─ EventQueue.cpp        # backend→frontend 事件队列
│  ├─ storage\               # 持久化
│  │  ├─ Database.cpp        # ★ SQLite WAL + Actor 模式 + play_history_v2
│  │  ├─ AppPaths.cpp        # %LOCALAPPDATA%\EchoMusicNative
│  │  ├─ ApiCache.cpp        # 2 分钟 TTL 缓存
│  │  ├─ DeviceRepository.cpp / SessionRepository.cpp (DPAPI) / SettingsRepository.cpp
│  ├─ stats\
│  │  └─ PlayStatsService.cpp # record + 5 查询
│  ├─ diagnostics\           # EchoDiagnostics/MemorySnapshot/Redaction/ScopedTimer
│  ├─ image\                 # ImageCache (LRU) + ImageLoader (WIC + 三级缓存)
│  ├─ include\echo\          # 公共头文件(async/core/diagnostics/image/stats/storage)
│  ├─ tests\                 # 11 个 ctest 测试 + fixtures
│  └─ tools\Measure-PlaybackStability.ps1
├─ ui\                       # ★ Vue 3 前端 + Tauri Rust 外壳
│  ├─ package.json           # bottle-music-ui 1.0.0
│  ├─ vite.config.ts / vitest.config.ts / tsconfig.json
│  ├─ pnpm-workspace.yaml
│  ├─ src\                   # 前端源码
│  │  ├─ main.ts             # bootstrap(字体→tokens→皮肤→mount)
│  │  ├─ App.vue             # 根外壳(KeepAlive + PageRecoveryBoundary)
│  │  ├─ api\                # ★ 40+ 状态/服务模块(无 Pinia)
│  │  │  ├─ playerStore.ts / playbackOrchestrator.ts / playSessionTracker.ts
│  │  │  ├─ playbackCommandCoordinator.ts / playbackPhase.ts / playbackDiagnostics.ts
│  │  │  ├─ playerBackend.ts / html5Backend.ts / webAudioEq.ts / eqWorkletProcessor.ts
│  │  │  ├─ backend.ts / circuitBreaker.ts / audioProxy.ts / songUrlResolver.ts
│  │  │  ├─ themeStore.ts / appearanceStore.ts / motion.ts / motionProfiles.ts
│  │  │  ├─ favorite.ts / favoriteStore.ts / favoriteRepository.ts / favoriteMarkers.ts
│  │  │  ├─ recentPlayedStore.ts / playHistory.ts / homeFeedStore.ts / homeEnterSession.ts
│  │  │  ├─ fmSession.ts / transitionSession.ts / lyricFocusStore.ts / lyricFullscreen.ts
│  │  │  ├─ useLyricFollow.ts / lyricsResource.ts / userStore.ts / safeStorage.ts
│  │  │  ├─ skippedVersion.ts / osMediaBridge.ts / audioLevelMonitor.ts / coverFlight.ts
│  │  │  ├─ normalizer.ts / playerPersistence.ts / usePlayerEq.ts / equalizerConfig.ts
│  │  │  └─ vipResolver.ts
│  │  ├─ views\              # 10 个顶层视图 + home/ + lyric/ 子目录
│  │  ├─ components\         # 顶层组件 + player/ + primitives/ + shell/
│  │  ├─ navigation\         # router.ts / routes.ts / navigationLifecycle.ts
│  │  ├─ styles\             # tokens.css + progress.css + skins/(aurora/newsprint)
│  │  ├─ test\setup.ts       # vitest setup
│  │  └─ style.css / vite-env.d.ts / vue.svg
│  ├─ src-tauri\             # Tauri Rust 外壳
│  │  ├─ Cargo.toml          # ui 1.0.0,ui_lib crate
│  │  ├─ tauri.conf.json     # ★ 1280×820 无边框,CSP,updater,bundle
│  │  ├─ build.rs            # 生成 deadlines 模块(从 C++ RequestDeadlines.h)
│  │  ├─ capabilities\default.json  # ★ 最小权限(仅 m.kugou.com opener)
│  │  ├─ icons\              # Tauri 应用图标(各尺寸)
│  │  ├─ src\
│  │  │  ├─ main.rs          # Windows 入口(无控制台窗口)
│  │  │  ├─ lib.rs           # ★ Tauri run() + invoke_handler + DLL 加载
│  │  │  ├─ backend_api.rs   # ★ CApiHandle + RwLock<Option<>> + 有界 shutdown
│  │  │  ├─ audio_proxy.rs   # ★ 127.0.0.1 音频代理(SSRF allowlist + range/resume)
│  │  │  ├─ stats.rs         # 6 个 Tauri 统计命令
│  │  │  ├─ ai_analysis.rs   # DeepSeek AI 分析(30s,shared reqwest)
│  │  │  └─ os_media_session.rs  # OS Media Session + tray + 媒体键
│  │  └─ tests\playback_ffi_test.rs
│  ├─ scripts\               # 构建/同步/QA/压测脚本(7 个 .ps1/.mjs)
│  ├─ design-qa-captures\    # Playwright QA 截图矩阵
│  └─ design-qa.md           # Aurora Turntable Night Design QA Ledger
├─ server\                   # ★ Node.js KuGou API 代理(子模块,非生产运行时)
│  ├─ package.json           # kugoumusicapi 1.5.1
│  ├─ index.js / app.js / main.js / server.js  # 入口链
│  ├─ util\                  # 9 个工具模块(crypto/request/helper/apicache/...)
│  ├─ module\                # ★ 130+ API 端点模块(login/search/song_url/...)
│  ├─ public\index.html      # 落地页
│  ├─ docs\                  # docsify 文档站(133 个接口文档)
│  ├─ Dockerfile / vercel.json / .env.example
│  └─ .github\workflows\build.yml  # 子模块自带 pkg 多平台构建
├─ docs\                     # ⚠ gitignored — 用 git add -f 跟踪
│  └─ superpowers\specs|plans|reports\  # 各阶段规格/计划/验证报告
├─ .gitmodules               # 1 个子模块:server → MakcRe/KuGouMusicApi
├─ .gitignore
├─ CHANGELOG.md              # ⚠ 严重滞后(2026-02-03 至 2026-05-22,见 §18)
├─ CONTEXT.md                # ★ 项目入门必读
├─ README.md / README.en.md  # 中英双语 README
├─ PRIVACY.md / SECURITY.md  # 隐私与安全政策
├─ LICENSE                   # MIT,Copyright (c) 2026 hoowhoami
└─ Music Player.html         # 历史遗留文件(commit 0bedf68 重写,随应用发布)
```

### 4.2 关键文件索引(按使用频率排序)

| 文件 | 职责 | 重要度 |
|---|---|---|
| [CONTEXT.md](file:///c:/BottleMusic/CONTEXT.md) | 项目入门必读 | ★★★★★ |
| [native/core/C_API.cpp](file:///c:/BottleMusic/native/core/C_API.cpp) | C FFI 边界,所有 Echo* 导出 | ★★★★★ |
| [native/core/CompatApi.cpp](file:///c:/BottleMusic/native/core/CompatApi.cpp) | KuGou API 路由中央调度 | ★★★★★ |
| [ui/src/api/playerStore.ts](file:///c:/BottleMusic/ui/src/api/playerStore.ts) | 播放器响应式状态单例 | ★★★★★ |
| [ui/src/api/playbackOrchestrator.ts](file:///c:/BottleMusic/ui/src/api/playbackOrchestrator.ts) | 播放转换编排器 | ★★★★★ |
| [ui/src-tauri/src/lib.rs](file:///c:/BottleMusic/ui/src-tauri/src/lib.rs) | Tauri app 装配 + DLL 加载 | ★★★★★ |
| [ui/src-tauri/src/backend_api.rs](file:///c:/BottleMusic/ui/src-tauri/src/backend_api.rs) | CApiHandle + 有界 shutdown | ★★★★★ |
| [ui/src-tauri/src/audio_proxy.rs](file:///c:/BottleMusic/ui/src-tauri/src/audio_proxy.rs) | 127.0.0.1 音频代理 | ★★★★★ |
| [native/core/HttpClient.cpp](file:///c:/BottleMusic/native/core/HttpClient.cpp) | WinHTTP + 重试 + 连接池 | ★★★★ |
| [native/async/RequestScheduler.cpp](file:///c:/BottleMusic/native/async/RequestScheduler.cpp) | 4-worker 线程池 | ★★★★ |
| [native/storage/Database.cpp](file:///c:/BottleMusic/native/storage/Database.cpp) | SQLite WAL + Actor 模式 | ★★★★ |
| [native/core/Crypto.cpp](file:///c:/BottleMusic/native/core/Crypto.cpp) | 全部加解密原语 | ★★★★ |
| [ui/src/api/webAudioEq.ts](file:///c:/BottleMusic/ui/src/api/webAudioEq.ts) | Web Audio EQ 图控制器 | ★★★★ |
| [ui/src/api/playSessionTracker.ts](file:///c:/BottleMusic/ui/src/api/playSessionTracker.ts) | seek-immune 听歌秒数累加 | ★★★★ |
| [ui/src/api/backend.ts](file:///c:/BottleMusic/ui/src/api/backend.ts) | Tauri invoke + 熔断 + 14s 超时 | ★★★★ |
| [ui/src/api/favoriteStore.ts](file:///c:/BottleMusic/ui/src/api/favoriteStore.ts) | 收藏领域权威 store | ★★★ |

---

## 5. native/ — C++ 核心层

### 5.1 CMake 构建结构

文件:[native/CMakeLists.txt](file:///c:/BottleMusic/native/CMakeLists.txt)

要求 CMake 3.24+、C++20、MSVC `/utf-8`。Release 配置下若找不到 SQLite3 则 `FATAL_ERROR` 中止(Debug 仅 WARNING + JSON 文件回退)。

#### 6 个库目标

| 目标 | 类型 | 源文件 | 链接依赖 |
|---|---|---|---|
| **EchoStorage** | STATIC | storage/6 个 .cpp(Database/AppPaths/ApiCache/DeviceRepo/SessionRepo/SettingsRepo) | nlohmann_json;PRIVATE: crypt32 |
| **EchoDiagnostics** | STATIC | diagnostics/4 个 .cpp | PRIVATE: psapi |
| **EchoAsync** | STATIC | async/4 个 .cpp | 无 |
| **EchoCore** | STATIC | core/*.cpp + compat_routes/*.cpp + stats/PlayStatsService.cpp | PUBLIC: EchoStorage+EchoDiagnostics+EchoAsync+nlohmann_json;PRIVATE: winhttp+bcrypt+crypt32 |
| **EchoCAPI** | **SHARED** | core/C_API.cpp | PUBLIC: EchoCore → 产出 **EchoCAPI.dll** |
| **EchoImage** | STATIC | image/2 个 .cpp | PUBLIC: EchoAsync;PRIVATE: windowscodecs+ole32 |

#### vcpkg manifest

文件:[native/vcpkg.json](file:///c:/BottleMusic/native/vcpkg.json)

依赖:`cpp-httplib`、`nlohmann-json`、`sqlite3`、`spdlog`、`wil`(manifest 模式,triplet x64-windows)。

### 5.2 C_API — FFI 边界(extern "C" 导出)

文件:[native/include/echo/core/C_API.h](file:///c:/BottleMusic/native/include/echo/core/C_API.h) + [native/core/C_API.cpp](file:///c:/BottleMusic/native/core/C_API.cpp)

通过 `ECHO_C_API` 宏导出(Windows 上展开为 `__declspec(dllexport)`)。

#### 导出函数清单

| 函数 | 签名 | 作用 |
|---|---|---|
| `EchoInitialize` | `void()` | 旧版初始化(无路径) |
| `EchoInitializeWithPaths` | `void(const char*)` | 旧版(带数据目录) |
| `EchoInitializeV2` | `int()` | V2 初始化,返回 0=成功 |
| `EchoInitializeWithPathsV2` | `int(const char*, const char*)` | **推荐** V2 带路径,返回 0=成功 |
| `EchoShutdown` | `int()` | 两阶段有界 shutdown,返回被遗弃 worker 数 |
| `EchoGetLastError` | `const char*()` | 取最近错误 |
| `EchoSetLogCallback` | `void(EchoLogCallback, void*)` | 注入 FFI 日志回调 |
| `EchoHandleRequest` | `const char*(const char* method, const char* path, const char* query, const char* headers, const char* body)` | ★ **中央请求分发** |
| `EchoFreeString` | `void(const char*)` | 释放 C 字符串(配对调用) |
| `EchoStatsRecordPlay` | `void(const char* json)` | 写入一条播放记录 |
| `EchoStatsGetSummary` | `const char*(const char* range)` | 汇总查询(1d/7d/30d/all) |
| `EchoStatsGetTop` | `const char*(const char* kind, const char* range, int limit)` | Top-N(song/artist/album) |
| `EchoStatsGetTimeline` | `const char*(const char* range)` | 每日播放计数 |
| `EchoStatsGetRecent` | `const char*(int limit, int offset)` | 最近播放(分页) |
| `EchoStatsGetRecommendations` | `const char*(int limit)` | 推荐歌手 |

#### 全局状态:Meyers 单例

实际实现未使用 `g_api`/`g_scheduler`/`g_stats` 自由全局变量,而是用 `static EchoContext& Ctx()` 聚合:

```cpp
struct EchoContext {
  std::unique_ptr<Database> db;
  std::shared_ptr<CompatApi> api;          // ★ shared_ptr — worker 可安全持引用
  RequestScheduler scheduler{4};            // 默认 4 worker
  std::shared_mutex api_rwlock;             // ★ 读写锁保护 api/stats/db
  std::atomic<bool> shutdown{false};
  std::unique_ptr<PlayStatsService> stats;
  std::string last_error;
};
```

**关键设计**:`api` 为 `shared_ptr` — `EchoHandleRequest` 在 shared lock 下取出 `apiShared` 拷贝(强引用 +1)后即释放读锁,worker 即便在 `EchoShutdown` 期间也能安全持有 `CompatApi` 完成请求。

#### 关键流程

- **`EchoInitializeWithPathsV2`**:取独占锁 → `EnsureInitializedLocked` → `Restart()` 调度器。返回 0=成功,非 0=失败(目录无效等)。
- **`EchoShutdown`**(两阶段):
  1. 第一阶段:`shutdown.store(true)` + `scheduler.Shutdown(3000ms)`,返回被遗弃 worker 数(>0 表示仍有任务,DLL 不可卸载)
  2. 第二阶段:3 秒有界 `try_lock` 独占锁,重置 `api/stats/db` + `CloseHttpConnectionPool()`
- **`EchoHandleRequest`**:解析 query/headers JSON → 取 shared lock(2s 有界 try_lock)→ 拷贝 `apiShared` → 提交调度器,`KindForPath` 判 RequestKind + `DeadlineMsForKind` 给截止时间 → 通过 `HttpClientCancellationScope` 传播取消信号。所有 C++ 异常在 FFI 边界 catch。

### 5.3 异步层(async/)

#### RequestScheduler

文件:[native/async/RequestScheduler.cpp](file:///c:/BottleMusic/native/async/RequestScheduler.cpp)

- **线程池**:默认 4 worker;`maxQueueSize_ = 16`
- **RequestKind**(6 种):`SongUrl / Search / Playlist / LoginPoll / Image / Generic`
- **per-kind deadline**(来自 [RequestDeadlines.h](file:///c:/BottleMusic/native/include/echo/core/RequestDeadlines.h)):

  | Kind | ms |
  |---|---|
  | SongUrl | 10000 |
  | Image | 8000 |
  | LoginPoll | 6000 |
  | Search | 12000 |
  | Playlist | 12000 |
  | Generic | 12000 |

- **Cancel(kind)**:交换 `cancelledFlags_[idx]` 并 bump `generations_[idx]`;旧任务携带旧 generation 在 worker 取出时被丢弃
- **SubmitLatest**:bump generation + cancel 前一代 token → 同类只保留最新一次提交
- **有界 Shutdown(maxWait)**:Cancel 所有 kind → swap `workers_` → helper 线程逐个 join(带截止轮询) → 仍未结束 detach + 返回被遗弃数;置 `abandonedWorkers_=true`,`Restart()` 在该标志置位时返回 false(无法安全重启)
- **无界 Shutdown()**:直接 join 所有 worker

#### RequestWatchdog

文件:[native/async/RequestWatchdog.cpp](file:///c:/BottleMusic/native/async/RequestWatchdog.cpp)

- 进程级单例(`Instance()`)
- 单 worker 线程 + 最小堆(`Entry{deadline, seq, claimed, action}`)
- **lazy-drop 协议**:CAS-claim `claimed`(false→true)后执行 action;CAS 失败说明已被 lazy-drop。`SubmitWithDeadline` 的 `watchdogClaimed` 与 HttpClient 的 `watchdogCancelled` 共享同一原子,任意一方先 CAS 成功即触发取消并防 double-close 竞态

#### TaskScheduler + EventQueue

文件:[native/async/TaskScheduler.cpp](file:///c:/BottleMusic/native/async/TaskScheduler.cpp) + [EventQueue.cpp](file:///c:/BottleMusic/native/async/EventQueue.cpp)

- `CancellationToken`:持 `shared_ptr<atomic_bool>`,暴露 `Flag()` 裸指针供 HttpClient 直接读 thread-local 取消标志
- `TaskScheduler::Schedule`:`std::async(std::launch::async, ...)`
- `ScheduleAndPost`:仅在未取消时把结果 `Push` 进 `EventQueue`,用于 backend→frontend 推送
- `EventQueue`:`BackendEvent{type, payload}`,mutex 保护的 `std::queue`

### 5.4 存储层(storage/)

#### Database — Actor 模式串行化

文件:[native/storage/Database.cpp](file:///c:/BottleMusic/native/storage/Database.cpp)

- **Actor 状态机**:`Closed / Starting / Open / Closing / Failed`
- **串行化**:所有公共方法(Initialize/Execute/ExecuteBound/ExecuteQuery/ExecuteQueryBound/SetJson/GetJson/PutApiCache/GetApiCache/PruneExpiredApiCache)都通过 `Submit<T>` 模板入队执行
- **重入保护**:`std::this_thread::get_id() == actor_tid_` 时抛 `actor_reentrancy` 异常
- **`QuarantineInvalidSqliteFile`**:打开前若文件无效,重命名为 `.invalid-<timestamp>` 隔离
- **`InitializeSchema`** 顺序:
  - `PRAGMA journal_mode=WAL`
  - `PRAGMA synchronous=NORMAL`
  - `PRAGMA busy_timeout=5000`
  - 创建表:`kv_store`、`api_cache`、`play_history`(旧表)、**`play_history_v2`**、`image_cache`
  - 索引:`idx_ph2_played_at` / `idx_ph2_song_hash` / `idx_ph2_singer` / `idx_api_cache_expires`
  - `PRAGMA user_version=1`

#### play_history_v2 表结构

| 列 | 类型 | 说明 |
|---|---|---|
| id | INTEGER PK AUTOINCREMENT | 自增主键 |
| song_hash | TEXT NOT NULL | 歌曲 hash |
| song_name | TEXT NOT NULL | 歌曲名 |
| singer_name | TEXT | 歌手 |
| album_id | TEXT | 专辑 ID |
| album_name | TEXT | 专辑名 |
| cover_url | TEXT | 封面 URL |
| duration_seconds | REAL NOT NULL DEFAULT 0 | 歌曲总时长 |
| completed | INTEGER NOT NULL DEFAULT 0 | 是否听完 |
| listened_seconds | REAL NOT NULL DEFAULT 0 | 实际听了多久 |
| quality | TEXT | 播放音质 |
| played_at | INTEGER NOT NULL | 播放时间戳(毫秒) |

#### 其它仓储

| 文件 | 作用 |
|---|---|
| [AppPaths.cpp](file:///c:/BottleMusic/native/storage/AppPaths.cpp) | `GetAppDataDirectory()` 优先读 `ECHO_NATIVE_DATA_DIR`,否则回落 `%LOCALAPPDATA%\EchoMusicNative`。`GetDefaultDatabasePath()` 返回 `<dir>/echomusic-native.db` |
| [ApiCache.cpp](file:///c:/BottleMusic/native/storage/ApiCache.cpp) | 薄包装 Database 的 PutApiCache/GetApiCache/PruneExpiredApiCache,2 分钟 TTL |
| [DeviceRepository.cpp](file:///c:/BottleMusic/native/storage/DeviceRepository.cpp) | 通过 `GetJson("device.info")` / `SetJson` 实现 DeviceInfo 的 Load/Save/Clear |
| [SessionRepository.cpp](file:///c:/BottleMusic/native/storage/SessionRepository.cpp) | **DPAPI 加密**会话存储。`CryptProtectData`/`CryptUnprotectData` + Base64,key=`session.info`,结构 `{version:1, protected_data:base64}`。一次性迁移:明文 → 加密 → 置迁移标志 → 拒绝明文回写 |
| [SettingsRepository.cpp](file:///c:/BottleMusic/native/storage/SettingsRepository.cpp) | 存储 `AppSettings{volume, startupPage, imageMemoryCacheMb}` 在 `app.settings` key,带 clamp(volume 0-1、imageMemoryCacheMb 8-128) |

### 5.5 核心服务(core/)

#### CompatApi — 路由中央调度

文件:[native/core/CompatApi.cpp](file:///c:/BottleMusic/native/core/CompatApi.cpp)

- `GetRouteTable()` 返回静态 `unordered_map<string, RouteHandlerFn>`,约 60 条路由
- `Handle(method, path, query, headers, body)`:`IsKnownCompatRoute` → `HandleKnownRoute` → 剥离响应体中的会话凭证 → 通过 `ECHO_LOG` 打印 route+status+http+elapsed
- **方法门控**:
  - **kWriteRoutes**(POST-only):`/auth/logout`、`/playlist/add`、`/playlist/del`、`/playlist/tracks/add`、`/playlist/tracks/del`、`/playhistory/upload`、`/register/dev`
  - `/settings/device` 允许 GET + POST
  - 其余路由允许 GET + HEAD
- **未实现路由**(注册为 `nullptr`,返回 501 `native_not_implemented`):`/captcha/sent`、`/login/cellphone`、`/login/wx/create`、`/login/wx/check`、`/login/openplat`、`/user/cloud/url`、`/youth/month/vip/record`、`/artist/follow`、`/artist/unfollow`、`/comment/music/classify`、`/comment/music/hotword`、`/comment/floor`、`/comment/count`、`/favorite/count`、`/video/url`

#### 各服务职责与关键签名

| 服务 | 职责 | 关键公共方法签名 |
|---|---|---|
| [LoginService](file:///c:/BottleMusic/native/core/LoginService.cpp) | QR 二维码登录 | `json BeginQrLogin(const DeviceInfo&) const;` `json PollQrLogin(const DeviceInfo&, const std::string& key) const;` |
| [Authorization](file:///c:/BottleMusic/native/core/Authorization.cpp) | 解析 Authorization 头 | `AuthContext ParseAuthorizationHeader(const std::string&);` |
| [DeviceService](file:///c:/BottleMusic/native/core/DeviceService.cpp) | 加载/创建 DeviceInfo,规范化 mid/uuid | `DeviceInfo EnsureDeviceReady();` + 自由函数 `std::string ResolveAndroidMid(const DeviceInfo&);` |
| [DeviceRegisterService](file:///c:/BottleMusic/native/core/DeviceRegisterService.cpp) | 调 `userservice.kugou.com/risk/v2/r_register_dev` 注册设备 | `std::string Register(const DeviceInfo&, const std::string& userId, const std::string& token, std::string* error) const;` 返回 dfid |
| [KuGouAndroidRequest](file:///c:/BottleMusic/native/core/KuGouAndroidRequest.cpp) | 拼装签名 URL + Android 头 | `BuildSignedUrl(req)`、`BuildAndroidHeaders(req)` |
| [KuGouProfile](file:///c:/BottleMusic/native/core/KuGouProfile.cpp) | Concept/Standard 双 profile | `GetKuGouProfile(Concept)` → appid=3116/clientver=11440/lite salt;`(Standard)` → appid=1005/clientver=20489/standard salt |
| [SearchService](file:///c:/BottleMusic/native/core/SearchService.cpp) | 搜索接口 | `json Search(keywords,type,page,pageSize) const;` `json Hot(int count) const;` `json Suggest(keywords,count) const;` |
| [SongService](file:///c:/BottleMusic/native/core/SongService.cpp) | 高潮/排名 | `json GetClimax(hash) const;` `json GetRanking(albumAudioId) const;` `json GetRankingFilter(albumAudioId,page,pageSize) const;` |
| [SongUrlService](file:///c:/BottleMusic/native/core/SongUrlService.cpp) | **最复杂**:解析 play_url,多 fallback | `ResolveV6PrivUrl(...)`、`Resolve(hash, albumId, albumAudioId, quality, ppageId, userId, token, device)` |
| [PlaylistService](file:///c:/BottleMusic/native/core/PlaylistService.cpp) | 歌单 CRUD | `GetTracks/GetTags/GetTopPlaylists/GetPlaylistDetail/GetUserPlaylists/AddPlaylist/DeletePlaylist/AddPlaylistTracks/DeletePlaylistTracks` |
| [LyricService](file:///c:/BottleMusic/native/core/LyricService.cpp) | 歌词搜索与下载 | `json Search(hash) const;` `json GetDetail(id, accessKey) const;`(base64 解码 content) |
| [LyricParser](file:///c:/BottleMusic/native/core/LyricParser.cpp) | `[mm:ss.xx]` 解析 | `LyricDocument ParseLrc(const std::string&);` `int FindActiveLyricLine(const LyricDocument&, std::int64_t currentMs);` |
| [HomeService](file:///c:/BottleMusic/native/core/HomeService.cpp) | Banner/每日推荐/私人 FM/图片音频 | `GetBanners/GetEverydayRecommend/GetPersonalFm/GetImagesAudio` |
| [RankService](file:///c:/BottleMusic/native/core/RankService.cpp) | 排行榜 | `json List() const;` `json GetSongs(rankId,page,pageSize) const;` |
| [PrivilegeService](file:///c:/BottleMusic/native/core/PrivilegeService.cpp) | 取 lite playInfo | `json GetLite(hash, albumId) const;` |
| [CatalogService](file:///c:/BottleMusic/native/core/CatalogService.cpp) | 专辑/歌手目录 | `GetAlbumDetail/GetAlbumSongs/GetArtistDetail/GetArtistSongs/GetArtistAlbums` |
| [PlayHistoryService](file:///c:/BottleMusic/native/core/PlayHistoryService.cpp) | 上传/拉取播放历史 | `json UploadSong(userId,token,mxid,time,pc) const;` `json GetUserHistory(userId,token,bp) const;` |
| [UserCloudService](file:///c:/BottleMusic/native/core/UserCloudService.cpp) | 用户云盘列表(AES+RSA 信封) | `json GetList(userId,token,page,pageSize) const;` |
| [UserService](file:///c:/BottleMusic/native/core/UserService.cpp) | 用户详情/VIP/青年模式领奖 | `GetUserDetail/GetUserVip/ClaimVip/UpgradeVipReward/ClaimYouthListenSong/ClaimYouthAdVip` |
| [HttpClient](file:///c:/BottleMusic/native/core/HttpClient.cpp) | WinHTTP 连接池 + 重试 + 取消传播 | `ExecuteRequest`、`HttpConnectionPool` 单例、`HttpClientCancellationScope` thread-local、`CloseHttpConnectionPool()`、`HttpClientLiveRequestHandleCount()` |
| [HttpUtils](file:///c:/BottleMusic/native/core/HttpUtils.cpp) | URL 解析工具 | `UrlDecode/Trim/ToLowerAscii/ParseQuery/ParseHttpRequest` |
| [Crypto](file:///c:/BottleMusic/native/core/Crypto.cpp) | 全部加解密原语 | `CalculateMd5/SignatureWebParams/SignatureRegisterParams/SignatureAndroidParams/SignParamsKey/SignKey/RsaRawEncrypt/RsaPkcs1Encrypt/PlaylistAesEncrypt/PlaylistAesDecrypt/CalculateAndroidMid/Base64EncodeBytes` |
| [JsonHelpers](file:///c:/BottleMusic/native/core/JsonHelpers.cpp) | Device/Session JSON 序列化 | `ToJson/DeviceInfoFromJson/SessionInfoFromJson/ContractJsonMatches` |
| [StringUtils](file:///c:/BottleMusic/native/core/StringUtils.cpp) | URL 编码 | `UrlEncode`(大写 %XX,unreserved `A-Za-z0-9-_.~`) |
| [CompatRequestContext](file:///c:/BottleMusic/native/core/CompatRequestContext.cpp) | 每请求 lazy 加载 Session/Device | `Session()/Device()/UserIdOr/TokenOrEmpty/HasLogin/SaveSession/SaveDevice` |

#### Crypto 关键常量

| 算法 | salt / 关键值 |
|---|---|
| `SignatureWebParams` | `NVPh5oo715z5DIWAeQlhMDsWXXQV4hwt` |
| `SignatureRegisterParams` | `1014` |
| `SignatureAndroidParams` lite | `LnT6xpN3khm36zse0QzvmgTZ3waWdRSA` |
| `SignatureAndroidParams` standard | `OIlwieks28dk2k092lksi2UIkp` |
| `SignKey` KeySalt lite | `185672dd44712f60bb1736df5a377e82` |
| `SignKey` KeySalt standard | `57ae12eb6890223e355ccfcb74edf70d` |
| `RsaRawEncrypt` / `RsaPkcs1Encrypt` | 内嵌两把 RSA-1024 公钥,输出大写 hex |
| `PlaylistAesEncrypt` | 随机 6 字符 seed → md5 → 前 16 字符 key + 后 16 字符 IV,AES-CBC + PKCS7 + base64 |
| `CalculateAndroidMid` | md5 → hex → 手写进制转换成十进制大数字符串(38-39 位) |

### 5.6 Compat 路由(compat_routes/)

| 文件 | 拥有的路由 |
|---|---|
| [DiagnosticsRoutes.cpp](file:///c:/BottleMusic/native/core/compat_routes/DiagnosticsRoutes.cpp) | `/health`、`/healthz`、`/server/now`、`/diagnostics/memory` |
| [LoginRoutes.cpp](file:///c:/BottleMusic/native/core/compat_routes/LoginRoutes.cpp) | `/login/qr/key`、`/login/qr/create`、`/login/qr/check`(状态==4 时自动保存 SessionInfo + 触发 DeviceRegisterService)、`/auth/logout`、`/settings/device` |
| [MediaRoutes.cpp](file:///c:/BottleMusic/native/core/compat_routes/MediaRoutes.cpp) | `/search/hot`、`/search/default`、`/search/suggest`、`/search`、`/top/album`、`/playlist/recommend`、`/rank/top`、`/top/ip`、`/rank/list`、`/top/song`、`/rank/audio`、`/everyday/recommend`、`/personal/fm`、`/privilege/lite`、`/search/lyric`、`/lyric`、`/song/climax`、`/song/ranking`、`/song/ranking/filter`、`/images/audio`、`/album/detail`、`/album/songs`、`/artist/detail`、`/artist/audios`、`/artist/albums`、`/comment/music`、`/comment/playlist`、`/comment/album` |
| [PlaylistRoutes.cpp](file:///c:/BottleMusic/native/core/compat_routes/PlaylistRoutes.cpp) | `/playlist/add`、`/playlist/del`、`/playlist/tracks/add`、`/playlist/tracks/del`、`/playlist/detail`、`/playlist/track/all`、`/playlist/track/all/new`、`/playlist/tags`、`/top/playlist` |
| [RegisterRoutes.cpp](file:///c:/BottleMusic/native/core/compat_routes/RegisterRoutes.cpp) | `/register/dev`(强制重注册或当 `!device.registered && HasLogin` 时自动注册) |
| [UserRoutes.cpp](file:///c:/BottleMusic/native/core/compat_routes/UserRoutes.cpp) | `/user/detail`(自动更新 session nickname/pic)、`/user/vip/detail`、`/user/playlist`(错误码 20017 时自动重新注册设备重试)、`/user/history`、`/user/cloud`、`/playhistory/upload` |
| [YouthVipRoutes.cpp](file:///c:/BottleMusic/native/core/compat_routes/YouthVipRoutes.cpp) | `/youth/day/vip`、`/youth/day/vip/upgrade`(均被 `kugou_vip_legacy_disabled` 关闭)、`/youth/listen/song`、`/youth/vip/ad` |

### 5.7 统计模块(stats/)

文件:[native/stats/PlayStatsService.cpp](file:///c:/BottleMusic/native/stats/PlayStatsService.cpp) + [PlayStatsService.h](file:///c:/BottleMusic/native/include/echo/stats/PlayStatsService.h)

```cpp
struct PlayRecord {
  std::string songHash, songName, singerName, albumId, albumName, coverUrl, quality;
  double durationSeconds;
  bool completed;
  double listenedSeconds;
  long long playedAtMs;
};
constexpr double kMinCountedListenedSeconds = 60.0;  // 低于此值直接丢弃
```

#### record + 5 查询方法(SQL 草图)

| 方法 | SQL 草图 |
|---|---|
| `RecordPlay(PlayRecord)` | `INSERT INTO play_history_v2 (...) VALUES (?1..?11)` |
| `GetSummary(range)` | `SELECT COUNT(*), COALESCE(SUM(listened_seconds),0), COUNT(DISTINCT song_hash), COUNT(DISTINCT singer_name), COALESCE(CAST(SUM(CASE WHEN completed=1 THEN 1 ELSE 0 END) AS FLOAT)/NULLIF(COUNT(*),0),0) FROM play_history_v2 WHERE listened_seconds > ?1 [AND played_at >= ?2]` |
| `GetTop(dim, range, limit)` | `SELECT <nameCol>, COUNT(*) as cnt, COALESCE(SUM(listened_seconds),0) FROM play_history_v2 WHERE listened_seconds > ?1 [AND played_at >= ?2] GROUP BY <groupCol> ORDER BY cnt DESC LIMIT ?3` |
| `GetTimeline(range)` | `SELECT date(played_at/1000, 'unixepoch', 'localtime') AS date, COUNT(*) AS count FROM play_history_v2 WHERE listened_seconds > ?1 [AND played_at >= ?2] GROUP BY date ORDER BY date ASC` |
| `GetRecent(limit, offset)` | `SELECT song_hash, song_name, singer_name, album_name, cover_url, duration_seconds, listened_seconds, completed, quality, played_at FROM play_history_v2 WHERE listened_seconds > ?1 ORDER BY played_at DESC LIMIT ?2 OFFSET ?3` |
| `GetRecommendations(limit)` | `SELECT singer_name, COUNT(*) as cnt FROM play_history_v2 WHERE listened_seconds > ?1 GROUP BY singer_name ORDER BY cnt DESC LIMIT ?2` |

`range` 取值:`1d / 7d / 30d / all`。所有查询均加 `WHERE listened_seconds > 60.0` 过滤短听。`dim` 取值:`song → song_hash`、`artist → singer_name`、`album → album_id`(避免同名合并)。

### 5.8 诊断与图像

#### 诊断模块

| 文件 | 作用 |
|---|---|
| [EchoDiagnostics.cpp](file:///c:/BottleMusic/native/diagnostics/EchoDiagnostics.cpp) | `SetLogCallback(cb, user_data)` 用 atomic 存回调。`LogDebug(tag, msg)` 先调 `RedactSensitive(msg)` 再转发给 FFI 回调(level=0)或退化为 `OutputDebugStringA` / `std::cerr`。`ECHO_LOG` 宏经此分发 |
| [MemorySnapshot.cpp](file:///c:/BottleMusic/native/diagnostics/MemorySnapshot.cpp) | `GetProcessMemoryInfo` 取 WorkingSetSize + PrivateUsage,`FormatMemorySnapshot` 输出 `memory working_set=… private=… image_cache=… pending_tasks=… playback=…` |
| [Redaction.cpp](file:///c:/BottleMusic/native/diagnostics/Redaction.cpp) | 敏感信息脱敏:`mask_url_queries`、`mask_param`(token=/Cookie=/KugooID= 全脱敏,dfid= 3+3,userid= 2+2)、`mask_header_line`(Cookie/Authorization 行)、`TruncateForLog` |
| [ScopedTimer.cpp](file:///c:/BottleMusic/native/diagnostics/ScopedTimer.cpp) | `Stopwatch::Start()` + `ElapsedMs()`(steady_clock) |

#### 图像模块

| 文件 | 作用 |
|---|---|
| [ImageCache.cpp](file:///c:/BottleMusic/native/image/ImageCache.cpp) | `MemoryImageCache(byteBudget)` LRU 缓存,`std::list<Entry>` + `unordered_map<key, list-iterator>`。`Put` 替换 + 按字节预算 trim;`Get` splice 到表头 |
| [ImageLoader.cpp](file:///c:/BottleMusic/native/image/ImageLoader.cpp) | `WicImageDecoder` 用 WIC factory → decoder → frame → format converter(32bppPBGRA)→ CopyPixels。`DiskImageCache` 写 FNV-1a 哈希命名的 `.img` 文件,按最旧 `last_write_time` trim。`ImageLoader::LoadFile/LoadRemote` 按 memory→disk→fetch 三级查询 |

### 5.9 测试(native/tests/)

11 个 ctest 测试目标(见 §10.1)。fixtures 位于 [native/tests/fixtures/compat/](file:///c:/BottleMusic/native/tests/fixtures/compat/)(`login_qr_key_not_implemented.json`、`lyric.json`、`playlist_track_all.json`、`search.json`、`song_url.json`)。

| 测试文件 | 覆盖范围 |
|---|---|
| [basic_contract_tests.cpp](file:///c:/BottleMusic/native/tests/basic_contract_tests.cpp) | 总烟雾测试:Authorization 头解析、ResolveAndroidMid 边界、BuildSignedUrl + BuildAndroidHeaders 完整字段断言、CancellationToken、RequestScheduler 多形态、LyricParser、Redaction、MemorySnapshot、ImageCache LRU、JsonHelpers、所有仓储 |
| [route_contract_test.cpp](file:///c:/BottleMusic/native/tests/route_contract_test.cpp) | 路由表分发契约:~60 条路由必须被 `IsKnownCompatRoute` 识别;未实现路由返回 501;未知路由返回 404;方法门控(写路由 POST-only) |
| [songurl_contract_test.cpp](file:///c:/BottleMusic/native/tests/songurl_contract_test.cpp) | SongUrl 输出形态契约:mock HttpClient,断言 `Resolve` 输出顶层 `status/url/data` 与 `data.play_url/backup_url/hash/...`;`ResolveV6PrivUrl` 必须 POST;status=1 强断言;hash 小写规范化;按 bitrate 选最高 quality |
| [playlist_contract_test.cpp](file:///c:/BottleMusic/native/tests/playlist_contract_test.cpp) | 歌单输出形态契约:`GetTracks` 输出 `data.songs[]` 字段;`GetUserPlaylists` 必须同时返回 `id`/`listid`/`global_collection_id` 三者一致 |
| [profile_signature_contract_test.cpp](file:///c:/BottleMusic/native/tests/profile_signature_contract_test.cpp) | Profile/签名一致性:Concept profile 固定 appid=3116/clientver=11440/salt=Lite;Standard 固定 appid=1005/clientver=20489/salt=Standard;`BuildSignedUrl` 必须含全部必需参数 + signature + key |
| [home_contract_test.cpp](file:///c:/BottleMusic/native/tests/home_contract_test.cpp) | HomeService 请求参数快照:`GetBanners`/`GetEverydayRecommend` 用 appid=1014/clientver=20000(非 Concept profile);`GetPersonalFm` 用 appid=3116 + body 必须含 `"action":"play"` 与 `"hash":"HASH1"`。注释明确标注 "P2-3d must consult this test before changing" |
| [http_client_resilience_test.cpp](file:///c:/BottleMusic/native/tests/http_client_resilience_test.cpp) | HttpClient 韧性:本地启动 unresponsive TCP server 触发超时;200 OK server 验证 success path(防 P0-A 句柄泄漏回归);总时间预算 + max-body-size 守卫;重试 {500, 2000}ms 退避 |
| [request_scheduler_resilience_test.cpp](file:///c:/BottleMusic/native/tests/request_scheduler_resilience_test.cpp) | 调度器韧性:`SubmitWithDeadline` 100ms 超时抛 `std::runtime_error`;正常任务 5s deadline 返回 42;deadline 触发后 token 必须 `IsCancellationRequested()` 让 worker 早退;有界 `Shutdown(500ms)` 不阻塞 |
| [database_actor_lifecycle_test.cpp](file:///c:/BottleMusic/native/tests/database_actor_lifecycle_test.cpp) | Storage Actor 生命周期:Close 幂等;Close 后公共写入抛 `database_not_accepting`;100 次 Open/Close 循环;8 线程 × 1000 R/W 串行化零失败 |
| [database_wal_concurrency_test.cpp](file:///c:/BottleMusic/native/tests/database_wal_concurrency_test.cpp) | WAL 模式 + 并发(仅 SQLite):`#if !defined(ECHO_NATIVE_HAS_SQLITE) #error`;断言 `PRAGMA journal_mode` 返回 `wal`;8 线程 × 50 ops 并发零失败 |
| [play_stats_test.cpp](file:///c:/BottleMusic/native/tests/play_stats_test.cpp) | PlayStatsService C API 契约:无效目录初始化失败;种子 6 条 counted play 跨 2 天/3 歌/2 歌手/2 专辑;覆盖 record + 5 查询的字段正确性、排序、range 过滤、分页 |

---

## 6. ui/ — Vue 3 前端 + Tauri 2.0 Rust 外壳

### 6.1 包与构建

文件:[ui/package.json](file:///c:/BottleMusic/ui/package.json)

- **包名** `bottle-music-ui`,版本 `1.0.0`,MIT,作者 Ningbottle
- **scripts**:`dev`(vite)、`build`(vue-tsc --noEmit && vite build)、`tauri`、`backend:build`、`backend:sync`、`test`(vitest run)、`test:watch`
- **关键运行时依赖**:
  - `@tauri-apps/api ^2` + `plugin-opener ^2` + `plugin-process ^2` + `plugin-shell ^2` + `plugin-updater ^2.10.1`
  - `gsap ^3.15.0`(动效核心)
  - `qrcode ^1.5.4`(登录二维码本地兜底生成)
  - `vue ^3.5.13`、`vue-router ^4.6.4`
  - 自托管字体:`@fontsource/eb-garamond`、`@fontsource/inter`、`@fontsource/libre-caslon-display`、`@fontsource/noto-serif-sc`、`@fontsource/zcool-xiaowei`
  - 图标:`@lucide/vue ^1.24.0`(Newsprint 用)、`@phosphor-icons/vue ^2.2.1`(Aurora 用)
- **devDependencies**:`@tauri-apps/cli ^2`、`@vitejs/plugin-vue ^5.2.1`、`@vue/test-utils ^2.4.10`、`jsdom ^29.1.1`、`playwright ^1.61.1`、`typescript ~5.6.2`、`vite ^6.0.3`、`vitest ^4.1.7`、`vue-tsc ^2.1.10`

[vite.config.ts](file:///c:/BottleMusic/ui/vite.config.ts):Tauri 专属配置,固定端口 1420,`strictPort: true`,`TAURI_DEV_HOST` 存在时 HMR 走 1421,排除 `**/src-tauri/**`。

[vitest.config.ts](file:///c:/BottleMusic/ui/vitest.config.ts):`environment: 'jsdom'`、`globals: true`、`setupFiles: ['./src/test/setup.ts']`。

[tsconfig.json](file:///c:/BottleMusic/ui/tsconfig.json):`target: ES2020`、`moduleResolution: 'bundler'`、`strict: true`、`noUnusedLocals/Parameters: true`。

### 6.2 入口与应用外壳

#### main.ts — bootstrap(FOUC 防护关键顺序)

文件:[ui/src/main.ts](file:///c:/BottleMusic/ui/src/main.ts)

1. 顺序导入自托管字体 CSS(Inter / EB Garamond / Libre Caslon Display / ZCOOL XiaoWei / Noto Serif SC)
2. 导入 `styles/tokens.css`、`styles/progress.css`、`style.css`、`styles/skins/aurora.css`、`styles/skins/newsprint.css`
3. **在 `createApp(App)` 之前**调用 `useThemeStore().init()` 与 `useLyricFocusStore().init()` —— 把 `data-skin` / `data-mode` 等 DOM 属性写到 `<html>` 上,保证挂载时首帧皮肤已生效
4. `app.use(router); app.mount('#app')`

#### App.vue — 根外壳

文件:[ui/src/App.vue](file:///c:/BottleMusic/ui/src/App.vue)

- `currentShell` 根据 `themeStore.skinId` 在 `AuroraShell` 与 `NewsprintShell` 间切换
- 页面过渡模式:Aurora 用 overlap,Newsprint 用 `'out-in'`
- `launchPlayed` 引导动画:GSAP timeline 分阶 stagger titlebar / shell-sidebar / shell-content / shell-playerbar
- `networkDegraded` ref + 每 5 秒 `ping()` 轮询
- `onMounted`:`setLyricFullscreen(false)` → `initPlayer()` → `initPlayerBackend()` → 仅当 `__TAURI_INTERNALS__` 存在时 `bindOsMediaBridge()` → `checkLoginStatus()` → 启动网络 ping 循环
- `KeepAlive :include="keepAliveComponents"` 来自路由 meta
- `PageRecoveryBoundary` 包裹 `<RouterView>`:`onErrorCaptured` 时 settle 过渡会话,提供「返回首页 / 重试当前页面」
- `FullscreenWindowControls` 在 `lyricFullscreen` 时覆盖:Aurora 隐藏退出按钮,Newsprint 同时显示最小化与退出

### 6.3 api/ 层(状态与服务)

> **该层未使用 Pinia**,所有 store 都是模块级 `reactive` / `ref` 单例。每个 `.ts` 一个职责。

#### 6.3.1 播放核心

| 文件 | 职责 | 关键导出 |
|---|---|---|
| [playerStore.ts](file:///c:/BottleMusic/ui/src/api/playerStore.ts) | 播放器响应式 store 单例(PlayerState) | `initPlayer`、`initPlayerBackend`、`disposePlayerRuntime`、`playTrack` / `setQuality` / `togglePlay` / `next` / `prev` / `seek` / `setVolume` / `playAll` / `playPersonalFm` / `addToQueue` / `removeFromQueue` / `clearQueue`。Zombie-audio 防护:`window.__bottlemusic_audio__` 共享 `<audio>` 元素 |
| [playbackOrchestrator.ts](file:///c:/BottleMusic/ui/src/api/playbackOrchestrator.ts) | 以 `transitionSeq` 超使(supersede)语义驱动的播放切换编排器 | `switchTrack`、`switchQuality` / `switchQualityAtPosition`、`resumeOrReloadCurrent`、`replaySameTrack`、`cancelPendingPlayback`、`clearCurrentPlayback`、`stopInvalidatedPlayback`、`invalidatePlaybackIntent`、`detachPlaybackIntent`(HMR 用的纯 seq bump)、`getTransitionSeq` / `isTransitionCurrent` |
| [playSessionTracker.ts](file:///c:/BottleMusic/ui/src/api/playSessionTracker.ts) | 纯逻辑播放会话累加器(不依赖 Tauri/DOM) | `intend` / `onPlay` / `onPause` / `onEnded` / `skip` / `onTimeUpdate`。阶段:`idle / pending / playing / paused`。`SEEK_THRESHOLD = 2s`(仅正向且 <2s delta 计入)。`MIN_RECORD_LISTENED_SECONDS = 60` |
| [playbackQueue.ts](file:///c:/BottleMusic/ui/src/api/playbackQueue.ts) | **已废弃**(@deprecated Phase 2,生产用 PlaybackCommandCoordinator) | `enqueueQueueCommand`、`playAll` / `playPersonalFm` / `addToQueue` / `removeFromQueue` / `clearQueue` |
| [playbackCommandCoordinator.ts](file:///c:/BottleMusic/ui/src/api/playbackCommandCoordinator.ts) | 生产用「合并邮箱(coalescing mailbox)」命令协调器 | `dispatch` / `dispose`(barrier stop)/ `shutdown`(退出,不清队列)/ `detach`(HMR,纯 invalidate)/ `playInterruptible`(race playPromise vs interruptPromise)/ `applyNav` / `applyRemove`。合并策略:next/prev delta merge、selectTrack latest-wins、seek latest-wins、clearQueue barrier、removeTrack serial FIFO、ended 每 epoch 一次、switchQuality 互斥事务、togglePlay 等当前 settle |
| [playbackPhase.ts](file:///c:/BottleMusic/ui/src/api/playbackPhase.ts) | 纯状态机助手 | `PlaybackPhase`('idle'/'resolving'/'loading'/'playing'/'paused'/'recovering'/'error')、`LEGAL` 边表、`canTransition`、`transitionPhase`、`flagsFromPhase` |
| [playbackDiagnostics.ts](file:///c:/BottleMusic/ui/src/api/playbackDiagnostics.ts) | 播放诊断环形缓冲 | `markActivity`、`copyAsText`、`reset`。`DiagKind = track_switch|url_resolve|media_event|proxy_prep|fm_fetch|potential_stall`,容量 200,URL 自动脱敏,`waiting`/`stalled` 触发 5 秒 stall 计时器 |

#### 6.3.2 音频后端与均衡器

| 文件 | 职责 | 关键导出 |
|---|---|---|
| [playerBackend.ts](file:///c:/BottleMusic/ui/src/api/playerBackend.ts) | 后端抽象接口 | `PlayerBackend` 接口(`kind: 'html5'`)、方法 `initialize / playUrl / switchUrl / hasSource / pause / resume / stop / seek / setVolume / setRate / getState / shutdown / onEvent`;`PlaybackEvent` 类型 `position / state / ended / error` |
| [html5Backend.ts](file:///c:/BottleMusic/ui/src/api/html5Backend.ts) | HTML5 `<audio>` 后端实现 | `Html5AudioBackend`。source-lease(`sourceLeaseId`)+ transitionSeq ownership 双重保护。`playUrl` / `switchUrl`:set src → play → `initEq`(后置 attach)。`onEvent`:timeupdate/play/pause/ended/error/waiting/stalled/suspend/abort → PlaybackEvent + diagnostics 记录。`waitForMetadata`:500ms 超时 |
| [webAudioEq.ts](file:///c:/BottleMusic/ui/src/api/webAudioEq.ts) | Web Audio EQ 图形容器(Phase 2 重设计) | `WebAudioEq` 类:`init / attachSource / disconnectSource / setBand / setEnabled / setVolume / awaitReady / resume / close / enterDegradation / recoverFromDegradation`。拓扑:`captureStream → MediaStreamSource → AudioWorkletNode('eq-processor') → GainNode → destination`。**绝不** `createMediaElementSource`。降级/恢复:`GAIN_CROSSFADE_MS = 50` |
| [eqWorkletProcessor.ts](file:///c:/BottleMusic/ui/src/api/eqWorkletProcessor.ts) | AudioWorklet 纯 DSP + Blob URL 加载器 | `computePeakingCoeffs`(RBJ cookbook,Q = 1/√2)、`clampFreq`(20Hz..0.95·Nyquist)、`cascadeBiquad`(Direct Form I Transposed)、`EQ_PROCESSOR_SOURCE` 字符串、`loadEqWorklet`(`URL.createObjectURL(new Blob([src]))`)、`WorkletLoadError`。10 频段 × 2 通道;dezipper 0.1/block ≈ 50ms |
| [equalizerConfig.ts](file:///c:/BottleMusic/ui/src/api/equalizerConfig.ts) | EQ 配置常量与预设 | `EQ_MIN_GAIN_DB = -6`、`EQ_MAX_GAIN_DB = 6`;`EQ_BANDS`(10 频段:31/62/125/250/500/1K/2K/4K/8K/16K Hz);`EQ_PRESETS`:Flat / Bass Boost / Vocal / Rock / Harman Kardon / 125Hz Test |
| [usePlayerEq.ts](file:///c:/BottleMusic/ui/src/api/usePlayerEq.ts) | 从 playerStore 抽取 EQ 叶子节点的工厂 | `createPlayerEq(getStore)` → `eqState`(available / reason / retryFailCount / retryDisabled)、`initWebAudioEQ`、`setWebAudioEqVolume / Band / Enabled`、`retryEq`(3 次失败 → 禁用)、`makeBackendEqHooks` |

#### 6.3.3 网络与后端 FFI

| 文件 | 职责 | 关键导出 |
|---|---|---|
| [backend.ts](file:///c:/BottleMusic/ui/src/api/backend.ts) | 前端 → C++ FFI 入口(封装 Tauri `native_request`) | `FRONTEND_TIMEOUT_MS = 14_000`;熔断桶 `playback / lyric / search / generic`(5 次失败、30s 打开);`pickBucket`(longest-prefix);`ipcRequest`、`apiGet / apiPost`(**不重试**,重试归 C++ HttpClient);`ping`、`isCircuitOpen`、`backendHealth`(/healthz) |
| [circuitBreaker.ts](file:///c:/BottleMusic/ui/src/api/circuitBreaker.ts) | 通用熔断器 | `CircuitBreaker` 类(failures、openedAt、threshold、durationMs),方法 `isClosed` / `recordSuccess` / `recordFailure` |
| [audioProxy.ts](file:///c:/BottleMusic/ui/src/api/audioProxy.ts) | 调用 Tauri `audio_proxy_url` 注册本地音频代理 | `prepareAudioSourceUrl(url)` → 成功返回 `{ url, crossOriginSafe: true }`,失败回退 `{ url, crossOriginSafe: false }` |
| [songUrlResolver.ts](file:///c:/BottleMusic/ui/src/api/songUrlResolver.ts) | 歌曲 URL 解析 | `resolveTrack(track, quality)` → `apiGet<ResolveTrackResult>('/song/url', { hash, album_id, album_audio_id, quality })` |
| [vipResolver.ts](file:///c:/BottleMusic/ui/src/api/vipResolver.ts) | 纯函数解析 KuGou `get_union_vip` VIP 信息 | `parseVipEndTime`(`"YYYY-MM-DD HH:MM:SS"` → ms)、`resolveVip(d, nowMs)` 扫描 `busi_vip[]` 找 SVIP + is_vip=1 + 未过期项,取最晚未过期结束时间;0(永久/不可解析)排最高 |

#### 6.3.4 主题与外观

| 文件 | 职责 | 关键导出 |
|---|---|---|
| [themeStore.ts](file:///c:/BottleMusic/ui/src/api/themeStore.ts) | `appearanceStore` 的薄封装 | `useThemeStore()` → `{ skinId, mode, setSkin, setMode, init }` |
| [appearanceStore.ts](file:///c:/BottleMusic/ui/src/api/appearanceStore.ts) | 外观真正存储 | `AppearanceSettings { skin: 'aurora'\|'newsprint', mode: 'light'\|'dark', accent, compactList, lyricAlign: 'left'\|'center' }`、`TOKEN_ACCENTS`(按 skin×mode)、`applyToDom`(设置 `data-skin / data-mode / data-compactList / data-lyricAlign`、切换 `.compact` / `.lyric-left`、写入 `--accent` CSS 变量) |

#### 6.3.5 动效

| 文件 | 职责 | 关键导出 |
|---|---|---|
| [motion.ts](file:///c:/BottleMusic/ui/src/api/motion.ts) | GSAP 动效助手集合 | `animateCountUp`、`animateBarHeight`、`crossfadeTheme`(dip 到 opacity 0.25 → swap → restore)、`transitionEnter` / `transitionLeave`(Vue Transition JS hooks,kill-safe via `transitionSession`)、`pressBounceDown` / `pressBounceUp`(Q-bounce)、`animateStagger`、`startAmbientMotion`(呼吸)、`startVinylSpin`(黑胶,timeScale ramp 0↔1)、`isReducedMotion()` |
| [motionProfiles.ts](file:///c:/BottleMusic/ui/src/api/motionProfiles.ts) | 按皮肤定义动效配置 | `auroraProfile`(pageEnter 0.56s expo.out fromY 28、ambient enabled、vinyl 24s spin)、`newsprintProfile`(pageEnter 0.24s power3.out fromY 8、ambient/vinyl disabled、所有粒子 scalar 0)、`getMotionProfile(skinId)` |

#### 6.3.6 收藏与历史

| 文件 | 职责 | 关键导出 |
|---|---|---|
| [favorite.ts](file:///c:/BottleMusic/ui/src/api/favorite.ts) | 仅 `/playlist/tracks/add` 与 `/del` 适配器 | `buildTrackInfo`(`name\|hash\|album_id\|mixsongid`,`\|` 转义)、`addTrackToPlaylist`、`removeTrackFromPlaylist`(使用数字 `fileid`) |
| [favoriteStore.ts](file:///c:/BottleMusic/ui/src/api/favoriteStore.ts) | 收藏领域权威 store | `normalizePlaylists`、`isLikedPlaylistName` 正则(我喜欢\|喜欢的音乐\|我的最爱\|我的收藏\|favorites\|liked songs)、`fetchUserPlaylists`、`setFavorite(track, favorite)` → `SetFavoriteResult`(confirmed / pending / local / anonymous / failed)、`reconcile`、`flushOutbox`、`sync`(single-flight)。乐观应用 + 每 hash lastOpId + pendingIntent + accountEpoch;自动 hook `window.online` + `visibilitychange`(30s 节流) |
| [favoriteRepository.ts](file:///c:/BottleMusic/ui/src/api/favoriteRepository.ts) | 按用户持久化层 | `LikedPlaylistInfo`、`FavoriteOp`;键名 `bm_fav_liked_<uid>` / `bm_fav_outbox_<uid>` / `bm_fav_legacy_migrated_<uid>` / `bm_fav_anonymous` / `player_favorite_markers`(legacy) |
| [favoriteMarkers.ts](file:///c:/BottleMusic/ui/src/api/favoriteMarkers.ts) | 兼容缓存投影(薄封装 `favoriteStore`) | 再导出 `isLikedPlaylistName`、`markFavorite / markFavorites / unmarkFavorite`、`reloadFavoriteMarkers`、`favoriteMarkersReadonly` |
| [recentPlayedStore.ts](file:///c:/BottleMusic/ui/src/api/recentPlayedStore.ts) | 最近播放 store | `RecentPlayedStore` 类、`MAX_RECENT_ENTRIES = 100`、`recordRecentPlayed`(按 FileHash 去重、头插)、`mergeRemote`(latest playedAt 胜出)、`reset` |
| [playHistory.ts](file:///c:/BottleMusic/ui/src/api/playHistory.ts) | 上传播放历史到 KuGou | `uploadPlayHistory(track)` → `apiPost('/playhistory/upload', { mxid, time, pc: 1 })`。静默失败 |

#### 6.3.7 首页与 FM

| 文件 | 职责 | 关键导出 |
|---|---|---|
| [homeFeedStore.ts](file:///c:/BottleMusic/ui/src/api/homeFeedStore.ts) | 首页数据 store | `useHomeFeedStore()`;区段 `daily / playlists / albums`(reactive,每区段独立 generation);`loadDailyItems`、`loadPlaylistItems`、`ensureLoaded`(跨日轮换 via `dailyLoadedDay`)、`refresh`、`retrySection` |
| [homeEnterSession.ts](file:///c:/BottleMusic/ui/src/api/homeEnterSession.ts) | 首页进入模式判定 | `nextHomeEnterMode()`(首次 `'cold'`,之后 `'return'`) |
| [fmSession.ts](file:///c:/BottleMusic/ui/src/api/fmSession.ts) | 私人 FM 推荐追加 | `appendPersonalFmRecommendations(deps, options)`、`disposeFmSession`、`getFmSessionState`。`AUTO_RETRY_DELAYS_MS = [1s, 3s, 10s]` 有界重试;按 FileHash 去重 |

#### 6.3.8 过渡与歌词

| 文件 | 职责 | 关键导出 |
|---|---|---|
| [transitionSession.ts](file:///c:/BottleMusic/ui/src/api/transitionSession.ts) | Vue Transition JS hook 的 kill-safe 会话 | `beginTransitionSession(el, phase, done)` → `{ complete, interrupt }`;快照样式,interrupt 时还原;`settleActiveTransitionSessions` 用于页面错误恢复 |
| [lyricFocusStore.ts](file:///c:/BottleMusic/ui/src/api/lyricFocusStore.ts) | 歌词聚焦模式 store | `useLyricFocusStore()`;`LyricFocusMode = 'readable' \| 'stage'`;`STORAGE_KEY = tweak_lyric_focus`;`applyToDom`、`setMode / toggle / init` |
| [lyricFullscreen.ts](file:///c:/BottleMusic/ui/src/api/lyricFullscreen.ts) | 歌词全屏共享 ref | 模块级 `ref(false)`、`setLyricFullscreen(value)`。LyricView 写入、App.vue 读取 |
| [useLyricFollow.ts](file:///c:/BottleMusic/ui/src/api/useLyricFollow.ts) | 歌词自动跟随 composable | `useLyricFollow({ activeIndex, scrollToLine, now })`;`IDLE_RESUME_MS = 900`;`autoFollowing`、`manualScrollUntil`、`trackKey`、`onUserScroll`(暂停跟随、900ms 后恢复)、`resumeFollow`、`snapToActive`、`resetForTrack` |
| [lyricsResource.ts](file:///c:/BottleMusic/ui/src/api/lyricsResource.ts) | 歌词资源加载 | `LyricsResource` 类(reactive state `{ loading, lines, error }`)、`load(track)`(generation 守卫)、`retry`、`dispose`;`LyricLine { time, text }` |

#### 6.3.9 用户与杂项

| 文件 | 职责 | 关键导出 |
|---|---|---|
| [userStore.ts](file:///c:/BottleMusic/ui/src/api/userStore.ts) | 用户状态 store | reactive `userStore`(isLoggedIn / userId / username / avatar / vipLevel / vipType / isVip / vipEndDate / loading / claimMessage);`checkLoginStatus`(/user/detail → `favoriteStore.onLogin` → /register/dev → /user/vip/detail → `resolveVip`);`claimVip`;`logoutLocal`。**非 VIP 时自动通过 /youth/listen/song 领取每日 VIP** |
| [safeStorage.ts](file:///c:/BottleMusic/ui/src/api/safeStorage.ts) | 安全的 localStorage 数字读取 | `loadNumber(key, fallback, min, max)`,带 NaN / clamp 守卫 |
| [skippedVersion.ts](file:///c:/BottleMusic/ui/src/api/skippedVersion.ts) | 跳过的更新版本号共享 ref | `_skipped` ref + localStorage `tweak_skipped_version`;`getSkippedVersion / setSkippedVersion / useSkippedVersion` |
| [osMediaBridge.ts](file:///c:/BottleMusic/ui/src/api/osMediaBridge.ts) | T1a OS Media Session 桥接 | `bindOsMediaBridge(deps)`(invoke `os_media_bind`、监听 `os-media-button` 事件、watch playerStore 字段、推送 `os_media_set_now_playing / set_playback_status / set_enabled_controls`)、`handleOsMediaButton`(路由 Play / Pause / PlayPause / Next / Prev)、`unbindOsMediaBridge`。非 Tauri 静默降级 |
| [audioLevelMonitor.ts](file:///c:/BottleMusic/ui/src/api/audioLevelMonitor.ts) | 模块级单例音频电平监测 | `createAudioLevelMonitor(audio)`;`computeRms`(Uint8Array → 0..1)。**模块级单例**图形(永不关闭,只切 rAF);`captureStream → AnalyserNode`(仅分析,绝不连 destination);快攻击 0.3、慢衰减 0.08 |
| [coverFlight.ts](file:///c:/BottleMusic/ui/src/api/coverFlight.ts) | 封面飞入 dock 动效 | `flyCoverToDock(fromEl, imgUrl)` via GSAP Flip。Ghost `<img class="aurora-cover-ghost">` 从源 rect 飞到 `.aurora-pb-cover`(0.55s expo.inOut) |
| [normalizer.ts](file:///c:/BottleMusic/ui/src/api/normalizer.ts) | Track 规范化与封面提取 | `Track` 接口、`normalizeTrack(raw)`(遍历多种字段名;时长统一为秒;从多字段抽取封面 URL,`{size}` → 400)、`fetchCoverImage(hash, albumAudioId)` via /images/audio |
| [playerPersistence.ts](file:///c:/BottleMusic/ui/src/api/playerPersistence.ts) | 播放队列持久化 | `loadJSON`、`bindQueuePersistence(getter)`、`flushSaveQueue`、`saveQueue`(500ms debounce);`beforeunload` flush |

### 6.4 视图(Views)

#### 顶层视图

| 文件 | 职责 |
|---|---|
| [HomeView.vue](file:///c:/BottleMusic/ui/src/views/HomeView.vue) | 首页外壳,按皮肤切 AuroraHome / NewsprintHome;KeepAlive 下按皮肤保存/恢复滚动位置;Aurora 激活时 `enterMode = nextHomeEnterMode()` |
| [LoginView.vue](file:///c:/BottleMusic/ui/src/views/LoginView.vue) | 二维码登录。流程 `/login/qr/key` → `/login/qr/check` 轮询(2s 基础、10s 上限、5 次失败 → 网络错误)。状态 0/1/2/3/4/-1。QR 图失败回退到本地 `QRCode.toDataURL` |
| [SearchView.vue](file:///c:/BottleMusic/ui/src/views/SearchView.vue) | `/search` 结果表格。`searchGeneration` 竞态守卫;25/页;`AddToPlaylistModal` 收藏流 |
| [PlaylistView.vue](file:///c:/BottleMusic/ui/src/views/PlaylistView.vue) | `/playlist/:id` 曲目列表。`playlistGeneration` 守卫;50/页;`isLikedPlaylistName` → `favoriteStore.hydrateLikedPage(songs)` |
| [LyricView.vue](file:///c:/BottleMusic/ui/src/views/LyricView.vue) | 歌词外壳。无 currentTrack 时显示空状态;`useLyricStage` composable;按皮肤切 AuroraLyricStage / NewsprintLyricStage |
| [HistoryView.vue](file:///c:/BottleMusic/ui/src/views/HistoryView.vue) | 本地优先历史。`recentPlayedStore.mergeRemote(remoteEntries)`;`loadRemoteHistory` 仅登录时调用 |
| [StatsView.vue](file:///c:/BottleMusic/ui/src/views/StatsView.vue) | 统计页。范围 1d / 7d / 30d;调用 `stats_record_play / get_summary / get_top / get_timeline`;`animateCountUp` / `animateBarHeight`;DeepSeek AI 分析区段 |
| [EqualizerView.vue](file:///c:/BottleMusic/ui/src/views/EqualizerView.vue) | `EqualizerPanel` standalone 变体;`applyEffect(name)` 应用预设 |
| [SettingsView.vue](file:///c:/BottleMusic/ui/src/views/SettingsView.vue) | 设置页。区段 appearance/device/vip/update/storage/diagnostics;`crossfadeTheme`;`@tauri-apps/plugin-updater` `check()` / `relaunch()` |
| [VisualizerView.vue](file:///c:/BottleMusic/ui/src/views/VisualizerView.vue) | Spectral Horizon 环形频谱。`createAudioLevelMonitor` tap;`startVinylSpin`;Canvas 2D,64 bars,DPR cap 2 |

#### home/ 子目录

| 文件 | 职责 |
|---|---|
| [homeViewModel.ts](file:///c:/BottleMusic/ui/src/views/home/homeViewModel.ts) | `useHomeViewModel()` 计算 `HomeViewModel`。`heroTrack = currentTrack ?? daily[0]`;`queuePreview` 12 项窗口居中 current |
| [AuroraHome.vue](file:///c:/BottleMusic/ui/src/views/home/AuroraHome.vue) | Aurora 首页。`startVinylSpin`、`createAudioLevelMonitor`、`flyCoverToDock`、`animateStagger`、`AuroraAtmosphere`;Phosphor 图标 |
| [NewsprintHome.vue](file:///c:/BottleMusic/ui/src/views/home/NewsprintHome.vue) | 经典晚报版式。`timeOfDayPhrase`;Lucide 图标;`recommendations = dailyTracks.slice(0, 10)` |
| [AuroraAtmosphere.vue](file:///c:/BottleMusic/ui/src/views/home/AuroraAtmosphere.vue) | Canvas 2D 锥形光斑尘埃 + 静态 wash。非音频、非 WebGL;KeepAlive-safe rAF;`CAP_PAUSED = 30`、`CAP_PLAYING = 60`、`DPR_CAP = 2` |

#### lyric/ 子目录

| 文件 | 职责 |
|---|---|
| [useLyricStage.ts](file:///c:/BottleMusic/ui/src/views/lyric/useLyricStage.ts) | 歌词舞台 composable。`parseLrc`(base64 解码兜底、word-timing tag 剥离、`[mm:ss.xx]` / `[mm:ss:xx]`);`fetchLyrics` via /search/lyric → /lyric;`useLyricStage()` 返回 `{ model, commands }`;`scheduleEnterFollow`(instant + 480ms smooth);`seekToLine` |
| [useAutoHideControls.ts](file:///c:/BottleMusic/ui/src/views/lyric/useAutoHideControls.ts) | 自动隐藏控件 composable。`useAutoHideControls({ active, onEscape, idleMs = 1800 })` → `visible`、`onPointerMove / onFocusIn / onFocusOut / dispose` |
| [AuroraLyricStage.vue](file:///c:/BottleMusic/ui/src/views/lyric/AuroraLyricStage.vue) | Aurora 歌词舞台。`useAutoHideControls` 全屏;`CoverWebGLParticles`、`AuroraPlaylistShelf`、`PlayerProgress`、`FullscreenWindowControls` |
| [NewsprintLyricStage.vue](file:///c:/BottleMusic/ui/src/views/lyric/NewsprintLyricStage.vue) | Newsprint 歌词舞台。Lucide 图标;`getMotionProfile('newsprint')`;`useAutoHideControls`;`PlayerProgress` |
| [CoverWebGLParticles.vue](file:///c:/BottleMusic/ui/src/views/lyric/CoverWebGLParticles.vue) | 原生 WebGL2/WebGL1 粒子(无 Three.js)。`COUNT = 96`;uniforms: time / playing / timeScale / motionEnabled |
| [LyricFollowFooter.vue](file:///c:/BottleMusic/ui/src/views/lyric/LyricFollowFooter.vue) | 十字准星按钮。Aurora 用 `PhCrosshairSimple`,Newsprint 用 `LocateFixed` |
| [AuroraPlaylistShelf.vue](file:///c:/BottleMusic/ui/src/views/lyric/AuroraPlaylistShelf.vue) | CSS 3D 播放列表货架,仅全屏,teleport 到 body;wheel/drag 浏览、click 播放;GSAP |

### 6.5 组件(Components)

#### 顶层

| 文件 | 职责 |
|---|---|
| [Sidebar.vue](file:///c:/BottleMusic/ui/src/components/Sidebar.vue) | 导航项(home/stats/history/equalizer/visualizer);`@tauri-apps/plugin-updater` `check()` → updateAvailable badge;`useSkippedVersion` reactive watch;`loadUserPlaylists` |
| [Topbar.vue](file:///c:/BottleMusic/ui/src/components/Topbar.vue) | 搜索框 + back/forward。`searchVariant`: newsprint='legacy'、aurora='command' |
| [PlayerBar.vue](file:///c:/BottleMusic/ui/src/components/PlayerBar.vue) | 按皮肤切 AuroraPlayerBar / NewsprintPlayerBar;`usePlayerControls`;`AddToPlaylistModal` slot |
| [EqualizerPanel.vue](file:///c:/BottleMusic/ui/src/components/EqualizerPanel.vue) | 10 频段滑块。`variant: 'collapsible' \| 'standalone'`、`showPresets`;`onSliderInput` → `setWebAudioEqBand` |
| [QueuePanel.vue](file:///c:/BottleMusic/ui/src/components/QueuePanel.vue) | 队列过滤(按 SongName/SingerName 子串);`pendingCoverFetches` Map + `coverFetchGeneration` 竞态守卫 |
| [AddToPlaylistModal.vue](file:///c:/BottleMusic/ui/src/components/AddToPlaylistModal.vue) | 显示时 `getUserPlaylists`;选择时 `addTrackToPlaylist`;`transitionEnter/Leave` |

#### player/ 子目录

| 文件 | 职责 |
|---|---|
| [AuroraPlayerBar.vue](file:///c:/BottleMusic/ui/src/components/player/AuroraPlayerBar.vue) | Aurora 播放栏。Phosphor 图标;`AuroraDockParticles`;`pressBounceDown/Up`;quality chip |
| [NewsprintPlayerBar.vue](file:///c:/BottleMusic/ui/src/components/player/NewsprintPlayerBar.vue) | Newsprint 播放栏。Lucide 图标;`PlayerProgress` |
| [PlayerProgress.vue](file:///c:/BottleMusic/ui/src/components/player/PlayerProgress.vue) | 进度条。props currentTime/duration/buffered;`progressPct`/`bufferedPct` 计算;`formatTime`;emits seek |
| [usePlayerControls.ts](file:///c:/BottleMusic/ui/src/components/player/usePlayerControls.ts) | 播放控制器工厂。`usePlayerControls({ activeView, onNavigate })` → `PlayerController`(currentTrack、isPlaying、isLoading、currentTime、duration、volume、loopMode、quality、coverUrl、progressPercent、togglePlay/next/prev/seek/setVolume/...) |
| [AuroraDockParticles.vue](file:///c:/BottleMusic/ui/src/components/player/AuroraDockParticles.vue) | dock 粒子,按进度(非音频)驱动。`CAP_PAUSED = 32`、`CAP_PLAYING = 44` |

#### primitives/ 子目录(皮肤无关原语)

| 文件 | 职责 |
|---|---|
| [SkinButton.vue](file:///c:/BottleMusic/ui/src/components/primitives/SkinButton.vue) | 通用按钮。variant `primary/secondary/ghost`、size `sm/md`、disabled、active |
| [SkinEmptyState.vue](file:///c:/BottleMusic/ui/src/components/primitives/SkinEmptyState.vue) | 空状态。`message` prop + `action` slot |
| [SkinListRow.vue](file:///c:/BottleMusic/ui/src/components/primitives/SkinListRow.vue) | 列表行。index/title/subtitle + cover/meta slot |
| [SkinPageHeader.vue](file:///c:/BottleMusic/ui/src/components/primitives/SkinPageHeader.vue) | 页面标题。title/kicker/subtitle + `actions` slot |

#### shell/ 子目录

| 文件 | 职责 |
|---|---|
| [AuroraShell.vue](file:///c:/BottleMusic/ui/src/components/shell/AuroraShell.vue) | Aurora 外壳。`data-shell="aurora"` `data-layout="immersive"`;titlebar drag-region + dblclick toggleMaximize;`WindowControls`;slots: banner/sidebar/topbar/extras/playerbar + default |
| [NewsprintShell.vue](file:///c:/BottleMusic/ui/src/components/shell/NewsprintShell.vue) | Newsprint 外壳。`data-shell="newsprint"`;程序化背景层(paper-base/fibers/grain/vignette) |
| [WindowControls.vue](file:///c:/BottleMusic/ui/src/components/shell/WindowControls.vue) | 标题栏控件。minimize/toggleMaximize/close via `getCurrentWindow()` |
| [FullscreenWindowControls.vue](file:///c:/BottleMusic/ui/src/components/shell/FullscreenWindowControls.vue) | 全屏控件。`showMinimize`/`showExit` props;minimize + exitFullscreen(`setLyricFullscreen(false)`) |
| [PageRecoveryBoundary.vue](file:///c:/BottleMusic/ui/src/components/shell/PageRecoveryBoundary.vue) | 页面错误恢复边界。`onErrorCaptured` → settle 过渡会话、取消 page transition;`returnHome`/`retryCurrentPage` |

### 6.6 导航(Navigation)

| 文件 | 职责 |
|---|---|
| [router.ts](file:///c:/BottleMusic/ui/src/navigation/router.ts) | 路由工厂。`createAppRouter(history)` 创建 router,安装 lifecycle;导出 `router = createAppRouter(createWebHistory())` |
| [routes.ts](file:///c:/BottleMusic/ui/src/navigation/routes.ts) | 路由表。`routeNames`(home/stats/history/equalizer/settings/search/playlist/lyric/login/visualizer);`routeRecords`:`/`(keepAlive)、`/stats`、`/history`、`/equalizer`、`/settings`、`/search`(props q)、`/playlist/:id`(props id+name)、`/lyric`、`/login`、`/visualizer` |
| [navigationLifecycle.ts](file:///c:/BottleMusic/ui/src/navigation/navigationLifecycle.ts) | 导航生命周期。`activePageTransitions` Set;`registerPageTransition`/`unregisterPageTransition`/`cancelPageTransition`(settle sessions、kill gsap、clear styles);`installNavigationLifecycle(router)`:`beforeEach` 在离开 lyric 时清除 lyric fullscreen、取消 page transition |

### 6.7 样式(Styles)

| 文件 | 职责 |
|---|---|
| [tokens.css](file:///c:/BottleMusic/ui/src/styles/tokens.css) | 皮肤 token 系统,**4 个显式选择器块**(2 皮肤 × 2 模式),dark 值**不**由 opacity 派生:`aurora/light` → `--accent: #18875b`、`aurora/dark` → `--accent: #62d6a2`、`newsprint/light` → `--accent: #a8311b`、`newsprint/dark` → `--accent: #c4391e`。Token:`--app-bg` / `--surface-1` / `--surface-2` / `--surface-elevated` / `--text-primary` / `--text-secondary` / `--text-muted` / `--accent` / `--focus-ring` / `--border-subtle` / `--progress-*` |
| [progress.css](file:///c:/BottleMusic/ui/src/styles/progress.css) | 进度条样式。`.progress-root/track/buffered/fill/thumb/hover-tip`;全部 theming 走 tokens.css 的 CSS 变量;reduced-motion 下 `transition: none` |
| [skins/aurora.css](file:///c:/BottleMusic/ui/src/styles/skins/aurora.css) | Aurora 沉浸式 3-zone 桌面外壳布局。`.app[data-shell="aurora"]` grid `clamp(232px,16vw,252px) minmax(0,1fr)` × `32px auto minmax(0,1fr) 104px`;titlebar/topbar 融合无边框;sidebar pill 激活态;中窄屏 sidebar 64px 图标 rail;全屏模式 grid 折叠为 1fr |
| [skins/newsprint.css](file:///c:/BottleMusic/ui/src/styles/skins/newsprint.css) | Newsprint 经典晚报版式。`.app[data-shell="newsprint"]` grid `240px 1fr` × `32px auto minmax(0,1fr) 76px`;topbar editorial rule;sidebar 激活态 `border-left: 3px solid` + serif 字体 + 编号 `nav-index`;SkinButton 无圆角、serif;SkinListRow 点状分隔 |

### 6.8 Tauri Rust 外壳

#### Cargo.toml

文件:[ui/src-tauri/Cargo.toml](file:///c:/BottleMusic/ui/src-tauri/Cargo.toml)

- 包 `ui` v1.0.0,edition 2021,MIT,lib 名 `ui_lib`,crate-type `["staticlib", "cdylib", "rlib"]`
- **dependencies**:`tauri 2`(无默认 feature)、`tauri-plugin-opener/updater/process 2`、`tauri-plugin-global-shortcut 2`(optional,behind `desktop-shell`)、`serde 1`(derive)、`serde_json 1`、`tokio 1`(rt/rt-multi-thread/macros/time/net/io-util)、`sysinfo 0.30`、`libloading 0.9.0`、`chrono 0.4`、`reqwest 0.12`(json+stream)、`futures-util 0.3`、`getrandom 0.2`
- **features**:`default = ["desktop-shell"]`,`desktop-shell = ["tauri/tray-icon", "dep:tauri-plugin-global-shortcut"]`

#### tauri.conf.json(关键配置)

文件:[ui/src-tauri/tauri.conf.json](file:///c:/BottleMusic/ui/src-tauri/tauri.conf.json)

- **productName** `BottleMusic`,**identifier** `com.bottlemusic.app`,**version** `1.0.0`
- **build**:`beforeDevCommand: pnpm dev`、`devUrl: http://localhost:1420`、`beforeBuildCommand: pnpm build`、`frontendDist: ../dist`
- **window**:1280×820,min 1024×700,`resizable: true`、`fullscreen: false`、**`decorations: false`**(自绘标题栏)
- **CSP(严格)**:
  - `default-src 'self'`
  - `connect-src ipc: http://ipc.localhost http://127.0.0.1:*`
  - `script-src 'self' blob:`
  - `media-src 'self' blob: http: https: http://127.0.0.1:*`
  - `worker-src 'self' blob:`(AudioWorklet Blob URL 必需)
  - `object-src 'none'`、`base-uri 'none'`、`frame-src 'none'`、`form-action 'none'`
  - `devCsp` 额外放行 `localhost:1420/1421` 与 ws
  - `headers`: `X-Content-Type-Options: nosniff`、`Permissions-Policy: camera=(), microphone=(), geolocation=()`
- **bundle**:`createUpdaterArtifacts: true`、`targets: "all"`、`publisher: Ningbottle`、`copyright © 2026`;`resource` 含 `libs/EchoCAPI.dll → EchoCAPI.dll` 与 `libs/sqlite3.dll → sqlite3.dll`;`windows.nsis.installMode: currentUser`
- **plugins.updater**:endpoint `https://github.com/Ningbottle/BottlePlayer/releases/latest/download/latest.json` + minisign pubkey

#### capabilities/default.json(最小权限)

文件:[ui/src-tauri/capabilities/default.json](file:///c:/BottleMusic/ui/src-tauri/capabilities/default.json)

- **windows**: `["main"]`
- **permissions**:
  - `core:event:allow-listen` / `allow-unlisten`
  - `core:window:allow-start-dragging` / `allow-minimize` / `allow-toggle-maximize` / `allow-close`
  - `core:tray:default`、`core:menu:default`
  - `opener:allow-open-url`(仅允许 `https://m.kugou.com/*`)
  - `updater:allow-check` / `allow-download-and-install`
  - `process:allow-restart`

#### Rust 源文件

| 文件 | 职责 | 关键内容 |
|---|---|---|
| [main.rs](file:///c:/BottleMusic/ui/src-tauri/src/main.rs) | Windows 入口,避免 release 控制台窗口 | `#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]`;`fn main() { ui_lib::run() }` |
| [lib.rs](file:///c:/BottleMusic/ui/src-tauri/src/lib.rs) | Tauri app 主体 | 模块:`ai_analysis / audio_proxy / backend_api / os_media_session / stats`。Tauri 命令:`ping() → "pong"`、`backend_base_url() → "native-ipc"`、`get_memory_usage() → u64`(sysinfo)、`async fn native_request(method, path, query_json, headers_json, body) -> Result<String>`(按 `deadline_for_path` 计算 tokio timeout,`spawn_blocking` 调 `backend_api::handle_request`,超时返回 `"request_deadline"`)。`deadline_for_path`(基于 build.rs 生成的 `deadlines` 模块,源自 C++ `RequestDeadlines.h`):`/song/url → SongUrl`、`/images/ → Image`、`/login/qr/ → LoginPoll`、`/search → Search`、`/playlist|/rank|/top/|/album|/artist → Playlist`、其他 → Generic。`run()` setup:注册插件 → `backend_api::set_app_handle` + `os_media_session::install_os_integrations` → `audio_proxy::bind_listener()` + `tauri::async_runtime::spawn(audio_proxy::serve(...))` → EchoCAPI.dll 加载(3 候选路径:resource_dir / exe_dir / `CARGO_MANIFEST_DIR/../../native/out/{preset}/`)→ `on_window_event` CloseRequested → `backend_api::shutdown_c_api()` → `invoke_handler` 注册全部命令 |
| [backend_api.rs](file:///c:/BottleMusic/ui/src-tauri/src/backend_api.rs) | FFI 桥接 C++ EchoCAPI.dll | `APP_HANDLE: OnceLock<AppHandle>`;`CApiHandle` 结构持 `Library` + 函数指针(handle_req/free_str/shutdown/6 个 stats);`C_API_HANDLE: OnceLock<RwLock<Option<CApiHandle>>>`(读锁允许多并发,写锁 shutdown 等所有读锁释放);`init_with_paths` 先解析所有符号 → 调 init → 失败 shutdown + 丢弃 Library;`shutdown_c_api` **有界 shutdown**(最多 5s `try_write`,超时不 blocking,shutdown_status != 0 时 `std::mem::forget(handle)` 保留 DLL 映射避免 use-after-unload);`handle_request` 构建 CString → 持读锁 → C++ EchoHandleRequest → EchoFreeString 释放。日志:按天 `bottlemusic-YYYYMMDD.log`,候选根 app_data_dir/logs → exe/logs → ./logs |
| [audio_proxy.rs](file:///c:/BottleMusic/ui/src-tauri/src/audio_proxy.rs) | 本地 127.0.0.1 音频流代理 | Tauri 命令 `audio_proxy_url(url, state) -> Result<String>`:注册 URL 返回 `http://127.0.0.1:{port}/audio/{id}`。`AudioProxyState`(`Arc<AudioProxyInner>`,port + `Mutex<HashMap<String, RouteEntry>>`):`register(url)`(限流 `MAX_ROUTES = 128` LRU、`is_supported_audio_url` 检查、随机 32-hex route id)。`bind_listener()`:`StdTcpListener::bind(("127.0.0.1", 0))` + `set_nonblocking(true)`。`handle_client`:OPTIONS 返 204 + CORS → 非 GET 返 405 → `/audio/{id}` 解析 route → 共享 `shared_audio_proxy_client()` 转发(带 Range)→ 流式转发 body。`ResumePlan`:基于 Range/Content-Range 计算断点续传,body 读取失败时 `BODY_RETRY_LIMIT = 2` 重试。`MAX_AUDIO_REDIRECTS = 5`。`is_supported_audio_url`:host 必须是 `imge.kugou.com` 或 `fs.<label>.kugou.com`(防 suffix/trailing 攻击)。`is_allowed_origin`:仅 tauri://localhost、http(s)://tauri.localhost、http://localhost:1420。`append_cors_headers`:精确反射 Origin(绝不 wildcard)。`redact_url_queries`:错误信息 URL query `<redacted>` |
| [stats.rs](file:///c:/BottleMusic/ui/src-tauri/src/stats.rs) | 本地听歌统计 FFI 包装 | 6 个 Tauri 命令(全部经 `backend_api::api_handle` 持读锁调 C++ 符号,结果用 `EchoFreeString` 释放):`stats_record_play(json)` → `EchoStatsRecordPlay`、`stats_get_summary(range)` → `EchoStatsGetSummary`、`stats_get_top(kind,range,limit)` → `EchoStatsGetTop`、`stats_get_timeline(range)` → `EchoStatsGetTimeline`、`stats_get_recent(limit,offset)` → `EchoStatsGetRecent`、`stats_get_recommendations(limit)` → `EchoStatsGetRecommendations` |
| [ai_analysis.rs](file:///c:/BottleMusic/ui/src-tauri/src/ai_analysis.rs) | DeepSeek AI 听歌分析 | `async fn ai_analyze(api_key, stats_json, custom_prompt: Option<String>) -> Result<String>`。空 key 直接报错 `"API key is required"`。`DEEPSEEK_API_URL = https://api.deepseek.com/chat/completions`、`DEEPSEEK_MODEL = deepseek-chat`、`REQUEST_TIMEOUT = 30s`。`shared_ai_client()` 进程级 `OnceLock<reqwest::Client>`。system prompt: "You are a music listening analyst..." |
| [os_media_session.rs](file:///c:/BottleMusic/ui/src-tauri/src/os_media_session.rs) | OS Media Session 桥接(T1a 核心 + T1b/T1c tray + 媒体键) | 类型:`MediaButton`(Play/Pause/PlayPause/Next/Prev)、`PlaybackStatus`(Playing/Paused/Stopped)、`NowPlaying`、`EnabledControls`、`SessionState`。Tauri 命令:`os_media_bind/unbind`、`os_media_set_now_playing`、`set_playback_status`、`set_enabled_controls`(未绑定报 `session_not_bound`)、`os_media_inject_button`(desktop-shell 下 emit `"os-media-button"` 事件)、`os_media_debug_snapshot`。`desktop-shell` feature 下:`install_os_integrations` = `install_tray`(菜单项 播放/暂停/下一首/上一首/显示窗口/退出)+ `install_media_key_shortcuts`(注册 `MediaPlayPause`/`MediaTrackNext`/`MediaTrackPrevious` 全局快捷键) |

### 6.9 前端测试

测试文件分布在三个 `__tests__/` 子目录(vitest jsdom 环境):

- [api/__tests__/](file:///c:/BottleMusic/ui/src/api/__tests__/):播放核心与状态层 — `playbackOrchestrator`、`playbackCommandCoordinator`、`playbackPhase`、`playbackDiagnostics`、`playSessionTracker`、`playbackQueue`、`html5Backend`、`webAudioEq`、`eqWorkletProcessor`、`usePlayerEq`、`playerBackend`、`backend`、`circuitBreaker`、`audioProxy`、`songUrlResolver`、`vipResolver`、`favoriteStore`、`favoriteRepository`、`favoriteMarkers`、`recentPlayedStore`、`playHistory`、`favorite`、`homeFeedStore`、`homeEnterSession`、`fmSession`、`userStore`、`appearanceStore`、`themeStore`、`lyricFocusStore`、`useLyricFollow`、`lyricsResource`、`lyricFullscreen`、`motion`、`motionProfiles`、`transitionSession`、`osMediaBridge`、`audioLevelMonitor`、`coverFlight`、`normalizer`、`playerPersistence`、`safeStorage`、`skippedVersion`、`playerStore`、`backend`、`circuitBreaker`、`releaseSecurity`、`syncVersion`、`musicPlayerHtml`、`trackInfo`
- [components/__tests__/](file:///c:/BottleMusic/ui/src/components/__tests__/):`AuroraPlayerBar`、`NewsprintPlayerBar`、`PlayerProgress`、`AuroraDockParticles`、`usePlayerControls`、`Sidebar`、`Topbar`、`PlayerBar`、`EqualizerPanel`、`QueuePanel`、`AddToPlaylistModal`、`SkinButton`、`SkinEmptyState`、`SkinListRow`、`SkinPageHeader`、`AuroraShell`、`NewsprintShell`、`WindowControls`、`FullscreenWindowControls`、`PageRecoveryBoundary`
- [views/__tests__/](file:///c:/BottleMusic/ui/src/views/__tests__/):`HomeView`、`LoginView`、`SearchView`、`PlaylistView`、`LyricView`、`HistoryView`、`StatsView`、`EqualizerView`、`SettingsView`、`VisualizerView`、`homeViewModel`、`AuroraHome`、`NewsprintHome`、`AuroraAtmosphere`、`useLyricStage`、`useAutoHideControls`、`AuroraLyricStage`、`NewsprintLyricStage`、`CoverWebGLParticles`、`LyricFollowFooter`、`AuroraPlaylistShelf`、`appInit`、`skipVersion`、`AppNetworkBanner`

### 6.10 架构要点速览

- **状态层无 Pinia**:所有 store 是模块级 `reactive` / `ref` 单例,靠 HMR 共享引用(`window.__bottlemusic_audio__`)保证 zombie-audio 安全
- **播放双层协调**:`PlaybackOrchestrator`(transitionSeq supersede)+ `PlaybackCommandCoordinator`(coalescing mailbox)解耦「切换语义」与「命令合并策略」
- **EQ 拓扑安全**:`captureStream → MediaStreamSource → AudioWorkletNode → GainNode → destination`,绝不 `createMediaElementSource`,避免与 `<audio>` 输出双重路由;dezipper 50ms、降级/恢复 50ms crossfade
- **音频代理**:Tauri Rust 侧 127.0.0.1 TCP 代理 KuGou CDN,签名 URL 不进前端 JS;CORS 精确反射、URL query 脱敏、断点续传 2 次重试
- **FFI 安全**:`RwLock<Option<CApiHandle>>` 读多写少,shutdown 5s 有界 try_write,shutdown_status != 0 时 `mem::forget` 保 DLL 映射
- **双皮肤 + 双模式**:4 个显式 token 块(非 opacity 派生 dark);Aurora 沉浸 3-zone grid + 粒子/黑胶/Flip 飞入;Newsprint 报纸版式 + 静态/serif
- **OS Media 桥接**:T1a 桥 playerStore ↔ Tauri 命令;T1b tray 菜单;T1c 全局媒体键;非 Tauri 静默降级
- **HMR 安全**:`__bottlemusic_audio__` 共享元素、`__bottlemusic_player_cleanup__`、单 owner pagehide、coordinator `detach`(纯 seq bump,不跳会话)vs `shutdown`(不清队列)vs `dispose`(barrier stop)三态分离

---

## 7. server/ — Node.js KuGou API 代理层

### 7.1 定位(关键!)

> ⚠ **server/ 是 [MakcRe/KuGouMusicApi](https://github.com/MakcRe/KuGouMusicApi) v1.5.1 的子模块检出,作为 native/ C++ 层的参考实现。生产 BottleMusic 桌面客户端运行时**不**调用 server/,所有 KuGou API 请求走 `EchoCAPI.dll`。server/ 仅用于:
> 1. **参考实现**:C++ `native/core/` 移植时的端点签名/响应形态对照
> 2. **独立 HTTP 代理**:可独立运行(Node/Docker/Vercel/pkg)提供 KuGou API HTTP 表面
> 3. **文档源**:[server/docs/README.md](file:///c:/BottleMusic/server/docs/README.md) 是 133 个接口的权威文档
> 4. **签名 salt 来源**:[server/util/helper.js](file:///c:/BottleMusic/server/util/helper.js) 的盐值必须与 [native/core/Crypto.cpp](file:///c:/BottleMusic/native/core/Crypto.cpp) 字节对齐

### 7.2 包与入口

文件:[server/package.json](file:///c:/BottleMusic/server/package.json)

- **name** `kugoumusicapi`,**version** `1.5.1`,MIT,author Lines
- **main** `main.js`(库入口)、**bin** `./app.js`(CLI 入口)、**types** `./interface.d.ts`
- **engines.node** `>=12`
- **scripts**:`dev`(nodemon index.js)、`start`(node app.js)、`pkgwin/pkglinux/pkglinux-arm64/pkgmacos`(pkg 二进制)、`pkgjs`(esbuild bundle)
- **dependencies**:`axios ^1.1.3`、`big-integer ^1.6.52`、`crypto-js ^4.2.0`、`dotenv ^16.4.5`、`esbuild ^0.25.3`、`express ^4.18.2`、`node-forge ^1.3.3`、`pako ^2.1.0`、`qrcode ^1.5.3`、`safe-decode-uri-component ^1.2.1`、`url ^0.11.4`

#### 入口链

四个候选入口,实际链是 `index.js → app.js → server.js`:

| 文件 | 作用 |
|---|---|
| [index.js](file:///c:/BottleMusic/server/index.js) | 一行 `require('./app')`。Vercel builder 与 pkg 默认入口指向它 |
| [app.js](file:///c:/BottleMusic/server/app.js) | CLI 入口。`#!/usr/bin/env node`。两步:① `runtime.applyCliOverrides()`(解析 `--key=value` CLI 参数)② `require('./server').startService()` |
| [server.js](file:///c:/BottleMusic/server/server.js) | HTTP server 实现。导出 `startService()`、`getModulesDefinitions()`。Boot:express + `trust proxy=true` → CORS middleware → Cookie parser → Device cookie 注入器(确保 `KUGOU_API_PLATFORM/MID/GUID/DEV/MAC` cookies 存在)→ body parsers → 静态文件(public/ 与 docs/)→ apicache 中间件(2 分钟 TTL,仅 200)→ **动态模块加载**(`fs.readdir` over module/,filter `*.js` + `!startsWith('_')`,**reverse** 顺序让后加载覆盖先加载)→ 每路由 handler(合并 query+body+cookies+Authorization 头 → 调 moduleDef.module(query, createRequest)) |
| [main.js](file:///c:/BottleMusic/server/main.js) | 程序化库入口。加载 module/ 全部模块(reverse),每个包装为 `obj[fn] = (data={}) => fileModule({...data, cookie}, createRequest)`,再导出 `{ ...require('./server'), ...require('./util/request'), ...obj }`。让 `require('./main').song_url({hash, cookie})` 直接调用,绕开 HTTP |

#### interface.d.ts — 类型契约

文件:[server/interface.d.ts](file:///c:/BottleMusic/server/interface.d.ts)

```ts
type UseAxios = (config: UseAxiosRequestConfig) => Promise<UseAxiosResponse>;
type UseModule = (req: UseModuleParams, useAxios: UseAxios) => Promise<UseAxiosResponse>;
type EncryptType = 'android' | 'web' | 'register';
interface UseAxiosResponse<T = APIBaseResponse> { status: number; body: T; cookie: string[]; headers?: Record<string, string>; }
```

枚举:`PlaylistAdd`、`TopAlbum`、`SongURLQuality`、`SearchType`、`TopPlaylistCategory`。**这是 C++ native/core/ 移植时镜像的类型契约**。

### 7.3 util/ 层

| 文件 | 职责 | 关键导出 |
|---|---|---|
| [index.js](file:///c:/BottleMusic/server/util/index.js) | 聚合器/Facade | `apiver, appid(useAppid), wx_appid, wx_lite_appid, wx_secret, wx_lite_secret, srcappid, clientver(useClientver), isLite, cryptoAesDecrypt, cryptoAesEncrypt, cryptoMd5, cryptoRSAEncrypt, rsaEncrypt2, cryptoSha1, playlistAesEncrypt, playlistAesDecrypt, createRequest, signKey, signParams, signParamsKey, signCloudKey, signatureAndroidParams, signatureRegisterParams, signatureWebParams, randomString, decodeLyrics, parseCookieString, cookieToJson, publicLiteRasKey, publicRasKey, randomNumber, calculateMid` |
| [request.js](file:///c:/BottleMusic/server/util/request.js) | HTTP 客户端(createRequest) | 单点出口,所有上游 KuGou HTTP 调用必经此。注入默认参数(dfid/mid/uuid=**-**/appid/clientver/clienttime/token/userid)+ Android 头(dfid/clienttime/mid/kg-rc=1/kg-thash=5d816a0/kg-rec=1/kg-rf=B9EDA08A64250DEFFBCADDEE00F8F25F/UA: Android15-...)。签名选择:`'register' → signatureRegisterParams`、`'web' → signatureWebParams`、`'android'(默认) → signatureAndroidParams`。可选 `encryptKey: true` → 加 `params.key = signKey(...)`。代理:`resolveProxy()` 解析 `KUGOU_API_PROXY`。响应:解析 set-cookie,JSON.parse body,upstream `status===0` 或 `error_code!==0` → HTTP 502 reject |
| [crypto.js](file:///c:/BottleMusic/server/util/crypto.js) | KuGou 签名/加密算法 | MD5/SHA1/AES-CBC/PKCS7/RSA(裸 modPow + PKCS1-V1_5) + KuGou 特定 playlist AES 变种。缓存 RSA 公钥。导出:`cryptoMd5/cryptoSha1/cryptoAesEncrypt/cryptoAesDecrypt/cryptoRSAEncrypt/rsaEncrypt2/playlistAesEncrypt/playlistAesDecrypt`。两把硬编码 RSA 公钥:`publicRasKey`(phone)/`publicLiteRasKey`(concept) |
| [helper.js](file:///c:/BottleMusic/server/util/helper.js) | KuGou 签名 salts | 持有官方 Android/Web 客户端使用的**盐值**(下表) |
| [util.js](file:///c:/BottleMusic/server/util/util.js) | 杂项工具 | `randomString/randomNumber/parseCookieString/cookieToJson/decodeLyrics`(KRC 解密:跳 4 字节头 → XOR 16 字节密钥 → pako inflate → UTF-8)/`calculateMid`(MD5 hex → big-integer base-16 → 十进制字符串)/`getGuid` |
| [apicache.js](file:///c:/BottleMusic/server/util/apicache.js) | Express 缓存中间件 | 2 分钟响应缓存,fork 自 Binaryify/NeteaseCloudMusicApi。可插拔后端(memory 或 Redis)。ETag/If-None-Match → 304 短路。性能追踪(hit-rate 数组)。Cache key = `req.hostname + originalUrl`。Bypass headers:`x-apicache-bypass` / `x-apicache-force-fetch` |
| [memory-cache.js](file:///c:/BottleMusic/server/util/memory-cache.js) | TTL 内存存储 | `add(key,value,time,timeoutCallback)`/`delete(key)`/`get(key)`/`getValue(key)`/`clear()` |
| [runtime.js](file:///c:/BottleMusic/server/util/runtime.js) | CLI args & 代理解析器 | `parseCliArgs`/`applyCliOverrides`(把 `proxy/platform/guid/dev/mac/port` 拷到 process.env)/`resolveProxy`(解析 `KUGOU_API_PROXY` URL) |
| [config.json](file:///c:/BottleMusic/server/util/config.json) | 静态客户端 profile | `wx_appid: wx79f2c4418704b4f8`、`wx_lite_appid: wx72b795aca60ad321`、`srcappid: 2919`、`appid: 1005`(phone)、`apiver: 20`、`clientver: 20489`(phone)、`liteAppid: 3116`、`liteClientver: 11440`(concept — BottleMusic 基线) |

#### helper.js 中的盐值(★ 必须与 native/core/Crypto.cpp 字节对齐)

| 函数 | 盐值 | 模式 |
|---|---|---|
| `signatureWebParams` | `NVPh5oo715z5DIWAeQlhMDsWXXQV4hwt` | `MD5(salt + sorted(k=v) + salt)` |
| `signatureAndroidParams` | regular: `OIlwieks28dk2k092lksi2UIkp` / lite: `LnT6xpN3khm36zse0QzvmgTZ3waWdRSA` | `MD5(salt + sorted(k=v) + data + salt)` |
| `signatureRegisterParams` | `1014` | `MD5('1014' + sorted(v) + '1014')` |
| `signParams` | `R6snCXJgbCaj9WFRJKefTMIFp0ey6Gza` | `MD5(sorted(k+v) + data + salt)` |
| `signKey` | regular: `57ae12eb6890223e355ccfcb74edf70d` / lite: `185672dd44712f60bb1736df5a377e82` | `MD5(hash + salt + appid + mid + userid)` |
| `signCloudKey` | `ebd1ac3134c880bda6a2194537843caa0162e2e7` | `MD5('musicclound' + hash + pid + salt)` |
| `signParamsKey` | same as Android salt | `MD5(appid + salt + clientver + data)` |

### 7.4 module/ 层(130+ API 端点)

每个文件导出相同 shape:

```js
module.exports = (params, useAxios) => {
  // build dataMap from params
  return useAxios({
    url: '/v5/url',
    method: 'GET' | 'POST',
    params: dataMap,
    data: bodyMap,
    encryptType: 'android',
    headers: { 'x-router': 'trackercdn.kugou.com' },
    cookie: params?.cookie || {},
  });
};
```

`useAxios(config)` 由 server.js 注入,设 `config.ip` 从 `req.ip` 后委托 `createRequest(config)`。返回 `Promise<UseAxiosResponse>`。

#### 按域分组

| 域 | 文件数 | 代表文件 |
|---|---|---|
| **login** | 11 | `login.js`、`login_cellphone.js`(AES+RSA 加密)、`login_qr_key.js`/`login_qr_create.js`/`login_qr_check.js`(QR 3 步)、`login_token.js`、`login_device.js`/`login_device_kick.js`、`login_wx_create.js`/`login_wx_check.js`(WeChat OAuth)、`login_openplat.js` |
| **search** | 7 | `search.js`、`search_complex.js`、`search_default.js`、`search_hot.js`、`search_lyric.js`、`search_mixed.js`、`search_suggest.js` |
| **song/audio** | 11 | `song_url.js`(含 `encryptKey`)、`song_url_new.js`、`song_climax.js`、`song_ranking.js`/`song_ranking_filter.js`、`audio.js`、`audio_accompany_matching.js`/`audio_ktv_total.js`/`audio_related.js`、`kmr_audio_mv.js`、`krm_audio.js`、`privilege_lite.js` |
| **playlist** | 15 | `playlist_add.js`/`playlist_del.js`/`playlist_detail.js`/`playlist_effect.js`/`playlist_similar.js`/`playlist_tags.js`/`playlist_track_all.js`/`playlist_track_all_new.js`/`playlist_tracks_add.js`/`playlist_tracks_del.js`、`sheet_collection.js`/`sheet_collection_detail.js`/`sheet_detail.js`/`sheet_hot.js`/`sheet_list.js` |
| **lyric** | 1 | `lyric.js`(支持 `fmt=lrc`/`krc`,`decode=true` 时通过 `decodeLyrics` XOR+pako inflate 解码 KRC) |
| **album** | 5 | `album.js`/`album_detail.js`/`album_shop.js`/`album_songs.js`/`top_album.js` |
| **artist** | 10 | `artist_albums.js`/`artist_audios.js`/`artist_detail.js`/`artist_follow.js`/`artist_follow_newsongs.js`/`artist_honour.js`/`artist_lists.js`/`artist_unfollow.js`/`artist_videos.js`/`singer_list.js` |
| **rank** | 8 | `rank_audio.js`/`rank_info.js`/`rank_list.js`/`rank_top.js`/`rank_vol.js`/`yueku.js`/`yueku_banner.js`/`yueku_fm.js` |
| **recommend/fm** | 12 | `fm_class.js`/`fm_image.js`/`fm_recommend.js`/`fm_songs.js`/`personal_fm.js`/`recommend_songs.js`/`everyday_friend.js`/`everyday_history.js`/`everyday_recommend.js`/`everyday_style_recommend.js`/`lastest_songs_listen.js`/`listen_timeadd.js` |
| **user** | 13 | `user_cloud.js`/`user_cloud_url.js`/`user_detail.js`/`user_follow.js`/`user_follow_message.js`/`user_history.js`/`user_listen.js`/`user_playlist.js`/`user_video_collect.js`/`user_video_love.js`/`user_vip_detail.js`/`favorite_count.js`/`playhistory_upload.js` |
| **youth/vip** | 17 | `youth_vip.js`/`youth_day_vip.js`/`youth_day_vip_upgrade.js`/`youth_month_vip_record.js`/`youth_union_vip.js`/`youth_user_song.js`/`youth_dynamic.js`/`youth_dynamic_recent.js`/`youth_listen_song.js`/`top_card_youth.js`/`youth_channel_all.js`/`youth_channel_amway.js`/`youth_channel_detail.js`/`youth_channel_similar.js`/`youth_channel_song.js`/`youth_channel_song_detail.js`/`youth_channel_sub.js` |
| **scene** | 8 | `scene_audio_list.js`/`scene_collection_list.js`/`scene_lists.js`/`scene_lists_v2.js`/`scene_module.js`/`scene_module_info.js`/`scene_music.js`/`scene_video_list.js` |
| **longaudio** | 6 | `longaudio_album_audios.js`/`longaudio_album_detail.js`/`longaudio_daily_recommend.js`/`longaudio_rank_recommend.js`/`longaudio_vip_recommend.js`/`longaudio_week_recommend.js` |
| **video** | 6 | `video_detail.js`/`video_privilege.js`/`video_url.js`/`user_video_collect.js`/`user_video_love.js`/`kmr_audio_mv.js` |
| **comment** | 7 | `comment_album.js`/`comment_count.js`/`comment_floor.js`/`comment_music.js`/`comment_music_classify.js`/`comment_music_hotword.js`/`comment_playlist.js` |
| **ip** | 6 | `ip.js`/`ip_dateil.js`/`ip_playlist.js`/`ip_zone.js`/`ip_zone_home.js`/`top_ip.js` |
| **theme** | 4 | `theme_music.js`/`theme_music_detail.js`/`theme_playlist.js`/`theme_playlist_track.js` |
| **captcha** | 1 | `captcha_sent.js` |
| **top** | 6 | `top_album.js`/`top_card.js`/`top_card_youth.js`/`top_ip.js`/`top_playlist.js`/`top_song.js` |
| **misc** | 7 | `brush.js`(刷刷短视频)、`server_now.js`、`register_dev.js`(设备注册,dfid 来源)、`pc_diantai.js`(PC 轮播图)、`images.js`/`images_audio.js`、`ai_recommend.js` |

#### 代表性模块模式

- **`login_cellphone.js`**(最复杂):`cryptoAesEncrypt({mobile,code})` → `{str, key}` → `cryptoRSAEncrypt({clienttime_ms, key: encrypt.key})` 上传 AES key 给服务器 → `signParamsKey(timestamp)` → POST `/v7/login_by_verifycode`,response `secu_params` 用 AES key 解密 → 推送 token/userid/vip_type/vip_token 到 cookie
- **`song_url.js`**(带 `encryptKey`):`encryptKey: true`、`notSign: true`(set key 但 skip 标准 signature)、`pid: 411 lite / 2 phone`、`cmd: 26`、`behavior: 'play'`、`cdnBackup: 1`、`dfid: randomString(24)` if not in cookie
- **`register_dev.js`**(最 crypto-heavy):`playlistAesEncrypt(dataMap)` 6 字符随机 key → `rsaEncrypt2({aes: key, uid, token})` PKCS1-V1_5 → POST `/risk/v2/r_register_dev`,body = Base64,响应 `playlistAesDecrypt` → 推 `dfid=${body.data.dfid}` 到 cookie
- **`youth_vip.js`**(广告上报技巧):POST `/youth/v1/ad/play_report`,body = `{ad_id: 12307537187, play_end: now, play_start: now-30000}`。上报假 30 秒广告播放,KuGou 后端给 3 小时 VIP 增量。每日 8 次得 24 小时 VIP。文档标记 test-only

### 7.5 Docker 与部署

[Dockerfile](file:///c:/BottleMusic/server/Dockerfile):`node:lts-alpine` + `tini` PID 1 + `corepack enable` + `--prod --frozen-lockfile` + 非 root `node` 用户 + `EXPOSE 3000`。

[vercel.json](file:///c:/BottleMusic/server/vercel.json):`@vercel/node` builder on `./index.js` + catch-all route `/(.*) -> /`。

其它部署渠道:`pkg` 二进制(win/linux/linux-arm64/macos)、`esbuild` bundle(`pkgjs`)、子模块自带 GitHub Actions(`server/.github/workflows/build.yml`,在 `v*` 标签时三平台构建并发布 GitHub Releases)。

### 7.6 server/ → native/ 镜像结构对照表

| server/ (Node.js 参考) | native/ (C++ 生产) |
|---|---|
| `util/crypto.js` + `util/helper.js` | `core/Crypto.cpp` + `core/KuGouAndroidRequest.cpp` + `KuGouProfile.h` |
| `util/request.js`(axios HTTP) | `core/HttpClient.cpp`(WinHTTP + watchdog + retry + 连接池) |
| `util/apicache.js` + `util/memory-cache.js` | `storage/ApiCache.cpp` |
| `server.js`(Express 动态路由加载) | `core/CompatApi.cpp` + `compat_routes/*.cpp` |
| `module/login_*.js` | `core/LoginService.cpp` + `compat_routes/LoginRoutes.cpp` |
| `module/song_url*.js` + `audio*.js` | `core/SongUrlService.cpp` + `SongService.cpp` + `compat_routes/MediaRoutes.cpp` |
| `module/playlist_*.js` | `core/PlaylistService.cpp` + `compat_routes/PlaylistRoutes.cpp` |
| `module/register_dev.js` | `core/DeviceRegisterService.cpp` + `compat_routes/RegisterRoutes.cpp` |
| `module/lyric.js` + `util/util.js::decodeLyric` | `core/LyricService.cpp` + `core/LyricParser.cpp` |
| `module/user_*.js` | `core/UserService.cpp` + `compat_routes/UserRoutes.cpp` |
| `module/youth_vip*.js` | `core/compat_routes/YouthVipRoutes.cpp` |
| `module/rank_*.js` + `module/yueku*.js` | `core/RankService.cpp` |
| `module/search*.js` | `core/SearchService.cpp` |
| `module/personal_fm.js` + `module/recommend_songs.js` + `module/everyday_*.js` | `core/HomeService.cpp` |
| `module/privilege_lite.js` + `module/audio*.js` | `core/PrivilegeService.cpp` + `core/SongUrlService.cpp` |
| `module/playhistory_upload.js` + `module/user_history.js` + `module/user_listen.js` | `core/PlayHistoryService.cpp` |
| `util/index.js` + `util/decryptor.js`(decode 调度) | `core/JsonHelpers.cpp` + `core/HttpUtils.cpp` |
| `config.json`(盐 + appid + key) | `core/KuGouProfile.h` + `core/Authorization.cpp`(编译期常量) |
| `index.js` + `app.js`(Express 入口) | 无生产等价物(Rust `lib.rs` + `audio_proxy.rs` 用 Tauri 替代 Express) |
| `interface.d.ts`(TS 类型) | `include/EchoCAPI.h` + `include/echo/Async*.h` + `include/echo/Storage*.h` |
| `async/`(本仓库**无此目录**;KuGou 为同步代码) | `async/EventQueue.cpp` + `TaskScheduler.cpp` + `RequestScheduler.cpp` + `RequestWatchdog.cpp`(**纯 BottleMusic 新增**) |

> **关键差异**:server/ 是 KuGou API 的 Node.js 直译,所有 HTTP/加密/响应解析**同一函数内同步完成**;native/ 在等价功能上引入了**三层 deadline**(Rust `deadline_for_path` → C++ `RequestScheduler` per-kind → `HttpClient` watchdog)、**Wal SQLite Actor 串行化**、**shared_mutex 读多写少**、**重试预算**等并发安全机制,这些在 server/ 中完全不存在。镜像表仅描述**功能等价性**,不描述并发/容错等价性。

---

## 8. 跨层数据流

### 8.1 播放一首歌(端到端时序)

```
[Vue]                  [Rust FFI]              [C++ DLL]                [KuGou CDN]
  │                       │                       │                         │
  │ 1. user clicks song   │                       │                         │
  ├──────────────────────►│                       │                         │
  │   invoke('song_url',  │                       │                         │
  │     {albumId, hash})  │                       │                         │
  │                       │ 2. call Echo_request  │                         │
  │                       ├──────────────────────►│                         │
  │                       │   {cmd:'song_url',    │                         │
  │                       │    body:{albumId,hash}}│                         │
  │                       │                       │ 3. CompatApi route      │
  │                       │                       │   SongUrlService        │
  │                       │                       │   ├─ crypto.sign()      │
  │                       │                       │   ├─ KuGouAndroidRequest│
  │                       │                       ├────────────────────────►│ POST /v7/song_url
  │                       │                       │◄────────────────────────┤ JSON response
  │                       │                       │   ├─ JsonHelpers.parse  │
  │                       │                       │   ├─ PrivilegeService   │
  │                       │                       │   │   .extractPlayable  │
  │                       │                       │   └─ return {url, ...}  │
  │                       │◄──────────────────────┤                         │
  │                       │ 4. Rust serializes    │                         │
  │                       │   Result<Json, Err>   │                         │
  │◄──────────────────────┤                       │                         │
  │ 5. playerStore plays  │                       │                         │
  │   audioProxy.getProxy │                       │                         │
  │   (songUrl)           │                       │                         │
  ├──────────────────────►│                       │                         │
  │                       │ 6. audio_proxy: GET   │                         │
  │                       │   127.0.0.1:port/...  │                         │
  │                       │   sign URL server-side│                         │
  │                       ├───────────────────────────────────────────────►│ GET CDN with Range
  │                       │◄───────────────────────────────────────────────┤ audio bytes (206)
  │◄──────────────────────┤ stream audio chunks   │                         │
  │ 7. html5Backend feeds │                       │                         │
  │   <audio>             │                       │                         │
  │ 8. webAudioEq: capture│                       │                         │
  │   Stream → AudioWorklet                       │                         │
  │   → Gain → destination                        │                         │
  │ 9. playbackOrchestrator                       │                         │
  │   settle transitionSeq                        │                         │
  ▼                                                                                   │
[player starts; onEnded → next track via PlaybackCommandCoordinator mailbox]         │
```

**关键不变量**:
- 签名 URL **永远不入 JS 堆**(由 `audio_proxy.rs` 服务端注入 `Authorization` header)
- `html5Backend` 是**唯一**生产播放后端(其他 backend 已禁用)
- EQ 拓扑必须 `captureStream → MediaStreamSource → AudioWorkletNode → GainNode → destination`,**绝不** `createMediaElementSource`(会破坏 `<audio>` 元素)
- 切歌时 `transitionSeq` 必须 supersede 旧 session,旧 session 的 `onEnded` 被 phase guard 静默丢弃

### 8.2 EQ 工作链(完整节点图)

```
<audio> (HTML5 element, src = audio_proxy URL)
   │
   ▼  captureStream()  ←─ 关键:不破坏 <audio>
MediaStreamSource
   │
   ▼
AudioWorkletNode(eq-worklet-processor.js)
   │  process(inputs, outputs):
   │    10-band IIR/FIR on each channel
   │    apply -12..+12 dB per band (biquad cascade)
   ▼
GainNode (master volume)
   │
   ▼
AudioDestinationNode (→ speakers)
```

### 8.3 播放历史写入(Actor 串行化)

```
[Vue] onSongCompleted
  ├─ invoke('stats_record_play', {song, duration, completed})
  ▼
[Rust] stats.rs::stats_record_play
  ├─ CApiHandle.stats_record_play(...)
  ▼
[C++] PlayStatsService::record_play
  ├─ Database::actor().submit([=]{
  │     // 串行化到单一 DB 线程
  │     db.exec("INSERT INTO play_history_v2 ...");
  │     db.exec("UPDATE play_stats SET ...");
  │   })
  ▼
[SQLite WAL] play_history_v2 表 + play_stats 表
```

### 8.4 OS Media Session 同步

```
[Vue] playerStore.currentTime changed
  ├─ invoke('os_media_update_position', {pos, dur})
  ▼
[Rust] os_media_session.rs
  ├─ SystemMediaTransportControls.update()
  │   position: pos, duration: dur
  ▼
[OS] Windows Media Session UI (volume flyout, lock screen)

[User presses Media NextTrack key]
  ▼
[OS] → [Rust] GlobalShortcutManager.on_shortcut
  ├─ emit('media-next')
  ▼
[Vue] listens on 'media-next' → playerStore.next()
```

### 8.5 DeepSeek AI 听歌分析

```
[Vue] user clicks "Analyze" in Stats view
  ├─ reads API key from localStorage (session-only)
  ├─ invoke('ai_analyze', {key, history, prompt})
  ▼
[Rust] ai_analysis.rs
  ├─ reqwest::Client (shared, 30s timeout)
  ├─ POST https://api.deepseek.com/v1/chat/completions
  │   (or DeepSeek-compatible endpoint)
  │   Authorization: Bearer ${key}
  │   body: {model, messages: [system, user_with_history]}
  ▼
[DeepSeek] returns analysis text
  ▼
[Vue] renders AI analysis panel
  ※ Key is held only in Rust memory for the duration of the call;
     not persisted to disk; not logged.
```

### 8.6 Tauri IPC 命令注册总览

[Rust `lib.rs`](file:///c:/BottleMusic/ui/src-tauri/src/lib.rs) `invoke_handler!` 注册的命令(按模块分组):

| 模块 | 命令 | 调用方 |
|---|---|---|
| `backend_api.rs` | `echo_request`(JSON `{cmd, body}`)→ C++ `Echo_request` | `api/backend.ts` |
| `audio_proxy.rs` | `audio_proxy_url`、`audio_proxy_status`、`audio_proxy_shutdown` | `audio/audioProxy.ts` |
| `stats.rs` | `stats_record_play`、`stats_get_overview`、`stats_get_top_list`、`stats_get_timeline`、`stats_get_recent`、`stats_get_ai_insights` | `api/statsApi.ts`、`stores/playHistoryStore.ts` |
| `ai_analysis.rs` | `ai_analyze` | `api/aiAnalysisApi.ts` |
| `os_media_session.rs` | `os_media_update_state`、`os_media_update_position`、`os_media_set_enabled`、`os_media_set_thumb` | `osMediaSession.ts` |
| `tray.rs`(内嵌) | (无直接 invoke;通过 `emit` 推送到前端) | `trayEventListener.ts` |
| Tauri plugin | `plugin:opener|*`、`plugin:updater|*`、`plugin:process|*`、`plugin:shell|*` | `api/*.ts` |

### 8.7 C++ FFI 边界契约

`EchoCAPI.dll` 导出的 C ABI(见 [include/EchoCAPI.h](file:///c:/BottleMusic/native/include/EchoCAPI.h)):

```c
// 上下文(Meyers singleton,shared_mutex 保护)
EchoHandle Echo_createContext(const EchoConfig* cfg);  // 可空 cfg → 默认
void        Echo_destroyContext(EchoHandle h);         // 实际为 no-op(单例)
EchoHandle  Echo_defaultContext();                     // 返回单例

// 主请求入口
EchoResult  Echo_request(EchoHandle h, const char* json_in);
//  json_in:  {"cmd":"song_url","body":{...}}
//  EchoResult: {ok: bool, data: json_string, error: {code, msg}}

// 统计
EchoResult  Echo_stats_record_play(EchoHandle h, const char* json_in);
EchoResult  Echo_stats_get_overview(EchoHandle h);
EchoResult  Echo_stats_get_top_list(EchoHandle h, int limit);
EchoResult  Echo_stats_get_timeline(EchoHandle h, const char* range);
EchoResult  Echo_stats_get_recent(EchoHandle h, int limit);

// 调试
EchoResult  Echo_diagnostics_dump(EchoHandle h);
void        Echo_shutdown(EchoHandle h);  // 有界 shutdown,最多等 2s
```

**内存所有权**:所有 `char*` 入参由调用方(Rust)持有,函数返回期内有效;返回的 `EchoResult.data` 字符串由 C++ 用 `new char[]` 分配,Rust 端通过 `Echo_free_result` 释放(C++ 提供 `Echo_free_string(char*)`)。Rust `CApiHandle`(`backend_api.rs`)用 `libloading` 加载这些符号并在 `Drop` 时调用 `Echo_shutdown`。

---

## 9. 依赖关系

### 9.1 三层内部依赖图

```
┌──────────────────────────────────────────────────────────┐
│ Vue ui/src/                                              │
│   ├─ depends on → @tauri-apps/api (invoke)               │
│   ├─ depends on → Vue 3.5 / vue-router / GSAP            │
│   └─ no direct dependency on Rust or C++ (only via IPC)  │
└──────────────────────────────────────────────────────────┘
                            │ invoke()
                            ▼
┌──────────────────────────────────────────────────────────┐
│ Rust ui/src-tauri/src/                                   │
│   ├─ depends on → tauri 2 + plugins (opener/updater/...) │
│   ├─ depends on → reqwest, tokio, sysinfo, libloading    │
│   ├─ depends on → EchoCAPI.dll (runtime, dlopen-like)    │
│   └─ no compile-time dependency on C++ headers           │
└──────────────────────────────────────────────────────────┘
                            │ extern "C"
                            ▼
┌──────────────────────────────────────────────────────────┐
│ C++ native/                                              │
│   ├─ EchoCAPI → EchoCore → EchoStorage + EchoAsync +     │
│   │              EchoDiagnostics                          │
│   ├─ EchoCore  → nlohmann_json, WinHTTP, bcrypt,         │
│   │              crypt32, winhttp                         │
│   ├─ EchoStorage → SQLite (WAL) + nlohmann_json          │
│   ├─ EchoAsync → (header-only; no external deps)         │
│   ├─ EchoDiagnostics → psapi (Windows memory info)       │
│   └─ EchoImage → windowscodecs + ole32 (WIC)             │
└──────────────────────────────────────────────────────────┘

旁路:
┌──────────────────────────────────────────────────────────┐
│ server/ (Node.js, 非生产)                                │
│   └─ depends on → express, axios, crypto-js,             │
│                    node-forge, pako, qrcode              │
└──────────────────────────────────────────────────────────┘
```

### 9.2 C++ 库内部依赖矩阵

| 库 | 依赖(编译期) | 依赖(链接期) | 提供给 |
|---|---|---|---|
| `EchoStorage` | nlohmann_json | crypt32, SQLite3 | EchoCore |
| `EchoDiagnostics` | (无) | psapi | EchoCore |
| `EchoAsync` | (无) | (无) | EchoCore |
| `EchoCore` | EchoStorage + EchoDiagnostics + EchoAsync + nlohmann_json | winhttp, bcrypt, crypt32 | EchoCAPI |
| `EchoCAPI` | EchoCore | (转发 EchoCore) | Rust FFI |
| `EchoImage` | EchoAsync | windowscodecs, ole32 | (目前未挂载到主链路;预留给封面图缓存) |

### 9.3 前端 npm 依赖(ui/package.json)

**运行时**(`dependencies`):
- `vue ^3.5.13`、`vue-router ^4.6.4` — 视图与路由
- `gsap ^3.15.0` — 动画(Aurora 粒子、过渡)
- `@tauri-apps/api ^2`、`@tauri-apps/plugin-{opener,process,shell,updater} ^2` — Tauri IPC
- `qrcode ^1.5.4` — 登录二维码
- `@lucide/vue ^1.24.0`、`@phosphor-icons/vue ^2.2.1` — 图标
- `@fontsource/{eb-garamond,inter,libre-caslon-display,noto-serif-sc,zcool-xiaowei} ^5.3.0` — 字体(Newsprint 衬线 + Aurora 现代体)

**开发时**(`devDependencies`):
- `vite ^6.0.3`、`@vitejs/plugin-vue ^5.2.1` — 构建
- `vitest ^4.1.7`、`@vue/test-utils ^2.4.10`、`jsdom ^29.1.1` — 单测
- `vue-tsc ^2.1.10`、`typescript ~5.6.2` — 类型检查
- `playwright ^1.61.1` — E2E(预留)
- `@tauri-apps/cli ^2` — Tauri 命令行

### 9.4 Rust Cargo 依赖(ui/src-tauri/Cargo.toml)

- `tauri 2` + `tauri-plugin-{opener, updater, process, global-shortcut}` — Tauri 框架
- `serde 1` + `serde_json 1` — 序列化
- `tokio 1`(rt/rt-multi-thread/macros/time/net/io-util)— 异步运行时
- `reqwest 0.12`(json + stream)— HTTP(ai_analysis + audio_proxy 上游)
- `sysinfo 0.30` — 系统/进程信息
- `libloading 0.9.0` — DLL 符号加载(`EchoCAPI.dll`)
- `chrono 0.4` — 时间(deadline_for_path、stats)
- `futures-util 0.3` — 流处理(audio_proxy stream)
- `getrandom 0.2` — 端口分配随机性

### 9.5 C++ vcpkg 依赖(native/)

通过 [native/vcpkg.json](file:///c:/BottleMusic/native/vcpkg.json)(如存在)或 CMake `find_package`:
- `nlohmann_json` — JSON 解析(**必需**)
- `unofficial-sqlite3` 或 `SQLite3` — SQLite(**Release 必需**,Debug 可降级 JSON 文件 fallback)
- `httplib`(可选,目前未在主链路使用)
- `spdlog`(可选,目前未在主链路使用)
- `wil`(Windows Implementation Libraries,可选)

**Windows 系统库**(直接链接):
- `winhttp` — HTTP 客户端
- `bcrypt` — 加密原语
- `crypt32` — 证书/哈希
- `psapi` — 进程内存快照
- `windowscodecs` + `ole32` — WIC 图像解码
- `ws2_32` — Winsock(HTTP resilience test 用)

### 9.6 server/ npm 依赖(非生产,参考用)

- `express ^4.18.2` — HTTP 服务
- `axios ^1.1.3` — HTTP 客户端
- `crypto-js ^4.2.0` — 加密(AES/MD5)
- `node-forge ^1.3.3` — RSA
- `pako ^2.1.0` — gzip 解压
- `qrcode ^1.5.3` — 二维码
- `big-integer ^1.6.52` — 大数(RSA 数学)
- `safe-decode-uri-component ^1.2.1` — URI 解码

### 9.7 工具链依赖

| 工具 | 版本(最低) | 用途 |
|---|---|---|
| Rust toolchain | stable(2026-05 基线) | Tauri 编译 |
| Node.js | 18+(server/ 要求 ≥12,但 vitest 4 需要 18+) | 前端 + server/ |
| MSVC | VS 2022(v143) | C++ 编译 |
| CMake | 3.24+ | C++ 构建 |
| vcpkg | latest | C++ 第三方库 |
| Tauri CLI | 2.x | `tauri dev`/`tauri build` |

---

## 10. 测试体系

### 10.1 三层测试计数基线

| 层 | 框架 | 文件数 | 用例数 | 基线日期 | 来源 |
|---|---|---|---|---|---|
| C++ (native/) | CTest + assert 宏 | 11 个 test 可执行 | 11 个测试目标(每个含若干 assert) | 2026-07-03 | [CONTEXT.md](file:///c:/BottleMusic/CONTEXT.md) |
| Rust (ui/src-tauri/) | `cargo test --lib` | (待统计) | 22 个用例 | 2026-07-22 | design-qa.md |
| 前端 (ui/src/) | Vitest + jsdom + @vue/test-utils | 76 文件 | 917 用例 | 2026-07-22 | design-qa.md |

> ⚠ **计数时间差**:CONTEXT.md(07-03 基线)写"131 个测试用例",design-qa.md(07-22 基线)写"917 vitest 用例"。两者非冲突 — 前者是 07-03 时的总数(可能含 C++/Rust),后者是 07-22 时仅前端 vitest 的数量(07-03 后新增大量前端单测)。详见 [§18 模糊点](#18-模糊点--易误解处)。

### 10.2 C++ 测试目标(native/tests/)

| 测试可执行 | 覆盖 | 关键文件 |
|---|---|---|
| `EchoNativeSmokeTests` | 全栈契约(路由/加密/服务) | [tests/basic_contract_tests.cpp](file:///c:/BottleMusic/native/tests/basic_contract_tests.cpp) |
| `EchoRouteContractTest` | 路由分发 | [tests/route_contract_test.cpp](file:///c:/BottleMusic/native/tests/route_contract_test.cpp) |
| `EchoSongUrlContractTest` | song_url 解析 + privilege 提取 | [tests/songurl_contract_test.cpp](file:///c:/BottleMusic/native/tests/songurl_contract_test.cpp) |
| `EchoPlaylistContractTest` | 歌单列表/详情 | [tests/playlist_contract_test.cpp](file:///c:/BottleMusic/native/tests/playlist_contract_test.cpp) |
| `EchoProfileSignatureContractTest` | KuGou profile 签名 | [tests/profile_signature_contract_test.cpp](file:///c:/BottleMusic/native/tests/profile_signature_contract_test.cpp) |
| `EchoHomeContractTest` | 首页推荐/FM | [tests/home_contract_test.cpp](file:///c:/BottleMusic/native/tests/home_contract_test.cpp) |
| `EchoHttpClientResilienceTest` | WinHTTP 重试/超时/连接池 | [tests/http_client_resilience_test.cpp](file:///c:/BottleMusic/native/tests/http_client_resilience_test.cpp) |
| `EchoRequestSchedulerResilienceTest` | RequestScheduler deadline | [tests/request_scheduler_resilience_test.cpp](file:///c:/BottleMusic/native/tests/request_scheduler_resilience_test.cpp) |
| `EchoDatabaseActorLifecycleTest` | Storage Actor 串行化 | [tests/database_actor_lifecycle_test.cpp](file:///c:/BottleMusic/native/tests/database_actor_lifecycle_test.cpp) |
| `EchoDatabaseWalConcurrencyTest` | SQLite WAL 并发(需 SQLite) | [tests/database_wal_concurrency_test.cpp](file:///c:/BottleMusic/native/tests/database_wal_concurrency_test.cpp) |
| `EchoPlayStatsTest` | 统计服务 + C API 入口 | [tests/play_stats_test.cpp](file:///c:/BottleMusic/native/tests/play_stats_test.cpp) |

**特殊编译要求**(CMakeLists.txt 第 232-238 行):所有测试可执行**强制 `/UNDEBUG`**(MSVC)或 `-UNDEBUG`(其它),即使在 Release 预设下也保留 `assert` 宏,避免 CTest 报假绿。`PATH` 环境变量临时 prepend vcpkg `bin/` 目录,避免 0xc0000135 DLL 未找到错误。

**运行命令**:

```powershell
cd c:\BottleMusic\native
cmake --preset bottlemusic-debug     # 或 bottlemusic-release
cmake --build --preset bottlemusic-debug
ctest --preset bottlemusic-debug --output-on-failure
```

### 10.3 Rust 测试(ui/src-tauri/)

- 框架:`cargo test --lib`(默认 `#[test]` + `#[tokio::test]`)
- 22 个用例(07-22 基线),覆盖:
  - `audio_proxy` URL 解析、SSRF allowlist、range 拼接
  - `backend_api` CApiHandle 符号加载、shutdown 超时
  - `stats` 命令序列化
  - `os_media_session` 状态机
- **关键约束**:`cargo test --lib` **不带** `desktop-shell` feature(tray-icon 会触发 `STATUS_ENTRYPOINT_NOT_FOUND`,见 Cargo.toml 第 24-25 行注释)

**运行命令**:

```powershell
cd c:\BottleMusic\ui\src-tauri
cargo test --lib --no-default-features
```

### 10.4 前端 Vitest 测试(ui/src/)

- 框架:Vitest 4 + jsdom 29 + @vue/test-utils 2
- 76 文件 / 917 用例(07-22 基线)
- 测试位置:`ui/src/**/__tests__/*.test.ts` 或 `*.spec.ts`
- 覆盖:
  - stores:playerStore、playbackOrchestrator、playSessionTracker、themeStore、favoriteStore、homeFeedStore、fmSession、playHistoryStore、settingsStore 等
  - api/:backend(熔断、超时、重试)、audioProxy、statsApi、aiAnalysisApi
  - audio/:webAudioEq、eqWorkletProcessor、html5Backend
  - views:大多数视图有 smoke test
  - utils:各种工具函数

**运行命令**:

```powershell
cd c:\BottleMusic\ui
npm run test         # 单次运行
npm run test:watch   # watch 模式
```

**Vitest 配置**(根 [vite.config.ts](file:///c:/BottleMusic/ui/vite.config.ts)):
- environment: `jsdom`
- setupFiles: `./src/test/setup.ts`
- include: `src/**/*.{test,spec}.ts`

### 10.5 测试设计约定

1. **C++ 测试用 `assert` 宏作失败信号**(非 gtest/catch2),因此 CMake 强制 `/UNDEBUG`
2. **Rust 测试避免 tray-icon feature**,防止 STATUS_ENTRYPOINT_NOT_FOUND
3. **前端测试**用 jsdom 模拟 DOM,不依赖真实 Tauri 运行时(`@tauri-apps/api` 的 `invoke` 被 mock)
4. **无端到端测试**生产化:Playwright 在 devDependencies 但未在 CI 中执行(预留)
5. **覆盖率**:目前未在 CI 中强制 coverage 阈值

---

## 11. CI/CD

### 11.1 GitHub Actions 工作流

仓库根 `.github/workflows/` 下两个核心工作流(Windows-only):

#### [ci.yml](file:///c:/BottleMusic/.github/workflows/ci.yml)

**触发**:`push` 到任意分支 + `pull_request` 到 `main`。

**Jobs**(串行):
1. **native-test** — C++ 层
   - runs-on: `windows-latest`
   - 步骤:checkout → vcpkg install(nlohmann_json, unofficial-sqlite3) → CMake configure (`--preset bottlemusic-debug`) → build → `ctest --preset bottlemusic-debug --output-on-failure`
   - 上传测试日志 artifact
2. **rust-test** — Rust FFI 层
   - runs-on: `windows-latest`
   - 步骤:checkout → setup Rust stable → `cargo test --lib --no-default-features`(在 `ui/src-tauri/`)
   - 需要 `EchoCAPI.dll` 不存在(测试不依赖 DLL 加载)
3. **frontend-test** — 前端
   - runs-on: `ubuntu-latest`(轻量,无 Tauri 编译)
   - 步骤:checkout → setup Node 20 → `npm ci` → `npm run test`(vitest run)→ `npm run build`(vue-tsc + vite build 类型检查)

#### [release.yml](file:///c:/BottleMusic/.github/workflows/release.yml)

**触发**:`push` tag `v*`。

**Jobs**:
1. **build-native** — 编译 `EchoCAPI.dll`(Release preset)
2. **build-tauri** — `tauri build`(产出 NSIS `.exe` + minisign `.sig`)
3. **upload-release** — 上传到 GitHub Releases + 触发 Tauri updater JSON

**产物**:
- `BottleMusic_<version>_x64-setup.exe`(NSIS installer,currentUser scope)
- `BottleMusic_<version>_x64-setup.nsis.zip`
- `latest.json`(Tauri updater manifest)

### 11.2 Tauri 配置要点([tauri.conf.json](file:///c:/BottleMusic/ui/src-tauri/tauri.conf.json))

| 配置项 | 值 | 说明 |
|---|---|---|
| `productName` | `BottleMusic` | 产品名 |
| `version` | `1.0.0` | 版本 |
| `identifier` | `com.bottlemusic.app` | App ID |
| `app.windows[0].decorations` | `false` | 无边框(自绘 titlebar) |
| `app.windows[0].transparent` | `true` | 透明背景(Aurora 粒子) |
| `app.windows[0].resizable` | `true` | 可调整大小 |
| `app.security.csp` | 严格 CSP | 限制 img/connect/media-src |
| `bundle.targets` | `["nsis"]` | 仅 NSIS(Windows-only) |
| `bundle.windows.nsis.installMode` | `currentUser` | 不需管理员权限 |
| `bundle.windows.nsis.scope` | `currentUser` | 安装到用户目录 |
| `plugins.updater.endpoints` | GitHub Releases `latest.json` | minisign 签名验证 |
| `plugins.updater.pubkey` | minisign public key | 内置公钥 |
| `app.capabilities` | 最小权限白名单 | 仅暴露用到的命令 |

### 11.3 vcpkg 集成

[native/vcpkg.json](file:///c:/BottleMusic/native/vcpkg.json)(若存在)声明:

```json
{
  "name": "bottlemusic-native",
  "version-string": "1.0.0",
  "dependencies": [
    "nlohmann-json",
    "unofficial-sqlite3"
  ]
}
```

CMakeLists.txt 通过 `CMAKE_PREFIX_PATH` 自动找 `vcpkg_installed/x64-windows`(`ECHO_NATIVE_VCPKG_INSTALLED_DIR`,第 16-19 行),CI 在 Windows runner 上 `vcpkg install` 后即可被 CMake 发现。

### 11.4 自动更新流程

1. 用户应用启动 → Tauri updater 插件拉取 `endpoints` 中的 `latest.json`
2. 比较 `version` 与当前版本
3. 若有新版,下载 `.nsis.zip` 并用内置 `pubkey` 验证 minisign 签名
4. 验证通过 → 解压运行 NSIS installer(静默)→ 重启

**密钥管理**:minisign 私钥**不在仓库中**,由维护者本地持有;CI 仅用公钥验证。详见 [§16 安全与隐私](#16-安全与隐私)。

### 11.5 子模块 CI(server/.github/workflows/build.yml)

`server/` 是 git submodule 指向 `MakcRe/KuGouMusicApi`,其自带 GitHub Actions:
- 触发:`push` tag `v*`
- 三平台构建:`pkg` 产出 win/linux/linux-arm64/macos 二进制
- 上传到 GitHub Releases

**本仓库不触发 server/ 的 CI**(它是只读参考实现)。

---

## 12. 项目时间线

> 数据来源:`git log --pretty=format:"%ad|%s" --date=short --no-merges`。**首次提交实际为 2026-02-02**(早期为 C++ 学习文档),非之前 README 暗示的 05-09。总提交数 694(含 merge),非 merge 提交按月分布如下。

### 12.1 月度提交分布

| 月份 | 非合并提交数 | 阶段定位 |
|---|---|---|
| 2026-02 | 69 | **学习文档期**(C++ Core Guidelines、RAII、reading notes) |
| 2026-03 | 76 | **三层骨架搭建**(Tauri 初始化 → C++ DLL → HTTP client → SQLite) |
| 2026-04 | 72 | **业务路由铺开**(KuGou API 路由 + Request scheduler + UI 侧栏/topbar) |
| 2026-05 | 59 | **播放可用化 + 架构重构**(HTML5 backend、player bar、home view、FFI 迁移) |
| 2026-06 | 129 | **v1.0.0 发布期**(tag v0.1.0 → v1.0.0 + 大量 bug 修复) |
| 2026-07 | 272 | **爆发期**(Storage Actor、Aurora turntable 重设计、stats 增强、实机修复) |

### 12.2 关键里程碑(commit 级)

| 日期 | commit 摘要 | 阶段意义 |
|---|---|---|
| **2026-02-02** | `docs: add C++ learning roadmap` | 仓库起点(C++ 学习笔记,非产品代码) |
| **2026-03-03** | `feat: initialize Tauri 2.0 project` | Tauri 2.0 脚手架落地 |
| **2026-03-10** | `feat: create C++ DLL project` | C++ 核心层启动(EchoCAPI 雏形) |
| **2026-03-17** | `feat: implement HTTP client` | WinHTTP HttpClient 落地 |
| **2026-03-24** | `feat: add SQLite storage layer` | SQLite WAL 存储层落地 |
| **2026-04-01** | `feat: build KuGou API routes` | KuGou API 路由(参考 server/ 翻译) |
| **2026-04-08** | `feat: implement request scheduler` | RequestScheduler + 三层 deadline 雏形 |
| **2026-04-15** | `feat: create sidebar component` | Vue 前端侧栏 |
| **2026-04-22** | `feat: add topbar with search` | Vue 前端顶栏 + 搜索 |
| **2026-05-01** | `feat: implement player bar` | 播放器栏 |
| **2026-05-08** | `feat: add HTML5 audio backend` | HTML5 音频后端(后来成为**唯一**后端) |
| **2026-05-15** | `feat: create home view` | 首页 |
| **2026-05-22** | `feat: implement circuit breaker` | 前端 backend.ts 熔断器 |
| **2026-05-25** | `fix: update DeviceRegisterService headers` | 设备注册 header 对齐 |
| **2026-05-26** | `fix(backend): use correct signature salt and preview fallback for appid 3116` | **关键修复**:酷狗概念版 appid=3116 盐值 + 降级 fallback |
| **2026-05-29** | `feat(arch): migrate EchoCompatServer loopback to EchoCAPI.dll with Tauri FFI invoke` | **架构大重构**:从 sidecar HTTP server 迁移到 Rust FFI 直接 dlopen DLL |
| **2026-05-29** | `refactor(core): split routes into compat_routes, remove CompatApi scheduler` | 路由文件拆分 |
| **2026-05-29** | `feat(async): introduce RequestScheduler and HttpUtils` | 异步层独立 |
| **2026-05-29** | `feat(diagnostics): structured logging, redaction, and exception safety` | 诊断层落地(Redaction 脱敏) |
| **2026-05-29** | `feat(vip): VIP业务重构收尾` | VIP 领取流程(含 youth_vip 广告上报技巧) |
| **2026-05-30** | `fix(player): 播放队列/时长/随机循环/僵尸音频修复` | 播放稳定性大批修复 |
| **2026-05-30** | tag `v0.1.0` | **首个版本标签**(0.1.0,内部里程碑) |
| **2026-06-04** | tag `v1.0.0` | **1.0.0 发布**(CI release.yml 触发首次 NSIS 构建路径) |
| **2026-07-17** | `refactor(native): remove MF playback stack and BackendFacade` | **Media Foundation 播放栈移除**(MFS 原生播放损坏,改为 HTML5-only) |
| **2026-07-17** | `refactor(ui): HTML5-only playback and single-shot backend calls` | 前端单后端化 |
| **2026-07-17** | `fix(native): zero plaintext and DPAPI buffers from memory` | 安全增强(明文密钥内存清零) |
| **2026-07-18** | `feat(native): Storage Actor - dedicated DB thread serializes all access (P1 #1)` | **Storage Actor 落地**(SQLite 串行化线程) |
| **2026-07-18** | `build(native): fail Release configure without SQLite` | Release 强制 SQLite(禁止 JSON fallback 出货) |
| **2026-07-18** | `feat(ui): explicit playback phase state machine` | 播放阶段状态机(phase guard) |
| **2026-07-19** | `feat(player): centralize playback command coordination` | **PlaybackCommandCoordinator 落地**(coalescing mailbox) |
| **2026-07-19** | `fix(player): preserve queue on exit, keep FM session, soft FM cooldown` | 队列/FM 持久化 |
| **2026-07-19** | `fix(native): reportable init V2, safe Shutdown, open-only Actor Open` | Actor 生命周期收紧 |
| **2026-07-21** | `feat(aurora): turntable vinyl hero stage, cone light, calmer rail` | **Aurora turntable-night 视觉重设计**(黑胶唱机) |
| **2026-07-21** | `feat(aurora): warm charcoal dark tokens for turntable night` | Aurora 暗色 token |
| **2026-07-22** | `feat(visualizer): spectral horizon page with ring spectrum and spinning disc` | 可视化页面 |
| **2026-07-22** | `feat(stats): listening clock hero, vinyl thumbs, semantic tokens` | Stats 增强 |
| **2026-07-22** | `feat(stats): daily/weekly/monthly ranges (native 1d), artist+album cover grids` | **最新提交**(日/周/月范围 + 封面网格) |

### 12.3 五大阶段划分(阅读建议)

1. **学习文档期(2026-02)**:仓库最初是 C++ 学习笔记集合,非产品代码
2. **骨架搭建期(2026-03 ~ 04)**:三层架构从零到可用,Tauri+C++ DLL+SQLite+HTTP+RequestScheduler 依次落地
3. **业务铺开期(2026-05 上半月)**:HTML5 backend、player、home、circuit breaker 串联
4. **架构重构期(2026-05 下半月 ~ 06)**:**关键转折点** — 从 sidecar HTTP server 迁移到 Rust FFI dlopen DLL,1.0.0 发布
5. **打磨与视觉重设计期(2026-07)**:Storage Actor、HTML5-only 化、Aurora turntable 重设计、stats 增强、实机 bug 大批修复

### 12.4 与 CHANGELOG.md 的差异

> ⚠ [CHANGELOG.md](file:///c:/BottleMusic/CHANGELOG.md) 标注日期范围 2026-02-03 至 2026-05-22,**严重滞后于 git 历史**(实际已到 07-22)。15 个条目均早于 1.0.0 发布(06-04)。详见 [§18 模糊点](#18-模糊点--易误解处)。

---

## 13. GitHub 改进历史

### 13.1 关键 Merge PR(从 git log 提取)

| PR | 分支 | 主题 | 阶段意义 |
|---|---|---|---|
| #2 | `codex/eq-10-band-audibility` | 10 频段 EQ 可听性改进 | EQ 调优 |
| #3 | `codex/update-app-icon` | App 图标更新 | 品牌资产 |
| #4 | `codex/fix-audio-proxy-resume` | audio_proxy resume 修复 | 跨域音频 Range 续播 |
| #5 | `codex/playback-hmr-diagnostics` | 播放 HMR 诊断 | HMR 共享引用(`window.__bottlemusic_audio__`) |
| #16 | `feat/landing-os-media-t1a` | 着陆 + OS Media Session T1A | 系统媒体集成 |
| #17 | `docs/post-16-baseline-reconcile` | #16 后基线对齐文档 | 文档校对 |
| #18 | `fix/review-wal-docs-and-os-media-comment` | WAL 文档与 OS Media 评论修复 | 文档修复 |
| #19 | `feat/t1-media-frontend-stability` | T1 媒体前端稳定性 | 大量 player/ui 修复 |
| #20 | `codex/storage-actor` | Storage Actor | **SQLite 串行化**(P1 #1) |
| #21 | `codex/phase2-integration` | 第二阶段集成 | phase guard + 集成 |
| #22 | `fix/realdevice-bugs` | 实机 bug 修复 | 真实硬件场景 bug 批量修复 |

### 13.2 Tags

| Tag | 日期 | commit | 含义 |
|---|---|---|---|
| `v0.1.0` | 2026-05-30 | (5-30 系列) | 内部里程碑,首版可运行 |
| `v1.0.0` | 2026-06-04 | (6-04 系列) | **首个公开版本**,触发 release.yml NSIS 构建 |

### 13.3 从 PR 看改进模式

**Codex 系列分支**(`codex/*`):由 AI 协作生成的分支(#2/#3/#4/#5/#20/#21),通常聚焦单一技术点(EQ/audio_proxy/HMR/Storage Actor),合并前有 review。

**Feat 系列分支**(`feat/*`):人工发起的特性分支(#16/#19),跨度较大,常带 `docs/` 对齐分支(#17)与 `fix/` 跟进分支(#18)。

**Fix 系列分支**(`fix/*`):bug 修复分支(#4 已 codex、#18、#22),其中 #22 `realdevice-bugs` 是实机测试发现的真实硬件场景 bug 批量修复(2026-07-19 那批 `fix(player+ui)` 提交)。

### 13.4 改进主题演变

按 commit 消息前缀统计(非精确):

| 主题 | 主要活跃月份 | 代表 commit 模式 |
|---|---|---|
| `feat(native)` | 03-07 | C++ 核心新增(HttpClient、SQLite、Storage Actor、RequestScheduler) |
| `feat(ui)` / `feat(aurora)` | 04-07 | 前端特性 + Aurora 视觉(06 后转向 Aurora turntable) |
| `feat(player)` | 05-07 | 播放器(07-19 集中重写 phase state) |
| `feat(stats)` | 07 | 统计(07-22 集中爆发) |
| `fix(native)` | 05-07 | C++ bug(05-26 signature salt、07-19 actor lifecycle) |
| `fix(ui)` / `fix(player)` | 05-07 | 前端 bug(07-18/19 集中修复) |
| `fix(aurora)` | 07 | Aurora 视觉细节 |
| `refactor(native)` | 05-07 | C++ 重构(05-29 路由拆分、07-17 移除 MF stack) |
| `refactor(ui)` | 07 | 前端重构(07-17 playerStore 拆分、HTML5-only) |
| `docs` | 02-07 | 文档(02 学习笔记、07-18 storage actor 文档) |
| `test(native)` / `test(ui)` | 07 | 测试(07-18 RED storage actor lifecycle、07-19 characterize) |

### 13.5 仓库 URL 与分支

- **GitHub**:[Ningbottle/BottlePlayer](https://github.com/Ningbottle/BottlePlayer)(注意仓库名是 `BottlePlayer`,产品名是 `BottleMusic`)
- **默认分支**:`main`
- **当前 HEAD(本 Wiki 基线)**:`22ba7951`(2026-07-22)
- **子模块**:`server/` → `MakcRe/KuGouMusicApi`(MIT License,作者 Lines)

---

## 14. 子项目 S1–S5 + 双界面重设计

> 数据来源:[CONTEXT.md](file:///c:/BottleMusic/CONTEXT.md)。v2 effort 以 **5 个子项目**(S1–S5)在共享 FFI 边界上展开,全部 ✅ Complete。2026-07 又叠加 **双界面重设计**(dual-interface redesign)。

### 14.1 S1–S5 总览

| 子项目 | 状态 | 关键交付物 |
|---|---|---|
| **S1 Resilience** | ✅ Complete | 三层 deadline、CircuitBreaker、有界 Shutdown/Restart、HttpClient watchdog |
| **S2 Auto-update/CI** | ✅ Complete | ci.yml、release.yml、sync-version.mjs、skip-version、Cargo test gate |
| **S3 Skin system** | ✅ Complete | themeStore、Aurora + Newsprint 皮肤、dark mode、FOUC 预防 |
| **S4 Playback+EQ** | ✅ Complete | HTML5 backend + Web Audio API EQ(生产)、PlaySessionTracker、event ownership |
| **S5 Statistics** | ✅ Complete | PlayStatsService、StatsView、DeepSeek AI 分析、6 个 stats Tauri 命令 |

### 14.2 S1 Resilience 详情

**三层 deadline**(从外到内):

```
┌─ Rust deadline_for_path (ui/src-tauri/src/backend_api.rs)
│    按 path 分类(card_url/playlist 等给长 deadline,其它短 deadline)
│    invoke 整体超时(默认 14s)
│
├─ C++ RequestScheduler per-kind (native/async/RequestScheduler.cpp)
│    每个 cmd kind 独立 deadline(slow/fast 分类)
│    超时 → 取消 token,workers 通过 IsCancelled 退出
│
└─ C++ HttpClient watchdog (native/core/HttpClient.cpp)
     每次 WinHTTP 请求挂 watchdog
     超时 → 关闭连接句柄,触发上层重试
```

**CircuitBreaker**(前端 `ui/src/api/backend.ts`):按请求类别分桶(`backend/categoryBucket.ts`),连续失败 N 次开路 → cooldown 后半开试探。前端调用 `invoke` 前 14s 超时,熔断开路时直接 reject 不发 invoke。

**有界 Shutdown/Restart**:`Echo_shutdown` 最多等 2s(`backend_api.rs::Drop`),超时强退;`Restart` 路径会先 Shutdown 再重新 dlopen DLL。

**关键文件**:
- [native/async/RequestScheduler.cpp](file:///c:/BottleMusic/native/async/RequestScheduler.cpp)
- [native/async/RequestWatchdog.cpp](file:///c:/BottleMusic/native/async/RequestWatchdog.cpp)
- [native/core/HttpClient.cpp](file:///c:/BottleMusic/native/core/HttpClient.cpp)
- [ui/src/api/backend.ts](file:///c:/BottleMusic/ui/src/api/backend.ts) + [ui/src/api/categoryBucket.ts](file:///c:/BottleMusic/ui/src/api/categoryBucket.ts)
- [ui/src-tauri/src/backend_api.rs](file:///c:/BottleMusic/ui/src-tauri/src/backend_api.rs)

### 14.3 S2 Auto-update/CI 详情

**ci.yml**(见 [§11.1](#111-github-actions-工作流)):三 job(native/rust/frontend),Windows-only native job。

**release.yml**:tag `v*` 触发,产出 NSIS + minisign + `latest.json`。

**sync-version.mjs**:`ui/src-tauri/tauri.conf.json` 与 `ui/package.json` 与 `native/CMakeLists.txt` 三处版本同步脚本,发版前用 `npm run version:bump`。

**skip-version**:若 commit message 含 `[skip ci]`,release.yml 不触发(避免重复构建)。

**Cargo test gate**:CI 在 rust-test job 跑 `cargo test --lib --no-default-features` 作为合并门禁。

### 14.4 S3 Skin system 详情

**themeStore**(`ui/src/stores/themeStore.ts`):
- 模块级 `reactive` 单例(**不**用 Pinia)
- 状态:`skin: 'aurora' | 'newsprint'`、`mode: 'light' | 'dark'`
- 持久化:localStorage `bottlemusic_theme`
- FOUC 预防:`main.ts` 在 mount 前**同步**读 localStorage 并给 `<html>` 加 `data-skin` + `data-mode` 属性,避免首屏闪烁
- 切换:GSAP 渐变过渡 + CSS variable 热替换

**Token 系统**(见 [§6.7](#67-样式styles)):4 个显式选择器块(2 皮肤 × 2 模式),dark 值**不**由 opacity 派生。

**皮肤组件分离**:
- `components/aurora/` — Aurora 专属(粒子、turntable、stage atmosphere)
- `components/newsprint/` — Newsprint 专属(editorial 排版)
- `components/primitives/` — 皮肤无关原语(SkinButton/SkinListRow/SkinPageHeader/SkinEmptyState)
- `components/shell/` — 外壳(AuroraShell/NewsprintShell/WindowControls/...)

### 14.5 S4 Playback+EQ 详情

**默认后端**:`Html5AudioBackend`(**唯一**生产后端,Media Foundation 播放栈已移除,见 [§12.2](#122-关键里程碑commit-级) 2026-07-17 条目)。

**Stop 清理**:`Html5AudioBackend.stop()` unload 当前 `src`,失败 next-track resolve 无法 resume 陈旧媒体。

**EQ 实现**:Web Audio API AudioWorklet graph(10 bands: 31/62/125/250/500/1K/2K/4K/8K/16K Hz),`webAudioEq.ts` controller + `eqWorkletProcessor.ts` DSP(RBJ peaking from Audio EQ Cookbook),路由 `captureStream → MediaStreamAudioSourceNode → AudioWorkletNode → GainNode → destination`。

**EQ + audio_proxy 协作**(#1):KuGou CDN 不发 CORS 头,本地 Tauri HTTP 代理(`audio_proxy.rs`,loopback 127.0.0.1)用 CORS 头 + range/resume 重发 CDN 媒体,让 EQ graph 能挂载到跨域媒体上。`eqState.available` 暴露到 UI;代理不可用时显示降级提示。

**EQ graph build order**(#4):完整 `filter → gain → destination` 链**在 `createMediaElementSource` 之前**构建;throw 安全(element 永不会滞留在断开图中)。

**AudioContext lifecycle**(#9):`webAudioEq.close()` 在 teardown 释放 context(HMR 安全)。

**Suspended resume**(#10):`resume()` 失败通过 `onSuspendedFail` 上报,不被吞掉。

**PlaySessionTracker**(状态机):session 只在真实 `play` 事件时打开(rejected `play()` 不开幽灵 session);`listened_seconds` 防跳转(forward delta 0<Δ<2s 计入,jumps/backward 忽略);`completed` 用累加器非 duration;`setQuality` skip+intend 保持 quality 准确。

**Event ownership**(#2):`Html5AudioBackend.onEvent` 是**唯一**事件源;`initPlayer` 只处理 `durationchange`/`loadedmetadata`。双 `ended` handler 双取 `/song/url` 的 bug 已修。

**Single-loop replay**:在 `ended` handler 中处理(非 `next()`);`intend()` 在 `play()` 之前(Bug A 不变量)。

**Native MF playback / EchoPlayback\***:已移除(架构审计 stage 2)。生产 EQ **仅** 10-band Web Audio。

**BackendFacade**:已移除;测试和生产仅用 CompatApi。

### 14.6 S5 Statistics 详情

**Schema**:`play_history_v2` 表 — `song_hash, song_name, singer_name, album_id, album_name, cover_url, duration_seconds, completed, listened_seconds, quality, played_at`。索引:`played_at`、`song_hash`。

**Record 路径**:每次播放 → 1 行。通过 `PlaySessionTracker`(skip-immune 累加器)+ `stats_record_play` Rust 命令 → `EchoStatsRecordPlay` C API → `PlayStatsService::RecordPlay`(用 `SqlEscape` 防 SQL 注入)。

**查询端点**(6 个 `EchoStatsGet*` C API → 6 个 Rust Tauri 命令):
- `stats_get_summary` — 总播放、听歌秒数、独立歌曲/歌手数、完成率,per range(7d/30d/all)
- `stats_get_top` — Top N by song/artist/**album_id**(album 按 `album_id` 分组而非 `name`,避免同名合并)
- `stats_get_timeline` — 每日播放计数(`{date: "YYYY-MM-DD", count: N}`)
- `stats_get_recent` — 最近 N 次播放(含完整 metadata,limit/offset)
- `stats_get_recommendations` — "for you"基于 top artists(本地,无 KuGou API 融合)
- `stats_get_ai_insights` — DeepSeek AI 分析(详见下)

**线程安全**:`g_stats` 用 `shared_lock(g_api_rwlock)` 保护;`Database::Execute`/`ExecuteQuery` 持 `mutex_`;5 个 query C API 函数全部 try-catch + 空 JSON fallback。`EchoShutdown` 在独占生命周期锁下重置 global API/database/stat 指针。

**AI 分析**:`ai_analyze` async Tauri 命令 → reqwest → DeepSeek API。用户提供 API key via localStorage `deepseek_api_key`(password 输入,never logged)。30s 超时。中文 prompt,200 字限制,覆盖听歌习惯 + 音乐品味 + 一个有趣发现。

**StatsView.vue**:4 区块 — overview 卡片(总播放/听歌时长/完成率/独立数)、Top 榜单(带封面,song/artist/album)、timeline CSS 柱状图、recent plays 列表(带封面 + 完成徽章)、AI 分析面板。

### 14.7 双界面重设计(2026-07)

来源:`docs/superpowers/specs/2026-07-12-dual-interface-closeout-design.md` + `docs/superpowers/plans/2026-07-12-dual-interface-closeout.md`。

| 项目 | 状态 |
|---|---|
| Aurora / Newsprint 独立 shells(Home、PlayerBar、LyricStage) | ✅ Closeout complete |
| Home keep-alive + `homeFeedStore` | ✅ |
| Skin-differentiated Sidebar / Topbar chrome | ✅ |
| Enriched Aurora empty queue rail | ✅ |
| Search / Playlist `SkinPageHeader` | ✅ |
| 验证报告 | `docs/superpowers/reports/2026-07-12-dual-interface-closeout-verification.md` |

**关键设计**:每个皮肤有**自己的** Shell + 各自的 Home/PlayerBar/LyricStage 实现,通过 `data-skin` + `data-shell` 属性在 CSS 层切换;原语层(SkinButton 等)被两个皮肤复用。Home 用 `keep-alive` + `homeFeedStore` 缓存首页推荐,避免切皮肤时重拉。

**历史 worktree**:`.worktrees/dual-interface-player-redesign`(合并到 main 后可移除)。

### 14.8 Aurora turntable-night 视觉重设计(2026-07-21)

继双界面重设计之后的**视觉子重设计**,commit 集中在 2026-07-21:

- `feat(aurora): turntable vinyl hero stage, cone light, calmer rail` — 黑胶唱机主舞台
- `feat(aurora): vinyl spin profile and startVinylSpin deck motion` — 唱片旋转动画
- `feat(aurora): rewrite stage atmosphere as light-cone dust` — 光锥尘埃氛围
- `feat(aurora): warm charcoal dark tokens for turntable night` — 暖 charcoal 暗 token
- `feat(aurora): persistent muted transport, deck play button, needle playhead` — 唱针拾音头
- `feat(aurora): 64px icon rail for 900-1099px windows` — 中窄屏侧栏

**视觉效果**:播放时黑胶唱片旋转,光锥尘埃随音量浮动,唱针拾音头随进度移动;停止时唱片缓停。配合 `warm charcoal` dark token 形成深夜听歌氛围。

---

## 15. 领域语言词汇表

> 数据来源:[CONTEXT.md § Language](file:///c:/BottleMusic/CONTEXT.md)。这些术语在 issue、refactor、test 中统一使用,避免同义词混用。

### 15.1 播放域(Playback)

| 术语 | 含义 | 避免(同义词) |
|---|---|---|
| **Backend** | `PlayerBackend` 之后的播放抽象 — 如 `Html5AudioBackend`(生产默认)或 `NativeBackend`(已禁用) | player, audio engine |
| **PlaybackOrchestrator** | 拥有播放 transition 和 Resolve/PlaySession/Backend 顺序的模块 | playback helper, player coordinator |
| **Playback transition** | 从一个播放源/状态到另一个的变更:切歌、切音质、重播、reload 缺失源恢复 | playback action, player operation |
| **PlaySession** | 从真实 `play` 事件到 `ended`/`stop` 的一次听歌 session,由 `PlaySessionTracker` 跟踪 | play instance, playback session |
| **Resolve** | 通过 KuGou API 路由把 song identity 转成可播放 URL | fetch url, get link |
| **EQ graph** | Web Audio API AudioWorklet graph(10 bands),通过本地 audio_proxy 路由以对跨域 CDN 媒体做 EQ | equalizer, filter chain |

### 15.2 统计域(Statistics)

| 术语 | 含义 | 避免 |
|---|---|---|
| **Listened seconds** | PlaySession 期间实际听到的秒数,由 seek-immune 累加器累计(只计 forward delta 0<Δ<2s) | play duration, actual play time |
| **Completed** | PlaySession 是否算作完成,基于 listened-seconds 累加器 — 非原始 track duration | finished, played through |

### 15.3 弹性域(Resilience)

| 术语 | 含义 | 避免 |
|---|---|---|
| **Circuit breaker** | 前端弹性包装(`circuitBreaker.ts`)— 反复失败后开路,cooldown 前阻塞调用 | fallback, retry handler |
| **Request** | 通过 C++ `RequestScheduler` 线程池(带 per-kind deadline)分发的 KuGou API 调用 | fetch, HTTP call |

### 15.4 KuGou 业务域(补充,来自代码)

| 术语 | 含义 |
|---|---|
| **dfid** | device fingerprint id,通过 `/risk/v2/r_register_dev` 注册获取,长期 cookie |
| **appid** | KuGou 应用标识,本项目用概念版 `3116`(Lite 盐);标准端用 `1014`/`1005` |
| **salt** | 签名盐,4 套:`web_salf`、`android_salf`、`register_salf`、`cloud_salf`(均为编译期常量,见 `KuGouProfile.h`) |
| **privilege** | 歌曲可播放权限信息,从 `song_url` 响应中提取,决定音质/可用性 |
| **personalFm** | 私人 FM,酷狗推荐流,基于 user_id 生成 daily queue |
| **youth_vip** | 青年 VIP,通过假广告上报(`/youth/v1/ad/play_report` 30 秒)换 3 小时 VIP 增量,每日 8 次 |
| **concept edition** | 酷狗概念版,appid=3116,本项目目标平台 |
| **Lite 盐** | 概念版用的简化签名盐(相对标准端) |
| **EchoCAPI** | 本项目 C++ 导出 DLL 的命名前缀(`Echo*` 系列 C ABI 函数) |
| **CompatApi** | 兼容 KuGou API 的路由层,server/ 的 Node.js 直译 + 三层 deadline 增强 |

### 15.5 架构域(补充)

| 术语 | 含义 |
|---|---|
| **三层 deadline** | Rust `deadline_for_path`(外) → C++ `RequestScheduler` per-kind(中) → `HttpClient` watchdog(内) |
| **Storage Actor** | SQLite 串行化线程,所有 DB 访问通过 actor mailbox 提交(`Database::actor().submit(...)`) |
| **transitionSeq** | 播放 transition 序列号,新 transition supersedes 旧 transition,旧 onEnded 被 phase guard 静默丢弃 |
| **PlaybackCommandCoordinator** | coalescing mailbox,合并连续播放命令(next/prev/play/seek)避免 race |
| **audio_proxy** | Rust 端 127.0.0.1 loopback HTTP 代理,签名 URL 服务端注入,绕开 CORS,SSRF allowlist |
| **HMR shared audio** | `window.__bottlemusic_audio__` 全局引用,Vite HMR 重建模块时不重建 `<audio>` 元素 |

---

## 16. 安全与隐私

> 数据来源:[PRIVACY.md](file:///c:/BottleMusic/PRIVACY.md)(更新 2026-07-17)+ [SECURITY.md](file:///c:/BottleMusic/SECURITY.md)。

### 16.1 数据本地保存

| 数据类型 | 存储位置 | 保护 |
|---|---|---|
| 播放历史 + 统计 | SQLite(`play_history_v2` 表)或 WebView localStorage | 无加密(本地数据) |
| 账号会话(token/userid/vip) | 本地应用数据目录 | **Windows DPAPI**(当前用户范围) |
| 设备记录 | 本地 + KuGou 远端 | 本地无加密 |
| 播放队列/音量/音质/EQ/外观 | WebView localStorage | 无加密 |
| 诊断日志 | 本地应用数据目录 | 无加密,提交前需按 SECURITY.md 脱敏 |
| DeepSeek API Key | **仅内存**(当前页面会话) | **不写磁盘 / 不入 localStorage** |
| minisign 私钥 | **不在仓库** | 维护者本地持有 |

### 16.2 第三方数据发送

#### 16.2.1 酷狗(KuGou)及其接口/CDN

使用对应功能时发送:
- 扫码登录、会话校验、退出登录
- 账号资料、VIP 状态、歌单/曲库
- 搜索、歌曲详情、歌词、封面、音频地址、音频流
- 播放历史同步、设备注册

**这些请求由酷狗网关/接口/CDN 处理,酷狗如何保存/使用由其政策决定,BottleMusic 无法控制。**

#### 16.2.2 DeepSeek AI 分析

- **可选功能**:用户在统计页主动点击 AI 分析并提供 API Key 才执行
- 发送:听歌摘要(用于生成报告)
- 认证:用户提供的 API Key(Bearer token)
- **API Key 仅当前页面会话内存**,页面刷新/关闭后需重新输入
- 提示:不要在摘要/prompt/错误反馈中加姓名/账号/会话令牌/设备标识

#### 16.2.3 GitHub Releases

- 启动时自动检查更新,设置页可手动检查
- 下载 + 安装通过 Tauri 更新器,**minisign 签名验证**
- GitHub 按其自身政策处理请求网络信息

#### 16.2.4 不使用开发者自建遥测

**BottleMusic 没有开发者自建的分析/广告/遥测服务器**。应用启动不会向开发者后台上传使用统计。上述酷狗/DeepSeek/GitHub 请求是功能所需第三方请求,非开发者控制。

### 16.3 应用安全设计

#### 16.3.1 Tauri 安全配置

- **严格 CSP**:限制 `img-src`/`connect-src`/`media-src`,见 [tauri.conf.json](file:///c:/BottleMusic/ui/src-tauri/tauri.conf.json)
- **最小权限 capabilities**:仅暴露用到的命令(白名单),非 `**` 通配
- **NSIS currentUser scope**:不需管理员权限,安装到用户目录
- **无边框窗口**(`decorations: false`):自绘 titlebar,无系统标题栏注入风险

#### 16.3.2 C++ 安全措施

- **DPAPI 内存清零**(`2026-07-17 fix(native): zero plaintext and DPAPI buffers from memory`):明文密钥/DPAPI 缓冲用后即清零
- **Redaction 脱敏**:诊断日志中 token/userid 等敏感字段在 `Redaction.cpp` 中脱敏
- **SqlEscape**:所有 SQL 拼接经 `SqlEscape` 转义,防 SQL 注入
- **shared_mutex**:读写锁保护全局状态(`g_api_rwlock`),避免数据竞争
- **明文 session 路径关闭**(`2026-07-17 fix(native): close plaintext session path after one-time migration`):一次性迁移后关闭明文 session 持久化路径

#### 16.3.3 audio_proxy SSRF 防护

- **SSRF allowlist**:只允许代理 KuGou CDN 域名白名单,不允许任意 URL
- **签名 URL 不入 JS**:Authorization header 由 Rust 端服务端注入,JS 堆中无签名
- **loopback only**:只监听 `127.0.0.1`,不接受外部连接

#### 16.3.4 更新签名验证

- minisign 公钥内置在 `tauri.conf.json` 中
- 下载 NSIS 包后用公钥验签,**验签失败拒绝安装**
- 私钥不在仓库 / 不在 CI,由维护者本地持有

### 16.4 数据清理

| 操作 | 清理内容 |
|---|---|
| 退出登录 | 本地账号会话 + 设备记录;前端登录状态 + 最近播放重置(不删酷狗远端数据) |
| 删除 AppData 目录 | 本地数据库 + 统计 + 缓存 + 日志 + 受保护会话 + WebView 存储 + 偏好 |
| 卸载应用 | 程序文件(不删用户数据) |

**应用当前没有"一键清除所有本地数据"按钮**。彻底清理需手动:退出登录 → 关闭应用 → 删除 `%LOCALAPPDATA%\com.bottlemusic.app` → 卸载。

### 16.5 SECURITY.md 漏洞报告流程

涉及令牌/账号/个人信息或安全漏洞时,**不要公开贴原文**,优先按 [SECURITY.md](file:///c:/BottleMusic/SECURITY.md) 的私密报告流程提交。

---

## 17. 已知问题与遗留事项

> 数据来源:[CONTEXT.md § Known Issues](file:///c:/BottleMusic/CONTEXT.md)(4 项)。截至 2026-07-22 基线。

### 17.1 MFS 原生播放损坏(已弃用,**不修**)

- **症状**:Media Foundation 原生播放拓扑解析失败,退出时死锁
- **状态**:已禁用,降级到 HTML5 后端
- **决策**:**不会修复** — MFS 路径已弃用,改用 Web Audio API(2026-07-17 commit `refactor(native): remove MF playback stack and BackendFacade`)
- **影响代码**:`EchoPlayback*` 已从 native/ 移除,`BackendFacade` 已移除,测试和生产仅用 CompatApi

### 17.2 EQ 对 KuGou CDN 媒体(已解决 ✅)

- **症状**:KuGou CDN 不发 CORS 头,Web Audio API 无法挂载 EQ graph
- **解决**:`audio_proxy.rs`(loopback 127.0.0.1)用 CORS 头 + range/resume 重发 CDN 媒体,EQ graph 可挂载
- **降级**:代理不可用时显示降级提示(`eqState.available` 暴露到 UI)
- **关键不变量**:EQ 拓扑 `captureStream → MediaStreamSource → AudioWorkletNode → GainNode → destination`,**绝不** `createMediaElementSource`

### 17.3 `Music Player.html` 重写(0bedf68)

- **背景**:spec 要求 line 673 一行语法修复,但 commit `0bedf68` 做了完整重写(格式化 + 死代码删除)
- **状态**:文件被仓库追踪,**不是** v2 源文件,但随应用一起分发
- **遗留**:无后续动作,仅作记录

### 17.4 PR review `0bedf68..ce5233c` 延期小项

#### 17.4.1 EQ 重复 `initPlayer` 重初始化顺序

- **现状**:重复 `initPlayer` 时 EQ 重初始化顺序问题
- **当前无害**:EQ 始终因 CORS 被禁用(在 audio_proxy 之前)
- **风险**:audio_proxy 启用后可能暴露
- **状态**:延期

#### 17.4.2 `onEnded` phase guard

- **现状**:phase guard 防御性代码
- **理论不可能触发**:transitionSeq supersede 保证旧 onEnded 不会执行
- **状态**:保留作防御,延期

#### 17.4.3 DeepSeek API URL `/v1` 前缀

- **现状**:`ai_analysis.rs` 调用 DeepSeek 时 URL 缺 `/v1` 前缀
- **当前可用**:DeepSeek 端点对带/不带 `/v1` 都响应
- **状态**:spec 偏差,延期

### 17.5 其它遗留(来自 commit 历史观察)

- **CHANGELOG.md 滞后**(详见 [§18.1](#181-changelogmd-与-git-历史日期不一致)):最后条目 2026-05-22,实际 git 已到 07-22
- **测试计数时间差**(详见 [§18.2](#182-测试计数时间差131-vs-917)):CONTEXT.md(07-03)131 个 vs design-qa.md(07-22)917 vitest 用例
- **作者署名不一致**(详见 [§18.4](#184-作者署名不一致hoowhoami-vs-ningbottle)):LICENSE `hoowhoami` vs tauri.conf.json `Ningbottle`
- **docs/ 被 gitignore**:需 `git add -f` 才能追踪(见 [CONTEXT.md § Environment](file:///c:/BottleMusic/CONTEXT.md))

---

## 18. 模糊点 / 易误解处

> 这些是本 Wiki 编写过程中发现的**事实层面不一致或描述模糊**处。读者应知悉这些不确定点,避免被误导。

### 18.1 CHANGELOG.md 与 git 历史日期不一致

- **CHANGELOG.md**:15 个条目,日期范围 **2026-02-03 至 2026-05-22**
- **git 历史**:首次提交 **2026-02-02**,最新提交 **2026-07-22**
- **不一致点**:CHANGELOG 严重滞后,所有条目均在 v1.0.0(06-04)之前
- **推断**:CHANGELOG 在 v1.0.0 发布前维护,发布后停止更新;真实历史以 git log 为准
- **建议**:以 git log + 本 Wiki [§12 时间线](#12-项目时间线) 为准

### 18.2 测试计数时间差(131 vs 917)

- **CONTEXT.md**(2026-07-03 基线):**131 个测试用例**
- **design-qa.md**(2026-07-22 基线):**917 vitest 用例**(76 文件)
- **不一致点**:数字相差 7 倍
- **推断**:两者非冲突 — 前者是 07-03 时的**总数**(可能含 C++/Rust),后者是 07-22 时**仅前端 vitest** 的数量;07-03 后(尤其 07-18/19 那批 player/ui 测试)新增大量前端单测
- **建议**:以 [§10.1 三层测试计数基线](#101-三层测试计数基线) 表为准,该表区分了层与基线日期

### 18.3 DeepSeek API Key 存储描述不一致

- **CONTEXT.md**(S5 Details):`User provides API key via localStorage deepseek_api_key`
- **PRIVACY.md**(2026-07-17):`API Key 仅当前页面会话,不写入磁盘或 localStorage`
- **不一致点**:CONTEXT.md 说 localStorage,PRIVACY.md 说仅内存
- **推断**:PRIVACY.md 更新(07-17),可能反映**当前实现** — Key 仅内存会话;CONTEXT.md 描述可能滞后
- **代码事实**:`ai_analysis.rs` 在请求时从 Rust 内存持有 Key;前端是否从 localStorage 读需查 `api/aiAnalysisApi.ts`(本 Wiki 未深入)
- **建议**:以 PRIVACY.md(07-17 更新)为准,Key 不入磁盘

### 18.4 作者署名不一致(hoowhoami vs Ningbottle)

- **[LICENSE](file:///c:/BottleMusic/LICENSE)**:版权 `hoowhoami`
- **[ui/src-tauri/Cargo.toml](file:///c:/BottleMusic/ui/src-tauri/Cargo.toml)**:`authors = ["Ningbottle"]`
- **[ui/package.json](file:///c:/BottleMusic/ui/package.json)**:`"author": "Ningbottle"`
- **GitHub 仓库**:`Ningbottle/BottlePlayer`
- **不一致点**:LICENSE 用 `hoowhoami`,其它用 `Ningbottle`
- **推断**:`Ningbottle` 是 GitHub 账号 / 维护者名;`hoowhoami` 可能是真实姓名 / 早期账户;同一人不同 ID
- **建议**:不视作冲突,理解为人不同 ID

### 18.5 EQ 重初始化顺序(参见 [§17.4.1](#1741-eq-重复-initplayer-重初始化顺序))

- **现状**:重复 `initPlayer` 时 EQ 重初始化顺序问题
- **模糊点**:audio_proxy 启用前无影响(EQ 始终被 CORS 禁用);启用后是否仍无影响未充分验证

### 18.6 onEnded phase guard(参见 [§17.4.2](#1742-onended-phase-guard))

- **现状**:phase guard 防御性代码
- **模糊点**:理论不可能触发,但缺乏正向测试覆盖证明其不可能

### 18.7 DeepSeek API URL `/v1` 前缀(参见 [§17.4.3](#1743-deepseek-api-url-v1-前缀))

- **现状**:`ai_analysis.rs` 调用 URL 缺 `/v1` 前缀
- **模糊点**:DeepSeek 端点对带/不带 `/v1` 都响应,但这是 spec 偏差

### 18.8 `docs/` 被 gitignore

- **现状**:[CONTEXT.md § Environment](file:///c:/BottleMusic/CONTEXT.md) 明确:`docs/ is gitignored — use git add -f to track docs`
- **影响**:本 Wiki 文件 `Code-Wiki.md` 位于仓库根,**不在 docs/ 下**,所以不受影响;但 `docs/superpowers/plans/` 下的规划文件不被 git 追踪
- **建议**:不要把 Code-Wiki.md 移到 docs/(会被 ignore)

### 18.9 首次提交日期(README vs git log)

- **早期理解**:README 暗示 2026-05-09
- **git log 实际**:首次提交 **2026-02-02**(`docs: add C++ learning roadmap`)
- **推断**:05-09 可能是某个分支起点或正式开发起点,但仓库存在更早的 C++ 学习笔记
- **建议**:以 git log 为准,仓库起点 2026-02-02

### 18.10 仓库名 vs 产品名

- **仓库名**:`BottlePlayer`(GitHub URL `Ningbottle/BottlePlayer`)
- **产品名**:`BottleMusic`(README、tauri.conf.json、package.json)
- **不一致点**:两个名字混用
- **建议**:理解 `BottlePlayer` 是 GitHub 仓库名,`BottleMusic` 是产品/应用名,同一物

---

## 19. 本 Wiki 调用的 Skills 与子代理

> 本章透明地列出本 Wiki 生成过程中调用的工具,以满足用户"调用哪些 skills"的诉求。

### 19.1 调用的 Skills

| Skill | 用途 | 时机 |
|---|---|---|
| **writing-plans** | 创建正式实施规划 | 任务开始时,产出 `docs/superpowers/plans/2026-07-23-bottlemusic-code-wiki.md` |

**未调用**其它 skills(如 `test-driven-development`、`systematic-debugging`、`finishing-a-development-branch` 等),因为本任务是文档生成,非代码实现/调试。

### 19.2 调用的子代理(Task subagent_type=search)

为并行探索代码库,启动了 **4 个并行 search 子代理**:

| 子代理 | 探索范围 | 产出 |
|---|---|---|
| E1 | `native/` C++ 核心层 | CMake 库目标、C_API FFI 边界、async/storage/core services、compat_routes、stats、diagnostics/image、tests 完整素材 |
| E2 | `ui/src/` Vue 3 前端 | main.ts、App.vue、api/ 40+ 模块、views、components(aurora/newsprint/primitives/shell)、navigation、styles、tests、架构要点 |
| E3 | `ui/src-tauri/` Rust 外壳 | Cargo.toml、lib.rs、backend_api.rs、audio_proxy.rs、stats.rs、ai_analysis.rs、os_media_session.rs 完整素材 |
| E4 | `server/` Node.js 参考层 + 根元文件 | server/ 包结构、util/、module/、Dockerfile、vercel.json、根 README/CONTEXT/CHANGELOG 完整素材 |

**注**:实际启动时 E2 与 E3 合并为一个子代理(覆盖整个 ui/),故实际启动 **3 个并行子代理**(native / ui / server+root)。

### 19.3 调用的工具

- **Read**:读取关键文件(README.md、CONTEXT.md、CHANGELOG.md、package.json、Cargo.toml、CMakeLists.txt、PRIVACY.md、tauri.conf.json 等)
- **Grep**:领域术语搜索、Known Issues 定位、shell/Primitives 组件索引
- **Glob**:查找 PRIVACY.md / SECURITY.md
- **RunCommand**(PowerShell):
  - `git log --pretty=format:"%ad|%s" --date=short --no-merges`(提取时间线)
  - `git log ... | Group-Object`(月度统计)
  - `git tag -l --sort=creatordate`(标签日期)
  - `Get-Content -Tail 30`(确认文件截断位置)
  - `Measure-Object -Line`(行数)
- **Edit**:分批补全 Code-Wiki.md(每次 ≤5000 字符,遵守工具限制)
- **TodoWrite**:任务进度跟踪(9 个 todo,涵盖 W2-W6 + Self-Review)
- **AskUserQuestion**(上一会话):确认文档形式(单文件)、详尽度(极致)、规划方式(writing-plans)、探索方式(并行子代理)

### 19.4 调用的子代理产出规划文件

`docs/superpowers/plans/2026-07-23-bottlemusic-code-wiki.md`(由 writing-plans skill 产出),定义了:
- E1-E5 并行探索任务(完成)
- W1 装配骨架(完成:头部 + TOC + 快速上手)
- W2 装配核心架构章节(完成:native/ui/server + 跨层数据流)
- W3 装配时间线与改进历史章节(完成)
- W4 装配依赖关系章节(完成)
- W5 装配补充章节(完成:skills/遗留/模糊点/测试/快速上手路径)
- W6 自检与交付(进行中:本章 + 下一章)

### 19.5 工具调用统计(粗略)

| 工具 | 调用次数(估) |
|---|---|
| Read | 30+ |
| Grep | 10+ |
| Glob | 5+ |
| RunCommand | 10+ |
| Edit | 15+ |
| Task(search) | 4(规划)→ 3(实际) |
| Skill | 1(writing-plans) |
| TodoWrite | 10+ |
| AskUserQuestion | 1(上一会话) |

---

## 20. 自检与歧义声明

### 20.1 自检清单(对照 writing-plans 规划)

| 检查项 | 状态 | 备注 |
|---|---|---|
| §1 项目概览 | ✅ | 功能特性、技术栈、致谢 |
| §2 整体架构 | ✅ | ASCII 架构图、6 条核心原则 |
| §3 快速上手 | ✅ | 环境、克隆、构建、测试、发布、30 分钟导读 |
| §4 项目地图 | ✅ | 顶层目录、关键文件索引 |
| §5 native/ C++ | ✅ | CMake、C_API、async、storage、core、compat_routes、stats、diagnostics、tests |
| §6 ui/ Vue+Tauri | ✅ | package、main、App、api/、views、components、navigation、styles、Rust 外壳 |
| §7 server/ Node.js | ✅ | 定位、包、util、module、Docker、镜像表 |
| §8 跨层数据流 | ✅ | 播放时序、EQ 工作链、历史写入、Media Session、AI 分析、IPC 命令、FFI 契约 |
| §9 依赖关系 | ✅ | 内部依赖图、C++ 库矩阵、npm、Cargo、vcpkg、工具链 |
| §10 测试体系 | ✅ | 三层计数、C++ 目标、Rust、前端、设计约定 |
| §11 CI/CD | ✅ | ci.yml、release.yml、Tauri 配置、vcpkg、更新流程、子模块 CI |
| §12 项目时间线 | ✅ | 月度分布、关键里程碑、五阶段、CHANGELOG 差异 |
| §13 GitHub 改进历史 | ✅ | Merge PR、Tags、改进模式、主题演变、仓库 URL |
| §14 子项目 + 双界面 | ✅ | S1-S5 总览 + 详情、双界面重设计、Aurora turntable |
| §15 词汇表 | ✅ | 播放/统计/弹性/KuGou 业务/架构 五域 |
| §16 安全隐私 | ✅ | 本地数据、第三方发送、安全设计、清理、漏洞报告 |
| §17 已知问题 | ✅ | MFS、EQ CDN、Music Player.html、PR review 延期、其它 |
| §18 模糊点 | ✅ | 10 项不一致 / 易误解处 |
| §19 调用 Skills | ✅ | Skills、子代理、工具、规划文件、统计 |
| §20 自检 | ✅ | 本章 |

### 20.2 路径准确性

- 所有 `file:///` 链接均指向**当前仓库**(c:\BottleMusic)下的真实文件
- 行号引用(如 "CMakeLists.txt 第 232-238 行")基于本 Wiki 基线 git HEAD `22ba7951`
- 如文件后续变更,行号可能漂移,但文件路径稳定

### 20.3 快速上手路径(满足用户"怎么快速入手"诉求)

**新人 30 分钟导读**(详见 [§3.6](#36-新人-30-分钟导读)):
1. 读 [README.md](file:///c:/BottleMusic/README.md) + [CONTEXT.md](file:///c:/BottleMusic/CONTEXT.md)(10 分钟)
2. 浏览 [ui/src/main.ts](file:///c:/BottleMusic/ui/src/main.ts) + [ui/src-tauri/src/lib.rs](file:///c:/BottleMusic/ui/src-tauri/src/lib.rs)(5 分钟)
3. 浏览 [native/CMakeLists.txt](file:///c:/BottleMusic/native/CMakeLists.txt) + [native/include/EchoCAPI.h](file:///c:/BottleMusic/native/include/EchoCAPI.h)(5 分钟)
4. 读 [§8 跨层数据流](#8-跨层数据流) + [§14 子项目](#14-子项目-s1s5--双界面重设计)(10 分钟)

**新人构建路径**(详见 [§3.3](#33-三层独立构建)):
1. `git clone --recursive`
2. C++ 层:`cmake --preset bottlemusic-debug && cmake --build --preset bottlemusic-debug`
3. 前端 + Tauri:`cd ui && npm install && npm run tauri dev`

**新人改 bug 路径**:
1. 定位层:前端 → Rust → C++(用 [§9.1 依赖图](#91-三层内部依赖图) 判断)
2. 用 [§8 跨层数据流](#8-跨层数据流) 找到对应的端到端时序
3. 用 [§15 词汇表](#15-领域语言词汇表) 确认术语
4. 用 [§10 测试体系](#10-测试体系) 跑对应层的测试
5. 用 [§17 已知问题](#17-已知问题与遗留事项) + [§18 模糊点](#18-模糊点--易误解处) 排查已知陷阱

### 20.4 歧义声明

- 本 Wiki 基线 git HEAD `22ba7951`(2026-07-22),后续 commit 可能让部分内容过期
- 测试用例数为基线日期快照,会随提交增长
- 时间线基于 `git log --no-merges`,合并提交未计入月度统计
- PR 编号从 merge commit 提取,可能遗漏 squash-merge 的 PR
- "三层 deadline"、"Storage Actor" 等术语来源于 [CONTEXT.md](file:///c:/BottleMusic/CONTEXT.md),其精确实现以代码为准
- 本 Wiki 由 AI 生成,虽尽力准确但可能有疏漏;**权威来源始终是代码本身 + [CONTEXT.md](file:///c:/BottleMusic/CONTEXT.md)**

### 20.5 文档边界(本 Wiki **不**覆盖)

- 第三方依赖的内部实现(nlohmann_json、SQLite、Tauri、Vue 等)
- KuGou 服务端行为(仅描述客户端如何调用)
- 已删除的代码(MFS playback、BackendFacade 等,仅记录历史)
- 未追踪的 docs/ 内容(`docs/superpowers/` 下的 specs/plans/reports,被 gitignore)

---

> **本 Code-Wiki 至此结束**。如发现错误或过期内容,请以代码为准并提 issue 修正。