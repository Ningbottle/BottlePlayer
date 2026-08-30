# BottleMusic Architecture Remediation Implementation Plan

> **v3.1 — 以 2026-08-30 commit `55fff82e` 为 clean baseline。** v2.0 的脏工作树前提已经不存在；Phase A 已完成。v3.1 修正 v3.0 的依赖方向、Aurora resize 分支、任务红绿边界、统计口径与验证命令。

> **For agentic workers:** REQUIRED SUB-SKILL: use a task-by-task execution workflow when available. If the runtime has no execution skill, follow the Weak-model protocol below exactly. Never execute more than one Task per turn.

**Goal:** 让目录直接表达 BottleMusic 的 UI Feature、Playback、Platform、Tauri、Native 边界，并消除会持续制造生命周期与依赖方向 Bug 的所有权泄漏。不重写产品，不破坏已验证的播放/歌单链路。

**Architecture:** 保留 Vue → Tauri/Rust → C ABI → C++ 三层运行时。UI 采用「Feature 为主 + Playback 作为跨 Feature 垂直模块 + Platform 作为最外层适配器」。KuGou 协议与签名仍由 C++ Native Core 拥有。

**Tech Stack:** Vue 3、TypeScript、Vitest、Vite、Tauri 2、Rust、C++20、CMake/CTest、WinHTTP、reqwest、Web Audio API。

**Decision priority:** `目录与模块命名简单清晰` ≥ `依赖方向单一` > `具体实现技巧`。如果实现需要破坏前两项，停止并回报，不得用 workaround 掩盖架构冲突。

## Weak-model protocol

每个 Task 都是一个独立交付单元，必须从绿灯开始并以绿灯结束：

1. 先运行该 Task 的 **Preflight**；任何输出不符合 Expected 就停止。
2. 只读取该 Task 的 `Files`、`Interfaces` 和直接依赖文件；不要顺手改邻近模块。
3. 行为 Task 在同一 Task 内完成 `RED → minimal GREEN → full targeted GREEN`，不得把故意失败的测试留给下一个 Task。
4. `move` commit 只允许 `git mv`、import/path 更新和测试同路径迁移；不得改函数体、类型语义或运行时顺序。
5. `refactor` / `fix` commit 不得同时批量移动旧文件。
6. 每个 Task 完成后输出：`before tree`、`after tree`、修改文件、测试命令/结果、剩余风险；然后停止等待 review。
7. 遇到计划未定义的类型、owner、生命周期 hook、跨 Feature 调用或失败测试，停止并报告。禁止自行发明 `core/`、`utils/`、`common/`、`helpers/`。

每个 Task 开始时运行：

```powershell
git status --short --branch
git branch --show-current
```

Expected: 当前分支为 `codex/architecture-remediation`；除当前 Task 已明确产生的文件外，工作树 clean。

所有“Expected: no output”的 `rg` 都是负向 gate：**exit 1 + 空输出 = PASS**（无匹配）；exit 0 = 找到违规；exit 2 = 正则/路径错误，也必须停止。不得把 exit 2 当成“无匹配”。

---

## Global Constraints

- 基线为 commit `55fff82e`（branch `codex/architecture-remediation`），工作树 **clean**。
- 本计划文件必须被 Git 跟踪；执行 worker 不得使用本地 ignored 副本或历史 v2/v3.0 文本。
- 不删除 `codex/wip-0830-pre-architecture-remediation` 分支与对应 stash，直到整改合并且 owner 确认。
- 不按行数拆文件；只按状态所有权、生命周期、依赖方向或可独立测试的职责拆分。
- 不删除、不改写 `ui/design-qa-captures/` 的 14 个 tracked QA 证据。
- 不改变 `server/` 的 submodule 存储方式（`docs/server-strategy-rfc.md` 与 owner 约束）。
- 不使用 `*.png`、`*.ps1`、`*.txt` 等全局 ignore 规则。
- 每个 Task 必须独立结束于 targeted tests green；每个 Phase 必须通过 Vitest + vue-tsc + build（涉及 Native/Rust 时加 CTest + cargo check/test/clippy）才能进入下一 Phase。

---

# 1. Executive Decision

1. 最大问题不是文件大，而是 `ui/src/api/` 用一个无语义目录装了 47 个生产模块：Feature 状态、播放运行时、DOM/Tauri 适配、动画、数据访问全部混住。新人无法从目录判断「改歌单要动哪里」。
2. 唯一的 P0 是所有权泄漏：`playerStore.ts` 同时持有响应式状态、`new Audio()`、全局 `pagehide` handler、Tauri `invoke` 与 Backend/EQ 组装；`html5Backend.ts:140` 与 `playerStore.ts:472` 同时写 `player_volume`。
3. 播放分层本身是对的。Coordinator → Orchestrator → Backend 的职责划分可保留，`playbackPhase.ts` 是纯规则（67 行，无状态）。报告的「7 个 Source of Truth」是错误归因。
4. 报告成立的部分：`api/` 无边界、View 直接写 protocol route、AuroraHome 职责过多、Aurora rAF 里存在 forced layout。
5. 报告不成立的部分：`server/` 应删除（它是 submodule，无 build 依赖）、两套 HTTP 栈冗余（服务不同边界）、native 测试未注册（14/14 已注册）、「点播放反而暂停」（AuroraHome 已接 `isPlaybackLoading` 并显示「取消加载」）。
6. 报告漏掉的更严重问题：`normalizer.ts` 把纯 Track 模型和 `/images/audio` I/O 混在一起、`audioProxy → html5Backend` 与 `songUrlResolver → playbackOrchestrator` 的类型依赖倒置、`userStore → favoriteStore/recentPlayedStore` 的跨 Feature 耦合。
7. 接管后最先改三件事：**(a)** 把 Media Runtime 生命周期从 Store 抽出，volume persistence 收敛到单一 owner；**(b)** 按 Feature/Playback/Platform 迁移 `api/`，不新建 `core/`；**(c)** 先让 ResizeObserver 在所有分支更新尺寸、补 DPR-only 更新，再删掉 `paint()` 里每帧的 `getBoundingClientRect()`——活动 rAF 的 layout read 是可证实的重复劳动。
8. Native 与 Rust 边界本轮不重写。没有任何证据支持为「技术栈统一」消灭 C++、WinHTTP 或 reqwest。
9. `native/core/` 的 34 个 cpp 把 HTTP transport、Crypto、Json/String utils 和 15 个 `*Service.cpp` 混在一层——这是真实的命名/边界问题，但优先级低于 UI，本轮只文档化。
10. 状态：Phase A 已完成（baseline 已存档于 `docs/wiki/architecture-remediation-baseline.md`），可直接从 Phase B 执行。

---

# 2. Current Architecture Tree

实测于 commit `55fff82e`，工作树 clean。省略 `node_modules`、`target`、`out`、`vcpkg_installed` 内部文件。

图例：`✅ 合理` · `⚠️ 边界模糊` · `❌ 应整改` · `🗑 历史/废弃`

```text
BottleMusic/
├── .github/workflows/                     ✅ CI / Release gate
├── assets/icons/                           ✅ 2 个 tracked 产品图标
├── docs/                                   ✅ 36 个 tracked md
│   ├── adr/                                ✅ 0001 FFI / 0002 storage actor / 0003 audio HMR
│   ├── wiki/                               ✅ 含本轮 architecture-remediation-baseline.md
│   ├── architecture-audit.md               ✅
│   ├── server-strategy-rfc.md              ✅ server/ 保留决策的依据
│   └── superpowers/                        ⚠️ 目录被 ignore；plans/ 当前 2 tracked + 3 local ignored
├── outputs/                                ⚠️ 本地已忽略；含 workspace-archive-2026-08-30
├── server/                                 ✅ submodule (mode 160000 @ 5a58694)
│                                              只读 KuGou 协议参考，build/runtime 零依赖
├── native/                                 ✅ C++20 EchoCAPI.dll
│   ├── core/                               ⚠️ 27 flat cpp + compat_routes = 34
│   │   │                                      transport/crypto/utils/15 个 *Service.cpp 混在一层
│   │   └── compat_routes/                  ✅ 7 个路由适配已成组
│   ├── include/echo/                       ⚠️ 49 headers，其中 core/ 占 32
│   │   ├── async/ (4)  diagnostics/ (4)     ✅ 边界清楚
│   │   ├── image/ (2)  stats/ (1)           ✅
│   │   └── storage/ (6)                     ✅
│   ├── async/ (4) diagnostics/ (4)          ✅ 基础设施
│   ├── image/ (2) stats/ (1) storage/ (6)   ✅ 所有权清楚
│   ├── tests/                              ✅ 14 cpp，14/14 已在 CMakeLists 注册
│   ├── tools/                              ✅ 无 cpp，仅工具脚本
│   └── out/ vcpkg_installed/               🗑 构建/依赖产物（已 ignore）
└── ui/                                     ⚠️ 应用边界正确，src 内部需整改
    ├── design-qa-captures/                 ✅ 14 个 tracked QA 证据，必须保护
    ├── scripts/                            ✅ 6 个脚本（3 ps1 + 3 mjs）
    ├── test_url.js / test_url.cjs          🗑 生产与测试零引用的重复 protocol 实验
    ├── src-tauri/src/                      ✅ 7 个平级 rs，2786 行
    │   ├── audio_proxy.rs        (1166)    ✅ 大但单一职责：loopback 音频代理
    │   ├── backend_api.rs         (534)    ✅ DLL / C ABI adapter
    │   ├── os_media_session.rs    (334)    ✅ OS media adapter
    │   ├── lib.rs                 (321)    ✅ Tauri runtime 组装
    │   ├── stats.rs               (265)    ✅
    │   └── ai_analysis.rs         (160)    ✅
    └── src/
        ├── main.ts / App.vue               ✅ composition root
        │                                      App.vue:41 pageTransitionMode
        │                                      App.vue:251 已有 grid overlap containment
        ├── api/                            ❌ 47 生产 ts + 47 test，无语义边界
        │   ├── playerStore.ts       (667)  ❌ P0：State + new Audio + pagehide + invoke
        │   ├── playbackCommandCoordinator.ts (1015) ⚠️ mailbox/coalescing 内聚，保留
        │   ├── playbackOrchestrator.ts (475) ⚠️ 切歌事务，保留
        │   ├── playbackPhase.ts      (67)  ✅ 纯转换规则 + flagsFromPhase，无状态
        │   ├── html5Backend.ts      (308)  ❌ P0：媒体适配器写 localStorage(:41,:140)
        │   ├── normalizer.ts        (107)  ❌ 纯 Track 模型 + /images/audio I/O 混住
        │   ├── audioProxy.ts               ❌ 依赖倒置：import type ← html5Backend
        │   ├── songUrlResolver.ts          ❌ 依赖倒置：import type ← playbackOrchestrator
        │   ├── userStore.ts                ❌ 跨 Feature：import favoriteStore/recentPlayedStore
        │   ├── playHistory.ts              ❌ 跨 Feature：import userStore
        │   ├── motion.ts           (366)   ❌ shared motion 反向依赖 themeStore + navigation
        │   ├── backend.ts                  ⚠️ generic Tauri client，名称过宽
        │   ├── webAudioEq / usePlayerEq    ⚠️ EQ graph 与 Store composition 互相耦合
        │   ├── playerSync.ts               ✅ startPlayerSyncHost(:92)，只观察+转发
        │   ├── *Store.ts × 9               ❌ Feature state 集中丢进 api/
        │   └── __tests__/ (47)             ⚠️ 与生产模块脱节的集中 test 仓
        ├── views/                          ⚠️ 页面与 Feature 私有状态分离
        │   ├── HomeView.vue                ✅ 双皮肤切换真正的 owner(:30)
        │   ├── home/AuroraHome.vue  (1763) ⚠️ script 1-333 / template 335-652
        │   │                                  / scoped style 658-1763；仅 Aurora
        │   ├── home/AuroraAtmosphere.vue   ❌ paint():135 每帧 getBoundingClientRect
        │   ├── lyric/ (7) overlay/ (2)     ⚠️ Feature 私有但与 state 分居
        │   └── SettingsView.vue            ❌ 7 处直接 apiGet/apiPost
        ├── components/                     ⚠️ shared 与 Feature component 混放
        │   ├── primitives/ (4)             ✅ 真 shared UI
        │   ├── player/ (4)                 ⚠️ 属于 Playback 垂直模块
        │   ├── shell/                      ⚠️ 属于 app shell
        │   └── AddToPlaylistModal.vue      ⚠️ 实为 Library Feature 组件
        ├── navigation/                     ✅ 路由/导航生命周期，边界清楚
        ├── styles/
        │   ├── tokens.css                  ✅ 语义 token --app-bg/--surface-*/--text-*
        │   ├── progress.css                ✅
        │   └── skins/{aurora,newsprint}.css ✅ skin 已分离
        └── style.css               (1576)  ⚠️ legacy token --paper/--ink + shared + page patches
```

## 2.1 Fact ledger（v3.1 复核）

| Fact | 实测值 | 解读 |
|---|---:|---|
| `ui/src/api/` 生产模块 | 47 | 核心问题：一个目录装了 4 类不同职责 |
| `ui/src/api/__tests__/` | 47 | 测试与生产模块同样脱节 |
| `playerStore.ts` | 667 行 | Store 兼作 composition root；行数不是理由 |
| `playbackCommandCoordinator.ts` | 1015 行 | mailbox/coalescing 内聚，本轮不动 |
| `playbackPhase.ts` | 67 行 | 纯规则，**不持有运行时状态** |
| `html5Backend.ts` | 308 行 | `:41` 读、`:140` 写 `player_volume` |
| `AuroraHome.vue` | 1763 行 | 1106 行是 scoped CSS；仅实现 Aurora |
| `audio_proxy.rs` | 1166 行 | 单一 loopback proxy 职责，无泄漏 |
| `style.css` | 1576 行 | legacy token 与 `tokens.css` 语义 token 并存 |
| `native/core` cpp | 27 flat / 34 recursive | 34 含 `compat_routes/` 的 7 个 |
| `native/include` headers | 49（core 占 32） | headers 不在 `native/core/` |
| native tests | 14 cpp / **14 已注册** | 报告的「构建孤岛测试」不成立 |
| Rust src | 7 rs / 2786 行 | 平级文件，非目录 |
| 直接调 `apiGet/apiPost` | **15 个生产文件 / 34 个调用表达式** | `rg` 共命中 49 行，其中 15 行是 import；v2.0 漏了 8 个 api 层文件 |
| Git 工作树 | **clean（0 modified / 0 untracked）** | v2.0 的迁移风险前提已消失 |
| tracked QA captures | 14 | 必须保护 |
| 根目录 `*.png/*.ps1/*.txt` | **0** | 报告的「80+ 碎屑」已在 `acd55f6d` 清理完毕 |

Baseline 测试结果（`docs/wiki/architecture-remediation-baseline.md`，commit `acd55f6d`）：Vitest 86 files / 1077 tests PASS；vue-tsc PASS；build PASS；CTest 14/14 PASS；cargo test 36/36 PASS；clippy `-D warnings` PASS。

v3.1 编写完成后的 fresh frontend 复核（2026-08-30）：`pnpm test` 再次得到 **86 files / 1077 tests PASS**。本轮只改计划文档与 ignore 条目，未重跑 Native/Rust toolchain；执行 worker 仍须按 B0 在首个代码 Task 前再跑一次。

---

# 3. Target Architecture Tree

保留根级 `ui/`、`native/`、`server/`。只有一个桌面应用，不增加无收益的 `apps/ui/` 嵌套。UI 采用 Feature-first，Playback 作为跨页面垂直模块。

```text
BottleMusic/
├── .github/
├── assets/
├── docs/{adr,wiki}/
├── server/                                # submodule；只读 protocol reference
├── native/                                # 本轮不搬迁，只补边界文档
│   ├── core/{compat_routes}/
│   ├── include/echo/{core,async,diagnostics,image,stats,storage}/
│   ├── async/ diagnostics/ image/ stats/ storage/
│   └── tests/
└── ui/
    ├── design-qa-captures/
    ├── scripts/
    ├── src-tauri/src/                     # 平级 rs 保持不变
    └── src/
        ├── main.ts                        # Vite entry / composition root
        ├── App.vue                        # Application root
        │
        ├── app/                           # 应用外壳，非业务 Feature
        │   ├── navigation/                # 现有 navigation/* + transitionSession
        │   ├── lifecycle/
        │   │   └── pageLifecycle.ts       # pagehide 应用级触发；调用 playback public API
        │   ├── appearance/                # appearanceStore, themeStore
        │   ├── update/                    # skippedVersion
        │   └── shell/                     # Sidebar, Topbar, components/shell/*
        │
        ├── features/                      # 页面 + Feature 私有 state + typed gateway 同住
        │   ├── home/                      # HomeView, AuroraHome, NewsprintHome,
        │   │                              #   AuroraAtmosphere, homeViewModel,
        │   │                              #   homeFeedStore, homeEnterSession,
        │   │                              #   coverColor, homeGateway
        │   ├── library/                   # PlaylistView, HistoryView, favorite*,
        │   │                              #   AddToPlaylistModal, playlistGateway,
        │   │                              #   historyGateway, favoriteGateway
        │   ├── search/                    # SearchView, searchGateway
        │   ├── account/                   # LoginView, userStore, vipResolver,
        │   │                              #   accountEffects, accountGateway
        │   ├── settings/                  # SettingsView, EqualizerView, settingsGateway
        │   ├── lyrics/                    # LyricView, views/lyric/*, lyricsResource,
        │   │                              #   lyricFocusStore, lyricFullscreen,
        │   │                              #   useLyricFollow, lyricsGateway
        │   ├── stats/                     # StatsView, statsGateway
        │   └── overlays/                  # views/overlay/*
        │
        ├── playback/                      # 跨 Feature 垂直模块
        │   ├── index.ts                   # public API：state refs + commands + types
        │   ├── types.ts                   # Playback 自有 public types（ResolveTrackResult 等）
        │   ├── playerStore.ts             # 应用状态投影 + public command facade
        │   ├── playbackPhase.ts           # 纯 phase transition policy
        │   ├── playbackQueue.ts
        │   ├── playbackDiagnostics.ts
        │   ├── playSessionTracker.ts
        │   ├── commands/
        │   │   └── playbackCommandCoordinator.ts
        │   ├── runtime/
        │   │   ├── mediaRuntime.ts        # ★新增：<audio>/Backend/媒体 listener/HMR resource owner
        │   │   ├── playbackOrchestrator.ts
        │   │   ├── playerBackend.ts
        │   │   ├── html5Backend.ts
        │   │   └── audioLevelMonitor.ts
        │   ├── eq/
        │   │   ├── equalizerConfig.ts
        │   │   ├── eqWorkletProcessor.ts
        │   │   ├── usePlayerEq.ts
        │   │   └── webAudioEq.ts
        │   ├── sync/
        │   │   ├── playerSync.ts
        │   │   └── osMediaBridge.ts
        │   ├── fm/
        │   │   ├── fmSession.ts
        │   │   └── fmGateway.ts
        │   ├── data/
        │   │   ├── songUrlGateway.ts      # ← songUrlResolver.ts
        │   │   ├── playHistoryGateway.ts  # ← playHistory.ts
        │   │   ├── playStatsGateway.ts    # stats_record_play 的 typed Tauri adapter
        │   │   ├── coverGateway.ts        # ← normalizer.ts 的 I/O 部分
        │   │   ├── recentPlayedStore.ts
        │   │   └── playerPersistence.ts   # volume/queue 持久化唯一 owner
        │   └── components/
        │       ├── PlayerBar.vue
        │       ├── player/*
        │       ├── QueuePanel.vue
        │       ├── EqualizerPanel.vue
        │       └── coverFlight.ts
        │
        ├── platform/                      # 最外层技术适配器
        │   ├── tauri/
        │   │   ├── index.ts
        │   │   ├── invoke.ts              # @tauri-apps/api/core 唯一 raw invoke wrapper
        │   │   ├── events.ts              # emit/listen adapter
        │   │   ├── nativeClient.ts        # ← backend.ts
        │   │   ├── circuitBreaker.ts
        │   │   ├── audioProxy.ts
        │   │   ├── windows.ts             # ← overlayWindows.ts
        │   │   └── updater.ts             # updater/relaunch/open-url adapter
        │   └── storage/
        │       └── safeStorage.ts
        │
        ├── shared/                        # 禁止依赖 app/ features/ playback/ platform/
        │   ├── music/track.ts             # Track + 纯 normalizeTrack
        │   ├── media/audioSource.ts        # PreparedAudioSource；Platform/Playback 共用的中立 port type
        │   ├── motion/                    # 纯 GSAP primitives + motionProfiles
        │   └── ui/                        # components/primitives/* only
        │
        └── styles/
            ├── tokens.css
            ├── base.css                   # ← style.css，仅在归属迁移完成后改名
            ├── progress.css
            └── skins/{aurora,newsprint}.css
```

测试与模块同住：每个生产文件移动时，其 `__tests__` 同 commit 移动。迁移完成后不保留 `api/__tests__` 集中仓。

## 3.1 Naming rules

- **不创建** `core/`、`utils/`、`common/`、`helpers/` 这类不能表达所有权的新垃圾桶。
- `Gateway` = Feature/Playback 到 `platform/tauri` 的 typed use-case adapter。KuGou routes 经 `nativeClient`，Rust commands 经 `invoke.ts`；Gateway **不实现**签名与协议算法。
- `Runtime` = 持有浏览器/Tauri 资源、有显式 initialize/dispose 生命周期的对象。
- `Store` = 只持有响应式应用状态。**不得**创建 DOM、注册全局 listener、直接 invoke Tauri。
- `Feature` 只能依赖另一个 Feature 的 public `index.ts`，不得导入其内部 Store/Repository。
- `platform/` 与 `shared/` 永远不得 import `playback/`；跨层共享的纯 port type 放入 `shared/`。
- 目录名用业务词（`library`、`lyrics`、`account`），不用技术词（`stores`、`services`）。

---

# 4. Module Responsibility Table

| Module | Responsibility | Owns | Must Not Own |
|---|---|---|---|
| **Playback State** (`playback/playerStore.ts`) | 向 Vue 暴露 currentTrack / queue / phase / position / duration / volume 的响应式投影与 public commands | 应用级播放快照；`playbackPhase` 的运行时值 | `<audio>` 创建、AudioContext、Tauri invoke、HMR/pagehide listener、KuGou route |
| **Phase Policy** (`playbackPhase.ts`) | 定义合法 phase 转换与单向 flags projection | 合法转换规则与类型 | 任何运行时状态值、I/O、Vue reactive state |
| **Command Coordinator** (`playback/commands/`) | 合并/排序用户 intent，管理 ticket、waiter、pending mailbox、epoch | Command State（短生命周期） | Media element、跨窗口广播、持久化、KuGou route |
| **Playback Orchestrator** (`playback/runtime/`) | 执行一次切歌/音质切换事务：resolve → load → play → phase | 当前 transition generation、取消边界、执行顺序 | UI 展示状态、DOM 创建、command coalescing |
| **Media Runtime** (`playback/runtime/mediaRuntime.ts`) ★新增 | 建立并释放 `<audio>`、Backend、媒体事件订阅与 HMR 资源复用 | HTMLAudioElement 与 Backend 实例的**唯一**生命周期 | pagehide 策略、队列/Feature 状态、KuGou 签名、UI schema、持久化策略 |
| **Page Lifecycle** (`app/lifecycle/pageLifecycle.ts`) | 注册唯一 pagehide listener，并调用 Playback public shutdown command | 应用级 window lifecycle trigger | `<audio>`/Backend 实例、队列实现、Feature Store |
| **HTML5 Backend** (`playback/runtime/html5Backend.ts`) | 把 Runtime 命令映射到 HTMLMediaElement，发出标准媒体事件 | `src` / `currentTime` / `play` / `pause` / `readyState` 的物理状态 | **localStorage**、Pinia/Vue Store、EQ 配置持久化 |
| **EQ** (`playback/eq/`) | 管理 AudioContext、MediaElementSource、worklet、gain、lease/disconnect | Effect graph、EQ bands/preset、graph volume route | 创建/替换 `<audio>`、队列、曲目选择、KuGou route |
| **Player Sync** (`playback/sync/`) | 主窗口广播只读 snapshot；浮层发送 public playback command | Overlay mirror snapshot、Tauri event 订阅 | Command mailbox、phase transition、直接操作 Backend |
| **Playback Persistence** (`playback/data/playerPersistence.ts`) | volume / queue / loopMode / quality 的读写 | 播放偏好持久化的**唯一** owner | 媒体元素、UI 状态、协议 route |
| **Song URL Gateway** (`playback/data/songUrlGateway.ts`) | 为 Playback 暴露 `resolveTrack(track, quality)` port | UI 侧 request/response 类型边界 | V5/V6 fallback、签名、CDN 策略（属于 Native `SongUrlService`） |
| **Play Stats Gateway** (`playback/data/playStatsGateway.ts`) | 把 `PlayRecord` 发送给 Tauri `stats_record_play` | 一次 fire-and-forget IPC adapter | 播放状态、session 累计规则、DOM 生命周期 |
| **UI Features** (`features/*`) | 页面、Feature component、Feature-local state、typed gateway | View state、交互状态、Feature DTO 映射 | 签名算法、WinHTTP/reqwest、**别的 Feature 的内部 Store** |
| **Library Feature** (`features/library/`) | 用户歌单/收藏/历史的 UI use-case、空态与错误态 | Playlist/favorite/history view state 与响应映射 | Concept/Standard 签名画像、Native retry、C++ DTO 内部实现 |
| **Platform Tauri adapters** (`platform/tauri/`) | 包装 raw invoke/event/window/updater API；`nativeClient` 负责 `native_request` 序列化、超时、circuit breaker 与错误描述 | `@tauri-apps/*` import 与技术错误 | Feature route 编排、ViewModel、播放状态 |
| **Shared** (`shared/`) | 纯模型、跨层中立 port、纯 motion primitive、无业务的 UI primitive | `Track`、`PreparedAudioSource`、纯 `normalizeTrack`、GSAP helper | 任何 `app/`、`features/`、`playback/`、`platform/` 的 import（含 themeStore、navigation） |
| **Tauri Runtime** (Rust) | Window/runtime、C ABI 加载、音频代理、OS media、AI/stats adapter | Tokio/reqwest runtime、loopback proxy、DLL handle | KuGou 业务签名、Vue state、C++ service policy |
| **Native Core** (C++) | KuGou protocol/profile/signing、路由兼容、HTTP、业务服务、SQLite | KuGou protocol 与持久化的**权威**实现 | UI schema、DOM/Tauri window、音频元素 |
| **`server/` submodule** | 只读协议参考 | 固定上游 commit `5a58694` | Build/runtime 依赖、产品代码所有权 |

## 4.1 Playback ownership model

报告主张「7 个模块 = 7 个 Source of Truth」。实测这 7 个模块持有的是**不同类别**的状态。正确模型是三类所有权，每类只有一个 owner：

```text
Application State          Command State           Media Runtime State
playerStore                Coordinator             MediaRuntime / Backend / EQ
├─ currentTrack            ├─ pendingSelect        ├─ HTMLAudioElement
├─ queue / index           ├─ pendingToggle        ├─ src / readyState / paused
├─ playbackPhase (值)      ├─ waiter tickets       ├─ AudioContext / worklet
├─ position / duration     └─ epoch / mailbox      └─ subscriptions / resources
└─ volume preference
        ▲                          │                        │
        └───── events / patch ─────┴────── commands ────────┘

Persistence: playback/data/playerPersistence.ts  ← 唯一写 localStorage 的模块
Policy:      playbackPhase.ts                    ← 无状态，只定义合法边
```

真正的 Bug 源不是「模块多」，而是 owner **重叠**：

| 重叠 | 证据 | 后果 |
|---|---|---|
| Store 与 Backend 都写 `player_volume` | `playerStore.ts:472` + `html5Backend.ts:140` | EQ attach/disconnect 时 volume 可能被反向覆盖 |
| Store 与 Runtime 都触碰 `<audio>` | `playerStore.ts:272` `new Audio()`、`:334-336` addEventListener | HMR/KeepAlive 往返可能出现第二个 audio owner |
| Store 直接 `invoke` | `playerStore.ts:59` `stats_record_play` | Store 无法在无 Tauri 环境下单测 |

---

# 5. Dependency Graph

## 5.1 Target dependency direction

```text
                    main.ts / App.vue  (composition root)
                              │
        ┌─────────────────────┼─────────────────────┐
        ▼                     ▼                     ▼
     app/                 features/*            playback/  (public index.ts)
  (shell, nav,           (home, library,        ├─ playerStore / phase / queue
   appearance,            search, account,      ├─ commands/
   update)                settings, lyrics,     ├─ runtime/ (mediaRuntime → backend)
        │                 stats, overlays)      ├─ eq/  sync/  fm/  data/
        │                     │                 └─ components/
        │                     │                         │
        └──────────┬──────────┴────────────┬────────────┘
                   ▼                       ▼
              shared/                 platform/
         (music, motion, ui)      ├─ tauri/nativeClient
                                  ├─ tauri/windows
                                  ├─ tauri/audioProxy
                                  └─ storage/safeStorage
                                           │
                                           ▼  Tauri IPC
                             ui/src-tauri (Rust runtime)
                             ├─ lib.rs / window / events
                             ├─ audio_proxy.rs (reqwest)
                             ├─ ai_analysis.rs / stats.rs
                             └─ backend_api.rs ── C ABI ──┐
                                                           ▼
                                            native/ (C++20 EchoCAPI.dll)
                                            ├─ core/C_API → CompatApi
                                            ├─ core/compat_routes/ (7)
                                             ├─ core/*Service (15)
                                            ├─ core/Crypto + KuGouProfile
                                            ├─ core/HttpClient (WinHTTP)
                                            └─ storage/ (SQLite)

server/ submodule ─ ─ ─ 只读人工参考，无任何运行时依赖边
```

**允许方向（逐条理解，不是强迫经过中间层）：**

```text
app/      → feature public index / playback public index / platform / shared
features/ → playback public index / platform / shared
playback/ → platform / shared
platform/ → shared
shared/   → no upper layer
platform  → Rust → C++
```

**禁止：** `shared/ → app|features|playback|platform`；`platform/ → playback/`；`features/A → features/B` 内部实现；`playback/ → features/`。任何例外必须先改 tree/ADR，不能靠 `import type` 绕过。

## 5.2 Current violations

| 位置 | 错误依赖 | 实测证据 | 决策 |
|---|---|---|---|
| `api/playerStore.ts` | Store → DOM / Window / Tauri | `:272` `new Audio()`；`:334-336` addEventListener；`:359-366` 全局 pagehide；`:59` `invoke` | **P0** → `playback/runtime/mediaRuntime.ts` |
| `api/html5Backend.ts` | Runtime adapter → Persistence | `:41` `loadNumber('player_volume')`；`:140` `localStorage.setItem` | **P0** → Backend 只改媒体；persistence 单 owner |
| `api/normalizer.ts` | 纯模型 → Platform I/O | `:1` `import { apiGet }`；`:15` 纯 `normalizeTrack` 与 `:74` `fetchCoverImage` 同文件 | **P1** → 拆 `shared/music/track.ts` + `playback/data/coverGateway.ts` |
| `api/audioProxy.ts` | Platform → Playback 实现类型 | `:2` `import type { PreparedAudioSource } from './html5Backend'` | **P1** → 类型移到 `shared/media/audioSource.ts`；Platform 不得 import Playback |
| `api/songUrlResolver.ts` | Data gateway → Orchestrator 实现类型 | `:3` `import type { ResolveTrackResult } from './playbackOrchestrator'` | **P1** → 类型移到 `playback/types.ts` |
| `api/userStore.ts` | Account Feature → Library 内部 | `:4` `import { recentPlayedStore }`；`:5` `import { favoriteStore }` | **P1** → 改为 session event + composition root 注入 |
| `api/playHistory.ts` | Playback data → Account Feature 内部 | `:3` `import { userStore }`（仅为判断是否上传） | **P1** → 注入 session-enabled port |
| `api/motion.ts` | Shared motion → Theme + Navigation | `:3` `useThemeStore`；`:6` `transitionSession`；`:7` `../navigation/direction` | **P1** → 拆纯 primitive 与 `app/navigation` transition adapter |
| 15 个生产文件 | Feature → Generic protocol route | 34 个调用表达式；另有 15 条 import 行 | **P1** → Feature-local typed gateway |
| `views/home/AuroraAtmosphere.vue` | View → 活动 rAF 每帧强制布局 | `paint():135` 调 `syncCanvasSize()` → `:91` `getBoundingClientRect()`；ResizeObserver 已存在，但 reduced/hidden early-return 仍借 `paint()` 同步 | **P1** → Observer 先应用 contentRect + DPR-only backing update，再删除 paint rect read |
| `api/playerSync.ts` | Sync adapter → Store 内部 | 直接观察 Store 并 import command 函数 | 可接受过渡；目标改为只依赖 `playback/index.ts` |

### 直接调用 generic client 的 15 个生产文件（34 个调用表达式）

v2.0 只列了 7 个 presentation 文件，漏掉 8 个 `api/` 层文件。括号是调用表达式数，不含 import 行：

```text
Presentation layer (7 文件 / 16 调用)     →  目标 Feature gateway
  views/SettingsView.vue        (7)      →  features/settings/settingsGateway.ts
  views/LoginView.vue           (3)      →  features/account/accountGateway.ts
  views/lyric/useLyricStage.ts  (2)      →  features/lyrics/lyricsGateway.ts
  views/SearchView.vue          (1)      →  features/search/searchGateway.ts
  views/PlaylistView.vue        (1)      →  features/library/playlistGateway.ts
  views/HistoryView.vue         (1)      →  features/library/historyGateway.ts
  components/Sidebar.vue        (1)      →  features/library/playlistGateway.ts

Api layer (8 文件 / 18 调用)              →  目标位置
  api/userStore.ts              (6)      →  features/account/accountGateway.ts
  api/homeFeedStore.ts          (4)      →  features/home/homeGateway.ts
  api/favoriteStore.ts          (2)      →  features/library/favoriteGateway.ts
  api/favorite.ts               (2)      →  features/library/favoriteGateway.ts
  api/songUrlResolver.ts        (1)      →  playback/data/songUrlGateway.ts
  api/playHistory.ts            (1)      →  playback/data/playHistoryGateway.ts
  api/normalizer.ts             (1)      →  playback/data/coverGateway.ts
  api/fmSession.ts              (1)      →  playback/fm/fmGateway.ts
```

---

# 6. Report Verification

| `report_next_0830.md` 结论 | 判断 | 实际证据 | 决策 |
|---|---|---|---|
| §1.1 `server/` 是孤岛死代码，应移出主树 | **Rejected** | `git ls-files -s server` = mode `160000` @ `5a58694`，已是 submodule；`vite.config`/`package.json`/`tauri.conf.json`/`native/CMakeLists.txt` 均无 `server/` 引用 | 保留 submodule，不改存储方式；依据 `docs/server-strategy-rfc.md` |
| §1.2 根目录 80+ 截图/脚本污染版本库 | **Confirmed, already fixed** | 当前根目录 `*.png`/`*.ps1`/`*.txt` = **0**；已在 `acd55f6d` 清理并归档至 `outputs/workspace-archive-2026-08-30/` | 无剩余动作；Phase F 大幅缩减 |
| §1.3 `GetUserVip` 的 `uuid="-"` 是死变量 | **Rejected** | 该值进入 request params | 无动作 |
| §1.3 login/youth_vip 测试未注册到 CTest | **Rejected** | `tests/*.cpp` = 14；`CMakeLists.txt` 中 `add_test` = 14；逐名校验无遗漏；baseline CTest 14/14 PASS | 保留 gate |
| §2.1 AuroraAtmosphere 在活动 rAF 中 forced layout | **Confirmed**（v2.0 误判为 Need Evidence） | `paint()` 的活动循环每帧读 rect；ResizeObserver 已存在，但 v3.0 漏查其 reduced/hidden early-return 分支 | **P1 修复**：Observer 所有分支先更新尺寸、DPR 单独处理后，删除 rAF rect read；不需要 profiler，但必须 red-green |
| §2.1 `getComputedStyle` 读 `--accent` 是热点 | **Partially Correct, 严重度低** | `readAccentRGB()` `:55` 每 30 帧才真读；且 `:69` 为 `props.tint ?? readAccentRGB()`，AuroraHome 在封面取色后传入 non-null `coverTint`（`:207`），正常播放时该分支不执行 | B5 明确不改；仅在 Phase D trace 证明有实际成本时独立 red-green |
| §2.1 `attachMagnet` 每次 mousemove 读 rect | **Confirmed pattern, 严重度未证实** | `motion.ts:251` 读 rect，`:267` 绑 `mousemove` | P2 profiler gate；确认后改 pointerenter 缓存 + resize invalidation |
| §2.2 Aurora overlap 必然造成容器高度抽搐 | **Need Evidence** | `App.vue:41` 计算 `pageTransitionMode`；`:251-260` 已用 CSS Grid `grid-area: 1/1` 做 overlap containment | 不改 transition mode；先录 trace 复现 |
| §2.3 GSAP clearProps 与 Vue patch 已产生竞争 | **Need Evidence** | `motion.ts` 有 6 处 `clearProps`，但报告无稳定复现步骤 | 保留 characterization test，出现复现再修 |
| §3.1 `AuroraHome.vue` 1764 行上帝组件（含双皮肤） | **Partially Correct / Misleading** | 实际 1763 行且**仅实现 Aurora**；双皮肤选择在 `HomeView.vue:30`；658-1763 共 1106 行是 scoped CSS，script 仅 333 行 | 按职责拆（Vinyl interaction / Queue rail），不按行数；Atmosphere 已独立 |
| §3.1 `style.css` 1577 行含明确 skin section | **Misleading** | 实际 1576 行；无可整段切割的 skin section；`styles/tokens.css` 与 `styles/skins/*` 已存在，且 token 词汇不同（`style.css:5-16` `--paper/--ink` vs `tokens.css:9-16` `--app-bg/--surface-*`） | 不重建结构；先做 token/selector ownership inventory |
| §3.1 `audio_proxy.rs` 41.9KB 说明模块化失败 | **Rejected** | 1166 行全部围绕单一 loopback proxy；未发现外部业务状态 owner 泄漏 | 不按行数拆 |
| §3.1 `PlaylistService.cpp` / `SongUrlService.cpp` 过大 | **Partially Correct** | 两者确实混合 ID 解析/分页/V5-V6 fallback/签名，但它们是 KuGou protocol 的权威实现，拆分需要协议回归证据 | P2：本轮只文档化 `native/core/` 边界 |
| §3.2 按钮无 loading 态，「点播放反而暂停」 | **Rejected** | `AuroraHome.vue:270` `isHeroPlaybackLoading`；`:277` 加载中显示「取消加载」；`:282` `vinylShowsPause` 含 loading；`:287` `heroPlayLabel` 显示「正在加载…」；`homeViewModel.ts:161` 映射 `playerStore.isLoading` | 无动作。**v2.0 完全漏掉对此条的核验** |
| §3.3 7 个模块 = 7 个播放 SoT | **Misleading** | `playbackPhase.ts` 仅 67 行纯规则、零状态（只有 `LEGAL` 常量 + `canTransition`/`transitionPhase`/`flagsFromPhase`）；phase 值在 Store；Coordinator/Orchestrator/Backend/EQ 持有不同**类别**状态 | 用 §4.1 三类 ownership model；修 owner 重叠，不追求单 Store |
| §3.3 状态割裂导致 volume 恢复 Bug | **Confirmed，但归因错误** | 根因是 `html5Backend.ts:140` 与 `playerStore.ts:472` 双写同一 key，不是「模块数量多」 | **P0** Phase B 收敛 persistence owner |
| §4 两套 HTTP 栈冗余，应统一 | **Rejected** | WinHTTP 服务 KuGou protocol（签名/画像/路由兼容）；reqwest 服务 loopback audio proxy（Range/CORS/流式） | 保留；见 ADR-001 |
| §4 `sync-backend.ps1` 总复制 Check DLL | **Historical, fixed** | 当前脚本有 `-Preset` ValidateSet + hash 校验；baseline 中 `verify-sync-backend.ps1` 25 项断言在 PS7 与 PS5.1 双环境 PASS | 保留 gate |
| §6 Roadmap 第一阶段应先做死代码清扫 | **Rejected（排序错误）** | 清理已完成，且本就不是架构风险；真正阻塞是 Store 所有权泄漏 | 架构风险优先于代码整洁；清理降为最后阶段 |
| — | **Missing** | Store→DOM/Tauri、Backend→localStorage、`normalizer` 混 I/O、`audioProxy`/`songUrlResolver` 类型倒置、`userStore`→Library 内部、`playHistory`→Account 内部、`motion`→theme/navigation、15 文件 34 个直连调用 | 全部先于「大文件拆分」处理 |
| — | **Missing** | `native/core/` 把 `HttpClient`/`HttpUtils`/`Crypto`/`JsonHelpers`/`StringUtils` 与 15 个 `*Service.cpp` 混在同一层 | P2 文档化，等 churn 证据再拆 |
| — | **Missing** | `ui/test_url.js` + `ui/test_url.cjs` 生产与测试零引用的重复 protocol 实验 | P3 移除 |
| — | **Missing** | `api/__tests__/` 47 个测试集中一处，与目标模块结构脱节 | 随生产文件同 commit 迁移 |

---

# 7. Architecture Decisions

## ADR-001 — 保留 Vue → Rust/Tauri → C ABI → C++ 与两套 HTTP 栈

**Decision**

保留现有三层语言边界与两个 HTTP client。Rust 负责桌面 Runtime、窗口/事件、DLL adapter、loopback 音频代理；C++ 负责 KuGou 协议、签名、业务服务与 SQLite。

**Why**

1. C ABI 面窄且有测试覆盖（`docs/adr/0001-ffi-boundary-c-abi.md`；baseline cargo 36/36 + CTest 14/14 PASS）。
2. 两套 HTTP 栈服务不同边界：WinHTTP 承载 KuGou 签名/画像/V5-V6 fallback；reqwest 承载 Range/CORS/流式代理。二者的超时、重试、安全模型都不同。
3. 统一语言或 HTTP 栈不解决任何已确认的 Bug——全部 P0 都在 UI 所有权层。

**Alternatives**

- 全 Rust 重写 Native：协议回归风险最高，对当前 Bug 收益为零。
- audio proxy 移到 C++：放弃现成 Tokio 流式实现。
- KuGou HTTP 移到 Rust：跨 ABI 的业务 DTO 面扩大。

**Trade-off**

接受跨语言调试与三套构建工具链成本；换取现有协议资产、DLL 热替换与分层运行时稳定性。

**Migration**

不迁移实现。补边界文档（Task E5）；DLL hash gate 与 C ABI contract test 已存在。

## ADR-002 — Playback 采用分类所有权，不做「所有状态进 Store」

**Decision**

应用状态归 `playerStore`，命令状态归 Coordinator，物理媒体状态归 MediaRuntime/Backend，Effect graph 归 EQ，持久化归 `playerPersistence`。`playbackPhase.ts` 只定义规则，phase 值由 Store 保存。

**Why**

实测 Bug 风险来自 owner **重叠**（§4.1），不是模块数量。把 DOM、AudioContext、IPC 全塞进一个 Pinia Store 会让这些资源无法独立 dispose，也无法在无 Tauri 环境下单测。

**Alternatives**

- 单一 Pinia God Store：拒绝，会把 DOM/AudioContext/IPC 与状态硬耦合。
- 完全事件溯源：对本项目过重，拒绝。

**Trade-off**

开发者需理解单向 command/event 流；换取资源生命周期可测试、可释放。

**Migration**

先抽 `mediaRuntime.ts`，由 `app/lifecycle/pageLifecycle.ts` 持有 pagehide trigger，再移除 Store 对 DOM/Tauri 的直接依赖；Phase C 才建立最终 `playback/index.ts` public API。见 Phase B。

## ADR-003 — UI 采用 Feature-first + Playback vertical slice，不创建 `core/`

**Decision**

页面、Feature state、Feature gateway 同住 `features/<name>/`；Playback 作为跨 Feature 垂直模块；Tauri/storage 进 `platform/`；纯 Track/Motion/UI 进 `shared/`。

**Why**

把 47 个模块从 `api/` 搬进 `core/` 只是换名字，仍然回答不了「改歌单要动哪里」。Feature-first 让一次业务变更的半径收敛到一个目录。

**Alternatives**

- 技术层目录（`stores/ services/ components/`）：一次 Feature 变更要跨 3 个目录，拒绝。
- 每个 Feature 各自复制 playback：破坏共享播放状态，拒绝。

**Trade-off**

少数跨 Feature 类型需要 public API；通过 `shared/music/track.ts` 与各 Feature `index.ts` 管理。

**Migration**

先搬 Platform，再搬 Playback，最后逐 Feature 搬。保留 basename，路径稳定后再考虑重命名。

## ADR-004 — Native Core 是 KuGou Protocol 的唯一 Owner

**Decision**

UI Feature gateway 只封装 typed use-case 与响应映射。route、签名、profile、重试、V5/V6 fallback 的权威实现留在 C++。

**Why**

UI 需要消除 View 里的硬编码 route，但建立一个包含 KuGou 细节的 `services/kugou/` 会复制 Native 的领域边界，制造两份协议真值。

**Alternatives**

- View 继续直连 generic client：测试与变更面过宽（当前 15 文件 / 34 调用）。
- UI 重做 KuGou SDK：协议所有权重复，拒绝。

**Trade-off**

同一 use-case 在 UI 有轻量 gateway、在 Native 有 Service 实现。这是跨进程 port/adapter，不是重复业务算法。

**Migration**

按 Library → Search → Account → Settings → Lyrics → Home 顺序迁移 15 个调用文件中的 34 个调用表达式。禁止从 `server/` 复制任何代码进 UI。

## ADR-005 — 保留 `server/` submodule

**Decision**

保留 `server/` 作为 submodule（`5a58694`），继续标注 reference-only。不删除、不改存储方式、不移动。

**Why**

实测 mode `160000`，build/runtime 零引用。它为 Native 协议变更提供固定 commit 的人工对照，成本仅为 clone 体积。报告称其为「未子模块化的死代码」与事实不符。

**Alternatives**

删除、外链、移动——均违反 `docs/server-strategy-rfc.md` 与 owner 约束，拒绝。

**Trade-off**

clone/CI 少量额外成本；新人需读 README 说明。

**Migration**

无目录迁移。只校验 README/CONTRIBUTING 与实际一致。

## ADR-006 — Styles 按归属收敛，不按行号拆分

**Decision**

保留 `tokens.css`、`progress.css`、`skins/*.css` 与 AuroraHome scoped styles。先建立 CSS variable/selector ownership map；只有 Feature-specific selector 迁移完成后，才把剩余 `style.css` 改名 `styles/base.css`。

**Why**

`style.css:5-16` 用 `--paper/--ink/--rule`，`tokens.css:9-16` 用 `--app-bg/--surface-*/--text-*`。两套词汇不是逐项等价的重复，直接删除会破坏大量仍引用 legacy token 的 selector。AuroraHome 的 1106 行 scoped CSS 从未位于 `style.css`。

**Alternatives**

- 删除 `style.css` 头部 token 块：拒绝。
- 把 AuroraHome scoped CSS 合入 global `aurora.css`：扩大作用域 + specificity 风险，拒绝。

**Trade-off**

短期两套 token 词汇并存；换取无视觉回归的渐进收敛。

**Migration**

inventory → QA baseline → 迁 Feature selector → 重命名 shared base。

## ADR-007 — Native Core 本轮只文档化，不搬迁

**Decision**

保留 `native/core/` + `include/echo/core/`。补 `native/core/README.md` 记录依赖方向与 include 规则，不移动任何 cpp/header。

**Why**

`core/` 确实过宽——34 个 cpp 中 `HttpClient`/`HttpUtils`/`Crypto`/`JsonHelpers`/`StringUtils` 是基础设施，另外有 15 个 `*Service.cpp`，混在同一层；`include/echo/core/` 32 个 header 同理。但：(a) 更高风险在 UI 生命周期；(b) `compat_routes/` 已完成部分分离；(c) 全树搬迁会制造大量 CMake/include churn，收益未量化。

**Alternatives**

立即按 `protocol/ crypto/ services/ transport/` 重排：churn 大、无 cycle 证据支撑，本轮拒绝。

**Trade-off**

`core` 命名继续偏宽；用 README + include rule 约束依赖，等真实 churn/cycle 证据再拆（需要新 ADR）。

**Migration**

只新增文档。

## ADR-008 — 性能修复分两类：确定性冗余立即修，热点归因需 trace

**Decision**

把动画问题拆成两类：

- **可证实的冗余劳动** → 立即修，不需要 profiler。当前唯一一例：活动动画的 `AuroraAtmosphere.paint()` 每帧调 `syncCanvasSize()` 读 rect；尺寸变化应由 ResizeObserver 提供的 `contentRect` 驱动，DPR 变化单独更新 backing store。
- **疑似热点** → 必须先有 before/after trace。包含 `attachMagnet` 的 mousemove rect、route transition 的 CLS、GSAP/Vue patch 竞争。

**Why**

v2.0 把两类混为 `Need Evidence`，导致活动动画中确定无用的每帧 rect 读取被无限期推迟。但 v3.0 也过度简化了调用链：当前 ResizeObserver 在 reduced-motion/hidden 分支先 `paint()` 再 return，尺寸同步仍间接依赖 `paint()`。因此必须先让 Observer 在所有分支更新尺寸，并单独覆盖 DPR，再删除 rAF rect 读取。`attachMagnet` 与 transition 的严重度仍依赖真实帧数据。

**Alternatives**

- 全部等 trace：把确定性冗余也拖住，拒绝。
- 全部直接重写动画系统：无证据的大改，拒绝。

**Trade-off**

需要两套验收标准（逻辑等价性 vs trace 对比）；换取立即拿到确定收益且不误伤。

**Migration**

确定性冗余在 **Phase B5** 用同一 Task 内的 red-green 测试处理；疑似热点在 **Phase D** 采 trace 后再决定。

## ADR-009 — Phase A 已完成，基线从 clean tree 起算

**Decision**

v2.0 的 Phase A（WIP 保护 + 基线）判定为**已完成**，不再重复执行。整改从 Phase B 开始。

**Why**

实测工作树 clean（0 modified / 0 untracked）；`docs/wiki/architecture-remediation-baseline.md` 已存档 commit `acd55f6d` 的 fresh gate 结果；WIP 保留在分支 `codex/wip-0830-pre-architecture-remediation` 与对应 stash；linked worktree 已用 `git worktree remove` 移除；根目录诊断产物已归档 `outputs/workspace-archive-2026-08-30/`。v2.0 描述的 75 modified + 83 untracked 前提已不存在。

**Alternatives**

重跑 Phase A：无收益且会覆盖已有 baseline 文档，拒绝。

**Trade-off**

本计划依赖既有 baseline 文档的准确性；因此 Phase B 前需重跑一次 Vitest 快速确认基线仍绿。

**Migration**

保留 WIP 分支与 stash 至合并；Phase B 开始前只做一次 `pnpm test` 复核。

---

# 8. Remediation Roadmap

排序原则：**架构风险 > 代码整洁**。报告把「死代码大扫除」放第一位是错误排序，且该工作实测已完成。

| Priority | Phase | Outcome | Gate to enter next phase |
|---|---|---|---|
| — | ~~Phase A — Protect WIP & Baseline~~ | **已完成**（ADR-009） | baseline 文档存在 + 工作树 clean |
| **P0** | Phase B — Ownership & Deterministic Fixes | Store 不再拥有 DOM/runtime 生命周期；volume persistence 单 owner；paint 路径不再强制布局 | 全量 Vitest/vue-tsc/build 绿 + 运行时 smoke |
| **P1** | Phase C — Directory Restructure | `api/` 消失，目录表达 Feature/Playback/Platform/Shared 边界 | refactor commit 先锁测试；move commit 零语义变化；每批 import gate + tests |
| **P2** | Phase D — Evidence-Gated Performance | 只修 profiler 确认的 forced layout / CLS | before/after trace 同场景可比 |
| **P2** | Phase E — Style & Native Boundary Docs | selector/token 归属清楚；Native 依赖规则文档化 | 双皮肤 QA + 14/14 CTest + cargo gates |
| **P3** | Phase F — Residual Cleanup | 移除零引用实验代码 | `git status` 干净；无 tracked 文件误删 |

## 8.1 为什么是这个顺序

```text
Phase B  所有权修复（含确定性冗余） ← 必须先做：目录移动会掩盖行为回归
   ↓     commit 类型：test / refactor / fix
Phase C  先断错误依赖，再移动目录   ← refactor 与 move 必须分 commit
   ↓     move commit 只含路径/import；函数体零语义变化
Phase D  证据驱动性能              ← 需要稳定目录才能定位 trace 热点
Phase E  样式与文档归属            ← 需要 Feature 目录才知道 selector 去哪
Phase F  清理                      ← 最低风险，最后做
```

**硬规则：** 一个 commit 只能是行为类（`test` / `refactor` / `fix`）或 `move` 中的一种，不能兼具。违反此规则会让 review、bisect、rollback 同时失去边界。

## 8.2 Reassessment gates

- Phase B 后重新统计 Player 模块的循环依赖与失败测试；ownership gate 不稳定就不进入批量移动。
- Phase C 不以「文件数变少」为成功；以依赖方向单向 + Feature 变更半径收敛为准。
- Native 目录拆分与 Aurora 动画重写均需新的实证 ADR，不由本计划自动触发。

---

# 9. 每阶段实施方案

## Phase B — Ownership & Deterministic Fixes (P0)

### Why now

这是唯一会持续制造跨生命周期 Bug 的 Architecture Blocking。三个确定性问题，全部有源码证据：

1. `playerStore.ts` 同时持有状态、`new Audio()`、全局 pagehide、Tauri invoke、Backend/EQ 组装。
2. `html5Backend.ts:140` 与 `playerStore.ts:472` 双写 `player_volume`。
3. 活动动画的 `AuroraAtmosphere.paint()` 每帧读 `getBoundingClientRect()`；ResizeObserver 已存在，但其 reduced-motion/hidden early-return 分支仍间接依赖 `paint()` 做尺寸同步。

本 Phase 不批量移动既有文件。只允许新增职责明确的新 owner；既有文件的目录迁移留到 Phase C。

### Phase B outcome tree

```text
ui/src/
├── main.ts
├── app/
│   └── lifecycle/
│       └── pageLifecycle.ts          # 唯一 pagehide trigger
├── api/                              # 过渡路径；Phase C 再整体迁移
│   ├── mediaRuntime.ts               # 唯一 audio/backend/media-listener owner
│   ├── playStatsGateway.ts           # 唯一 stats_record_play IPC adapter
│   ├── playerPersistence.ts          # 唯一 player_volume storage owner
│   ├── playerStore.ts                # reactive state + public commands；无 DOM/Tauri
│   ├── html5Backend.ts               # media adapter；无 storage
│   └── usePlayerEq.ts                # 通过 getAudio/getVolume ports 使用 runtime
└── views/home/AuroraAtmosphere.vue   # rAF 不读取 layout；RO + DPR 各自更新尺寸
```

### Task B0 — Re-confirm baseline

**Commit type:** none

**Files:** none

**Preflight:** root status must be clean and branch must be `codex/architecture-remediation`.

```powershell
Set-Location ui
pnpm test
Set-Location ..
```

Expected: at least 86 files / 1077 tests PASS（不得低于 `docs/wiki/architecture-remediation-baseline.md`）。若失败，停止，不要修改产品代码。

### Task B1 — Lock current lifecycle behavior (green characterization only)

**Commit type:** `test`

**Files:**

- Modify `ui/src/api/__tests__/audioLifecycleOwnership.test.ts`
- Modify `ui/src/api/__tests__/playbackRuntimeCharacterization.test.ts`
- Modify `ui/src/api/__tests__/playerStore.test.ts`
- Modify `ui/src/api/__tests__/webAudioEq.test.ts`

**Required contracts:**

- HMR reuse 后只有一个 live `<audio>`，并复用当前 `src/currentTime/paused`。
- pagehide 只触发一次 queue flush + coordinator shutdown + backend shutdown。
- `playbackPhase` 写入后 `isPlaying/isLoading` 只由 `flagsFromPhase` 投影。
- EQ attach → volume change → disconnect 后恢复 Store preference。
- `vi.resetModules()` 后，没有调用 `initPlayer()` 的 orphan module 不得接管 audio/pagehide/persistence snapshot。

本 Task **不得**新增“Backend 不写 storage”测试，也不得修改产品代码；那条 red-green 契约属于 B4。所有新增 characterization test 必须立即 PASS。

```powershell
Set-Location ui
pnpm exec vitest run src/api/__tests__/audioLifecycleOwnership.test.ts src/api/__tests__/playbackRuntimeCharacterization.test.ts src/api/__tests__/playerStore.test.ts src/api/__tests__/webAudioEq.test.ts
Set-Location ..
```

Expected: all PASS。提交：

```powershell
git add ui/src/api/__tests__/audioLifecycleOwnership.test.ts ui/src/api/__tests__/playbackRuntimeCharacterization.test.ts ui/src/api/__tests__/playerStore.test.ts ui/src/api/__tests__/webAudioEq.test.ts
git commit -m "test(playback): lock media lifecycle behavior"
```

### Task B2 — Introduce Media Runtime owner

**Commit type:** `refactor`

**Files:**

- Create `ui/src/api/mediaRuntime.ts`（先在原目录抽取，Phase C 再移到 `playback/runtime/`）
- Modify `ui/src/api/playerStore.ts`
- Modify `ui/src/api/usePlayerEq.ts`
- Modify `ui/src/api/__tests__/audioLifecycleOwnership.test.ts`
- Modify `ui/src/api/__tests__/playbackRuntimeCharacterization.test.ts`
- Modify `ui/src/api/__tests__/playerStore.test.ts`
- Modify `ui/src/api/__tests__/usePlayerEq.test.ts`

**Interfaces:**

```typescript
export type MediaRuntimeShutdownReason = 'pagehide' | 'shutdown';

export interface MediaRuntimeDeps {
  initialVolume: () => number;
  createBackend: (audio: HTMLAudioElement, initialVolume: number) => PlayerBackend;
  onBackendEvent: (event: PlaybackEvent) => void;
  onDuration: (duration: number) => void;
  onFirstPlay: () => void;
  beforeHmrDetach: () => void;
}

export interface MediaRuntime {
  readonly audio: HTMLAudioElement;
  getBackend(): PlayerBackend | null;
  ensureBackend(): PlayerBackend;
  detachForHmr(): void;
  shutdown(reason: MediaRuntimeShutdownReason): Promise<void>;
}

export function getOrCreateMediaRuntime(deps: MediaRuntimeDeps): MediaRuntime;
export function getMediaRuntime(): MediaRuntime | null;
```

`MediaRuntimeDeps` 是唯一允许 Runtime 回传状态的方式；`mediaRuntime.ts` 不得 import `playerStore.ts`、Vue、Feature 或 persistence。`ensureBackend()` 必须保持当前同步建好 Backend 引用的语义；现有调用方会在同一 call stack 立即读取 Backend，禁止把它偷偷改成异步初始化。

**Steps（顺序不可调换）:**

- [ ] 将 `new Audio()`、全局复用槽、Backend 实例、Backend/audio listener 订阅与 HMR audio reuse 移入 `mediaRuntime.ts`；全局只保留一个 `__bottlemusic_media_runtime__`，不再分别发布 audio/backend owner。
- [ ] 首次 `getOrCreateMediaRuntime(deps)` 创建 audio；HMR 复用时必须依次调用旧实例捕获的 `beforeHmrDetach()`、移除旧 media/backend listeners、丢弃旧 Backend 引用但**不 pause、不清 src**，再绑定新 deps。没有调用此函数的 orphan module 不得改全局 runtime。
- [ ] 从 `PlayerState` 删除 `audio: HTMLAudioElement | null`；`backend: 'html5' | null` 可保留为 UI projection，但不得保存实例。
- [ ] `playerStore.ts` 通过 `MediaRuntimeDeps` 提供 Backend factory 与纯 callback；Backend event 仍进入现有 Store reducer。
- [ ] `beforeHmrDetach()` 只负责仍属 Store/composition 的 teardown：flush queue、detach Coordinator、dispose FM/EQ/analyser。Runtime 自己负责移除 audio listeners 与 Backend event subscription，禁止两边重复 unsubscribe。
- [ ] 将 `createPlayerEq(() => playerStore)` 改成显式 ports：

```typescript
export interface PlayerEqDeps {
  getAudio: () => HTMLAudioElement | null;
  getVolume: () => number;
  getEqEnabled: () => boolean;
  getEqBands: () => number[];
}

export function createPlayerEq(deps: PlayerEqDeps): PlayerEqApi;
```

- [ ] 把 `usePlayerEq.ts` 中全部 `getStore().audio/volume/eqEnabled/eqBands` 改为上面四个 getter；不得再持有 `PlayerEqStoreSlice`、读取 `store.audio` 或 import `playerStore.ts`。
- [ ] `disposePlayerRuntime()` 仍是应用级 shutdown 编排者：先 flush queue / dispose FM / shutdown Coordinator，再调用 `MediaRuntime.shutdown()`，最后关闭 EQ。不要把 Feature/queue/Coordinator teardown 塞进 MediaRuntime。
- [ ] pagehide 与 Tauri stats 暂留原 owner，分别由 B3 处理；本 Task 不混入其他行为修复。

**Verification:**

```powershell
rg -n 'audio:\s*HTMLAudioElement|playerStore\.audio|activeBackend\s*=' ui/src/api/playerStore.ts ui/src/api/usePlayerEq.ts
rg -n "from './playerStore'|from \"./playerStore\"" ui/src/api/mediaRuntime.ts ui/src/api/usePlayerEq.ts
Set-Location ui
pnpm exec vitest run src/api/__tests__/audioLifecycleOwnership.test.ts src/api/__tests__/playbackRuntimeCharacterization.test.ts src/api/__tests__/playerStore.test.ts src/api/__tests__/usePlayerEq.test.ts src/api/__tests__/webAudioEq.test.ts
pnpm exec vue-tsc --noEmit
Set-Location ..
```

Expected: both `rg` commands have no output; all tests and type-check PASS。提交：

```powershell
git add ui/src/api/mediaRuntime.ts ui/src/api/playerStore.ts ui/src/api/usePlayerEq.ts ui/src/api/__tests__/audioLifecycleOwnership.test.ts ui/src/api/__tests__/playbackRuntimeCharacterization.test.ts ui/src/api/__tests__/playerStore.test.ts ui/src/api/__tests__/usePlayerEq.test.ts
git commit -m "refactor(playback): extract media runtime ownership"
```

### Task B3 — Move page lifecycle and play-stats IPC out of the Store

**Commit type:** `refactor`

**Files:**

- Create `ui/src/app/lifecycle/pageLifecycle.ts`
- Create `ui/src/api/playStatsGateway.ts`
- Modify `ui/src/main.ts`
- Modify `ui/src/api/playerStore.ts`
- Modify `ui/src/api/__tests__/playerStore.test.ts`
- Create `ui/src/app/lifecycle/__tests__/pageLifecycle.test.ts`

**Interfaces:**

```typescript
// ui/src/app/lifecycle/pageLifecycle.ts
export interface PageLifecycleDeps {
  shutdownPlayback: () => Promise<void>;
}
export function installPageLifecycle(deps: PageLifecycleDeps): () => void;

// ui/src/api/playStatsGateway.ts
export function recordPlay(record: PlayRecord): void;
```

`installPageLifecycle` must be idempotent: a second install removes/replaces the prior handler. It owns the `window.pagehide` listener but never imports Store internals; `main.ts` wires it to the public `disposePlayerRuntime` command. `recordPlay` is the only module in UI allowed to invoke `stats_record_play`.

**Steps:**

- [ ] Write a failing lifecycle test proving two installs still trigger exactly one shutdown callback.
- [ ] Implement `installPageLifecycle` and wire it from `main.ts`.
- [ ] Move `invoke('stats_record_play')` into `playStatsGateway.ts`; keep fire-and-forget failure semantics.
- [ ] Remove `audioGlobal().__bottlemusic_pagehide__`, pagehide registration and `@tauri-apps` import from `playerStore.ts`.
- [ ] Run targeted tests and type-check.

```powershell
rg -n 'document\.|window\.|pagehide|@tauri-apps|invoke\(' ui/src/api/playerStore.ts
rg -n "stats_record_play" ui/src -g '*.ts'
Set-Location ui
pnpm exec vitest run src/app/lifecycle/__tests__/pageLifecycle.test.ts src/api/__tests__/playerStore.test.ts src/api/__tests__/playbackRuntimeCharacterization.test.ts
pnpm exec vue-tsc --noEmit
Set-Location ..
```

Expected: first `rg` has no output; second has exactly one production match in `api/playStatsGateway.ts` plus test fixtures. All tests PASS。提交：

```powershell
git add ui/src/app/lifecycle ui/src/main.ts ui/src/api/playStatsGateway.ts ui/src/api/playerStore.ts ui/src/api/__tests__/playerStore.test.ts
git commit -m "refactor(playback): isolate page lifecycle and stats IPC"
```

### Task B4 — Converge volume persistence to a single owner

**Commit type:** `fix`

**Files:**

- Modify `ui/src/api/html5Backend.ts`
- Modify `ui/src/api/playerPersistence.ts`
- Modify `ui/src/api/mediaRuntime.ts`
- Modify `ui/src/api/playerStore.ts`
- Modify `ui/src/api/__tests__/playerBackend.test.ts`
- Modify `ui/src/api/__tests__/playerStore.test.ts`
- Modify `ui/src/api/__tests__/playbackRuntimeCharacterization.test.ts`

**Interfaces:**

```typescript
export const PLAYER_VOLUME_KEY = 'player_volume';
export function loadPlayerVolume(): number;
export function savePlayerVolume(volume: number): void;

export interface Html5AudioBackendOptions {
  initialVolume?: number;
  // keep all existing hooks unchanged
}
```

**RED:** add a test which seeds localStorage with a conflicting value, constructs `Html5AudioBackend(audio, { initialVolume: 0.25 })`, calls `setVolume(0.4)`, and asserts `audio.volume === 0.4` while `Storage.prototype.setItem` was not called by the Backend.

```powershell
Set-Location ui
pnpm exec vitest run src/api/__tests__/playerBackend.test.ts -t "does not read or write persisted volume"
Set-Location ..
```

Expected before implementation: FAIL because the constructor reads and `setVolume` writes `player_volume`.

- [ ] 删除 `html5Backend.ts:140` 的 `localStorage.setItem('player_volume', ...)`。
- [ ] 删除 `html5Backend.ts:41` 的 `loadNumber('player_volume', ...)`；Backend 只使用 `options.initialVolume`，不 import storage。
- [ ] `playerPersistence.ts` 成为 `player_volume` 的唯一读写方；Store 初始化调用 `loadPlayerVolume()`，volume watch 调用 `savePlayerVolume()`。
- [ ] `mediaRuntime.ts` 的 Backend factory 从 `initialVolume()` 取得当前 preference 并显式传给 Backend。

**GREEN verification:**

```powershell
rg -n 'localStorage|safeSetItem|safeGetItem|loadNumber|player_volume' ui/src/api/html5Backend.ts
rg -n "player_volume" ui/src/api -g '*.ts' -g '!**/__tests__/**'
Set-Location ui
pnpm exec vitest run src/api/__tests__/playerBackend.test.ts src/api/__tests__/playerStore.test.ts src/api/__tests__/playbackRuntimeCharacterization.test.ts src/api/__tests__/webAudioEq.test.ts
pnpm exec vue-tsc --noEmit
Set-Location ..
```

Expected: first `rg` no output；第二条只允许 `playerPersistence.ts`。全部 PASS。提交：

```powershell
git add ui/src/api/html5Backend.ts ui/src/api/playerPersistence.ts ui/src/api/mediaRuntime.ts ui/src/api/playerStore.ts ui/src/api/__tests__/playerBackend.test.ts ui/src/api/__tests__/playerStore.test.ts ui/src/api/__tests__/playbackRuntimeCharacterization.test.ts
git commit -m "fix(playback): make persistence own player volume"
```

### Task B5 — Remove rAF layout reads without breaking resize branches

**Commit type:** `fix`

**Files:**

- Modify `ui/src/views/home/AuroraAtmosphere.vue`
- Create `ui/src/views/home/__tests__/auroraAtmosphereSizing.test.ts`

**Required design:**

```text
ResizeObserverEntry.contentRect ──→ applyCanvasSize(cssWidth, cssHeight, dpr)
mount one-time rect measurement ──→ applyCanvasSize(...)
paint/rAF ──→ syncDprOnly() ──→ backing-store resize only; NO layout read
```

`ResizeObserver` callback must call `applyCanvasSize` **before** checking reduced-motion/hidden/inactive branches. DPR changes must update `canvas.width/height` from cached `cssW/cssH` without reading rect.

**RED tests:**

- active mode: two rAF paints after initial measurement do not add `getBoundingClientRect` calls;
- reduced-motion resize: invoking ResizeObserver with a new `contentRect` updates canvas backing size before static paint;
- hidden/inactive resize: cached CSS size updates even when no loop starts;
- DPR-only change: backing size changes while rect-call count stays unchanged.

Run before implementation and confirm at least the rAF-read test FAILS for the expected reason:

```powershell
Set-Location ui
pnpm exec vitest run src/views/home/__tests__/auroraAtmosphereSizing.test.ts
Set-Location ..
```

- [ ] 将 canvas backing-store 写入提取为纯 `applyCanvasSize(width, height, dpr)`。
- [ ] Observer 使用 callback 的 `entry.contentRect`，并在任何 early return 之前更新尺寸。
- [ ] mount 允许一次 `getBoundingClientRect()` 初始测量。
- [ ] `paint()` 只检查 DPR 是否改变，不读取 rect。
- [ ] 本 Task **不改** `readAccentRGB()`；它属于 Phase D 的 trace-gated 项，避免混合两种行为。

**GREEN verification:**

```powershell
rg -n 'getBoundingClientRect' ui/src/views/home/AuroraAtmosphere.vue
Set-Location ui
pnpm exec vitest run src/views/home/__tests__/auroraAtmosphereSizing.test.ts src/views/home/__tests__/AuroraHome.test.ts
pnpm exec vue-tsc --noEmit
Set-Location ..
```

Expected: `getBoundingClientRect` 只有一处且仅在 mount initial measurement helper；tests PASS。提交：

```powershell
git add ui/src/views/home/AuroraAtmosphere.vue ui/src/views/home/__tests__/auroraAtmosphereSizing.test.ts
git commit -m "fix(aurora): remove layout reads from animation frames"
```

### Task B6 — Verify Phase B ownership and runtime behavior

**Commit type:** none unless verification reveals a separately tested fix

```powershell
rg -n 'HTMLAudioElement|new Audio|document\.|window\.|pagehide|@tauri-apps|invoke\(' ui/src/api/playerStore.ts
rg -n 'localStorage|safeSetItem|safeGetItem|loadNumber' ui/src/api/html5Backend.ts
rg -n "player_volume" ui/src/api -g '*.ts' -g '!**/__tests__/**'
rg -n 'getBoundingClientRect' ui/src/views/home/AuroraAtmosphere.vue
Set-Location ui
pnpm test
pnpm exec vue-tsc --noEmit
pnpm build
Set-Location ..
```

Expected: 前两条无输出；`player_volume` 只在 `playerPersistence.ts`；rect 只在 mount initial measurement helper；全量 gate exit 0。

### Runtime acceptance

- [ ] 冷启动播放、暂停、seek、切歌、切音质均可用。
- [ ] EQ OFF/ON 都推进 `currentTime`；disconnect 后 volume 与 Store preference 一致。
- [ ] 重启应用后音量恢复为上次设定值（验证 persistence 单 owner 未丢写）。
- [ ] 首页 ↔ 歌词页 KeepAlive 往返不产生第二个 audio owner。
- [ ] Aurora 首页氛围动画视觉无变化；窗口拖拽缩放时 canvas 不模糊、不错位。
- [ ] Overlay command 仍通过 public command facade，未直接调用 Backend。

### Risk & rollback boundary

| 风险 | 缓解 |
|---|---|
| Media Runtime 抽取破坏 HMR 单 owner | B1 characterization test 先锁行为；dev 下手动触发 HMR 验证 |
| pagehide 移到 app lifecycle 后重复注册 | `installPageLifecycle` 幂等测试 + main composition root 单点安装 |
| 移除 Backend 的 volume 读取导致初始音量为默认值 | Runtime factory 显式传入 Store preference；运行时 acceptance 第 3 条覆盖 |
| 移除 paint rect 后 reduced/hidden resize 失效 | Observer 必须在 early return 前应用 `contentRect`；四分支测试覆盖 |
| CSS 尺寸不变但 DPR 改变 | rAF 只比较 DPR，用缓存 cssW/cssH 更新 backing store，不读 layout |

Rollback：B1-B5 每个 commit 可独立 revert。Coordinator/Orchestrator 在本 Phase **完全不动**；不得用回滚 Phase A/WIP stash 代替 commit revert。

## Phase C — Directory Restructure (P1)

### Why now

行为所有权稳定后才建立最终目录。本 Phase 同时包含两类 Task，但**每个 commit 只能属于一类**：

- `refactor`：先建立 public facade、typed gateway、neutral port 或依赖注入；允许改接口，必须 red-green，禁止批量 `git mv`。
- `move`：只做 `git mv`、import/path 更新和测试同路径迁移；禁止改函数体、运行时顺序或 DTO 语义。

### Phase C tree delta

```text
BEFORE                              AFTER
ui/src/api/*.ts (47)                ui/src/app/
ui/src/api/__tests__/*.ts (47+)  →  ui/src/features/
ui/src/views/*                      ui/src/playback/
ui/src/components/*                 ui/src/platform/
                                     ui/src/shared/
                                     ui/src/styles/
                                     ui/src/api/  (absent)
```

每个 move Task 必须展示自己负责的 before/after subtree；不能只报告“修了 imports”。

### Task C1 — Create public facades before moving anything

**Commit type:** `refactor`（type/facade only；不得移动旧文件）

**Files:**

- Create `ui/src/playback/index.ts`
- Create `ui/src/playback/types.ts`
- Create `ui/src/platform/tauri/index.ts`
- Create `ui/src/shared/media/audioSource.ts`

`playback/index.ts` 只 re-export UI 允许使用的 state refs、command 函数与类型。**禁止** export Backend 实例、Coordinator class 实例、EQ internal node。

`shared/media/audioSource.ts` 只定义：

```typescript
export interface PreparedAudioSource {
  url: string;
  crossOriginSafe: boolean;
}
```

它不得 import Platform 或 Playback。提交前运行 `pnpm exec vue-tsc --noEmit`。

### Task C2 — Move Platform adapters (batch 1)

**Commit type:** `move`（每个文件一个 commit）

| From | To |
|---|---|
| `ui/src/api/backend.ts` | `ui/src/platform/tauri/nativeClient.ts` |
| `ui/src/api/circuitBreaker.ts` | `ui/src/platform/tauri/circuitBreaker.ts` |
| `ui/src/api/audioProxy.ts` | `ui/src/platform/tauri/audioProxy.ts` |
| `ui/src/api/overlayWindows.ts` | `ui/src/platform/tauri/windows.ts` |
| `ui/src/api/safeStorage.ts` | `ui/src/platform/storage/safeStorage.ts` |

- [ ] 一次 `git mv` 一个文件，改 import，跑该文件的 targeted test，再继续下一个。
- [ ] 测试与生产文件在**同一 commit** 内从 `api/__tests__/` 移到 `platform/**/__tests__/`。
- [ ] 本批**不得**重命名任何 export API。

### Task C3 — Break inverted type imports

**Commit type:** `refactor`（type-only；不移动旧实现）

**必须在 C4 之前完成**，否则 Playback 移动会把倒置依赖一起带走。

- [ ] 把 `PreparedAudioSource`（当前在 `html5Backend.ts`）移到 `shared/media/audioSource.ts`。
- [ ] 把 `ResolveTrackResult`（当前在 `playbackOrchestrator.ts`）移到 `playback/types.ts`。
- [ ] `platform/tauri/audioProxy.ts` 与 `api/html5Backend.ts` 都 import `shared/media/audioSource`；Platform 不得 import Playback。
- [ ] `songUrlResolver.ts` 改为 import `playback/types`，不再 import `playbackOrchestrator`。

Verification:

```powershell
rg -n "from '.*(html5Backend|playback/)" ui/src/platform
rg -n "from '.*playbackOrchestrator'" ui/src/api/songUrlResolver.ts
rg -n "from '.*(app|features|playback|platform)/" ui/src/shared
```

Expected: 三条均无输出。

### Task C4 — Move Playback vertical slice (batch 2)

**Commit type:** `move`（按下表目录分批；函数体不得变化）

| Target directory | Files to move |
|---|---|
| `playback/` | `playerStore.ts`, `playbackPhase.ts`, `playbackQueue.ts`, `playbackDiagnostics.ts`, `playSessionTracker.ts` |
| `playback/commands/` | `playbackCommandCoordinator.ts` |
| `playback/runtime/` | `mediaRuntime.ts`, `playbackOrchestrator.ts`, `playerBackend.ts`, `html5Backend.ts`, `audioLevelMonitor.ts` |
| `playback/eq/` | `equalizerConfig.ts`, `eqWorkletProcessor.ts`, `usePlayerEq.ts`, `webAudioEq.ts` |
| `playback/sync/` | `playerSync.ts`, `osMediaBridge.ts` |
| `playback/fm/` | `fmSession.ts` |
| `playback/data/` | `songUrlResolver.ts` → `songUrlGateway.ts`；`playHistory.ts` → `playHistoryGateway.ts`；`playStatsGateway.ts`, `playerPersistence.ts`, `recentPlayedStore.ts` |
| `playback/components/` | `PlayerBar.vue`, `components/player/*`, `QueuePanel.vue`, `EqualizerPanel.vue`, `coverFlight.ts` |

本批**唯一**允许的重命名是 `songUrlResolver → songUrlGateway`、`playHistory → playHistoryGateway`，因为 public facade 会隔离调用方。其余保持 basename 不变。

### Task C5a — Separate pure motion from navigation behavior

**Commit type:** `refactor`（不得移动旧文件）

- [ ] 在旧路径把 `motion.ts` 拆成「纯 motion primitive」与「navigation transition adapter」。
- [ ] 先补/修改 `motion.test.ts`、`motionProfiles.test.ts`、`transitionSession.test.ts`，锁定 reduced-motion、route direction 与 cleanup 顺序。
- [ ] 纯 motion 部分不得 import `themeStore`、`transitionSession` 或 `navigation/*`；adapter 继续留在旧路径，等 C5b 再移动。

Expected: targeted tests 与 `pnpm exec vue-tsc --noEmit` PASS。提交 `refactor(motion): separate navigation adapter from primitives`。

### Task C5b — Move app shell and shared modules

**Commit type:** `move`（函数体不得变化）

| Target | Files |
|---|---|
| `app/appearance/` | `appearanceStore.ts`, `themeStore.ts` |
| `app/navigation/` | 现有 `navigation/*`, `transitionSession.ts` + `motion.ts` 中的 route-transition part |
| `app/lifecycle/` | Phase B 已创建的 `pageLifecycle.ts`（保持原位，只更新 imports） |
| `app/update/` | `skippedVersion.ts` |
| `app/shell/` | `components/Sidebar.vue`, `components/Topbar.vue`, `components/shell/*` |
| `shared/ui/` | `components/primitives/*` **only** |
| `shared/motion/` | `motion.ts` 的纯 GSAP primitive 部分 + `motionProfiles.ts` |

- [ ] tests green 后才 `git mv` app shell/shared files，并只改 imports。
- [ ] 禁止把仍 import `themeStore`/`navigation` 的文件放进 `shared/`。
- [ ] `App.vue` 与 `main.ts` 保持在 `ui/src/` 作为 composition root，不移动。
- [ ] `AddToPlaylistModal.vue` 属于 Library Feature，**不进** `shared/ui/`。

Verification:

```powershell
rg -n "from '.*(app|features|platform)/" ui/src/shared
```

Expected: 无输出（`shared/` 不得依赖任何上层）。

### Task C6 — Split pure Track model from cover I/O

**Commit type:** `refactor`（先拆职责、测试 green；不得顺带移动 Feature）

- [ ] Create `ui/src/shared/music/track.ts`：只含 `Track` 接口与纯 `normalizeTrack`（当前 `normalizer.ts:3-73`）。
- [ ] Create `ui/src/playback/data/coverGateway.ts`：含 `fetchCoverImage` 与 `/images/audio` 调用（当前 `normalizer.ts:74+`）。
- [ ] 所有 import 迁移完成后删除 `ui/src/api/normalizer.ts`。

Acceptance:

```powershell
rg -n '^import' ui/src/shared/music/track.ts
```

Expected: 只允许 type-only 的 shared import，不得出现 `platform/`、`vue`、`@tauri-apps`。

### Task C7a — Remove Account-to-Library and Playback-to-Account edges

**Commit type:** `refactor`（只改依赖注入；不移动文件，不迁 protocol routes）

**Files:** Create `features/account/accountEffects.ts`, `features/account/__tests__/accountEffects.test.ts`, and `playback/data/__tests__/playHistoryGateway.test.ts`; modify the transitional `api/userStore.ts`, `playback/data/playHistoryGateway.ts`, `main.ts`, and `api/__tests__/userStore.test.ts`.

```typescript
// features/account/accountEffects.ts
export interface AccountEffects {
  onAccountReady(userId: string): void | Promise<void>;
  onAccountCleared(): void;
  onLocalLogout(): void;
}
export function configureAccountEffects(effects: AccountEffects): void;

// playback/data/playHistoryGateway.ts
export interface PlayHistoryPolicy {
  isUploadEnabled(): boolean;
}
export function configurePlayHistoryPolicy(policy: PlayHistoryPolicy): void;
```

- [ ] 两个 port 在未配置时使用安全 no-op/`false`，并提供 test-only reset，避免 `vi.resetModules()` 污染测试。
- [ ] 先在 `userStore.test.ts` 写 fake-effects assertions，再删除 `favoriteStore` / `recentPlayedStore` direct imports。
- [ ] 保持当前顺序：device registration 成功后 `void onAccountReady(userId)`；session 无效/检查失败触发 `onAccountCleared()`；显式 `logoutLocal()` 先 reset account（其中触发 cleared），再触发 `onLocalLogout()`。
- [ ] `main.ts` 作为 composition root 注入：ready → favorite reconcile；cleared → favorite logout cleanup；local logout → recent-played reset；history policy → `userStore.isLoggedIn`。

```powershell
rg -n 'favoriteStore|recentPlayedStore' ui/src/api/userStore.ts
rg -n 'userStore|features/account' ui/src/playback/data/playHistoryGateway.ts
Set-Location ui
pnpm exec vitest run src/api/__tests__/userStore.test.ts src/playback/data/__tests__/playHistoryGateway.test.ts src/features/account/__tests__/accountEffects.test.ts
pnpm exec vue-tsc --noEmit
Set-Location ..
```

Expected: 两条 `rg` 无输出，tests/type-check PASS。此 Task 结束前不得开始任何 Feature move。

提交：`git commit -m "refactor(account): inject cross-module effects"`，且只 stage 上述 Files。

### Task C7b — Put every Tauri package import behind platform/

**Commit type:** `refactor`（按 adapter 分 commit；不移动 View/Feature）

目标依赖树：

```text
app/ features/ playback/
           │
           ▼
platform/tauri/
├── invoke.ts     # invokeTauri<T>(command, args)
├── events.ts     # emit/listen adapter
├── windows.ts    # window/webview/position/size adapter
└── updater.ts    # checkForUpdate/relaunchApp/openExternalUrl
           │
           ▼
@tauri-apps/*
```

- [ ] 新建 `invoke.ts`、`events.ts`、`updater.ts`；扩展 C2 已移动的 `windows.ts`。名称必须保持上表，不再创建 `tauriUtils` 或 `desktopService`。
- [ ] `StatsView` 的 stats/AI commands 先移入 `features/stats/statsGateway.ts`，Gateway 只调用 `invokeTauri`。
- [ ] `playStatsGateway` 改用 `invokeTauri`；`playerSync` / `osMediaBridge` 改用 `events.ts` / `invoke.ts`。
- [ ] app shell、overlay Views 改用 `windows.ts`；Settings/Sidebar 改用 `updater.ts`。
- [ ] 每个 adapter commit 都先迁相应 mocks/tests，运行 focused Vitest 后再提交。

```powershell
rg -n '@tauri-apps' ui/src -g '*.ts' -g '*.vue' -g '!**/platform/tauri/**' -g '!**/__tests__/**'
```

Expected: no output。测试里的 mock 可暂时引用包名；生产代码只能经 `platform/tauri/`。

Commit 顺序固定为 `refactor(platform): isolate tauri commands`、`refactor(platform): isolate tauri events`、`refactor(platform): isolate tauri windows`、`refactor(platform): isolate updater actions`；每个 commit 只 stage 当前 adapter 与其调用方/tests。

### Task C7c — Replace all generic-client calls with typed gateways

**Commit type:** `refactor`（一个 Gateway 一个 commit；不移动旧 View/Store）

迁移 §5.2 的 15 个生产文件 / 34 个调用表达式。旧调用方仍在原目录，route 字符串与 response envelope 先移入目标 Gateway：

```text
features/account/accountGateway.ts
features/home/homeGateway.ts
features/library/{playlist,history,favorite}Gateway.ts
features/lyrics/lyricsGateway.ts
features/search/searchGateway.ts
features/settings/settingsGateway.ts
playback/data/{songUrl,playHistory,cover}Gateway.ts
playback/fm/fmGateway.ts
```

- [ ] 每个 Feature Gateway commit 同时创建/更新该 Feature 的 public `index.ts`，但只 export 已稳定的 Gateway/types；View 仍在旧路径时不要伪造 route export。
- [ ] `features/stats/index.ts` re-export C7b 的 `statsGateway`；`features/overlays/index.ts` 建立空的 public facade。C8 移动 View 时只追加 route-component re-export。
- [ ] 每个 Gateway 先写 contract test，再替换该组调用；每个 commit 从 green 开始并回到 green。
- [ ] Gateway 返回 View/Store 所需的明确类型，**不得**返回 `any`；`status/error/data` envelope 由 `nativeClient` 表达，Feature 再映射 View state。
- [ ] 协议签名/画像/fallback 留在 C++。**禁止**从 `server/` 复制代码进 UI。

```powershell
rg --pcre2 -n '\bapi(Get|Post)(?:<[^>]+>)?\s*\(' ui/src -g '*.ts' -g '*.vue' -g '!**/platform/tauri/nativeClient.ts' -g '!**/*Gateway.ts' -g '!**/__tests__/**'
```

Expected: no output。注意统计的是调用表达式，不是 import 行。

每个提交使用 `refactor(<module>): add typed gateway`，其中 `<module>` 取 `account|home|library|lyrics|search|settings|playback|fm`；禁止一次 stage 多个未关联 Gateway。

### Task C8 — Move Feature folders one at a time

**Commit type:** `move`（每行一个 commit；函数体、类型、runtime 顺序不得变化）

C7a-C7c 必须全部 green 后才能开始。Gateway 已在目标目录；本 Task 只搬 View/Store/component/tests 并更新 imports：

| # | Feature | Move together |
|---|---|---|
| 1 | `features/account/` | `LoginView.vue`, `userStore.ts`, `vipResolver.ts` |
| 2 | `features/library/` | `PlaylistView.vue`, `HistoryView.vue`, `favorite.ts`, `favoriteStore.ts`, `favoriteRepository.ts`, `favoriteMarkers.ts`, `AddToPlaylistModal.vue` |
| 3 | `features/search/` | `SearchView.vue` |
| 4 | `features/settings/` | `SettingsView.vue`, `EqualizerView.vue` |
| 5 | `features/lyrics/` | `LyricView.vue`, `views/lyric/*`, `lyricsResource.ts`, `lyricFocusStore.ts`, `lyricFullscreen.ts`, `useLyricFollow.ts` |
| 6 | `features/home/` | `HomeView.vue`, `views/home/*`, `homeFeedStore.ts`, `homeEnterSession.ts`, `coverColor.ts` |
| 7 | `features/stats/` | `StatsView.vue` |
| 8 | `features/overlays/` | `views/overlay/*` |

- [ ] 每行先输出 before/after subtree，再执行 `git mv`；对应 tests 同 commit 移到 Feature 内的 `__tests__/`。
- [ ] Router 只从 Feature public `index.ts` import route component。
- [ ] 每个 move commit 前后 test count 相同；任何行为测试变化都回退该 move，另开 refactor Task。
- [ ] 提交格式固定为 `refactor(<feature>): move files into feature boundary`，只 stage 当前行文件、imports 与同住 tests。

Verification:

```powershell
rg --pcre2 -n "from ['\"][^'\"]*features/[^/'\"]+/(?!index)" ui/src/features -g '*.ts' -g '*.vue'
rg -n "from ['\"].*playback/" ui/src/platform
rg -n "from ['\"].*(app|features|playback|platform)/" ui/src/shared
```

第一条必须带 `--pcre2`；默认引擎不支持 look-ahead。若当前 ripgrep 未编译 PCRE2，使用：

```powershell
rg -n "from '[^']*features/[^/']+/[^']+'" ui/src/features -g '*.ts' -g '*.vue' | rg -v "/index'"
```

Expected: 三条均无输出。Feature 内相对 import（如 `./homeGateway`）不会命中，属正常。

### Task C9 — Retire the old api/ directory

```powershell
if (Test-Path -LiteralPath ui/src/api) { rg --files ui/src/api }
rg -n "from ['\"].*api/" ui/src
```

Expected: 两条均无输出。空目录不被 Git 追踪，无需额外删除命令。

### Phase C verification

```powershell
Set-Location ui
pnpm test
pnpm exec vue-tsc --noEmit
pnpm build
Set-Location ..
```

Expected: 全部 exit 0。每个 `move` commit 前后测试数必须相同；整个 Phase C 可因 gateway/effects 的 `refactor` tests 增加测试，但不得低于 Phase B 结束值。

### Risk & rollback boundary

| 风险 | 缓解 |
|---|---|
| 大批 import 改写引入循环依赖 | 每批后跑 `vue-tsc`；C5 的 `shared/` 反向依赖检查 |
| Feature 迁移遗漏测试文件 | 每个 Feature 的 move commit 必须同时移动对应 `__tests__` |
| Gateway 类型收窄暴露既有隐式 `any` | 允许在 Gateway 内显式声明 response 类型；不允许用 `as any` 掩盖 |

Rollback：C2/C4/C5b/C8 的每个 move batch、C7a-C7c 的每个依赖/Gateway refactor 都有独立 commit。先 revert 当前 move；若失败来自前置依赖改造，再单独 revert 对应 refactor。**不回滚 Phase B 的行为修复**。

## Phase D — Evidence-Gated Performance (P2)

### Why now

Phase B5 已拿掉唯一个可证实的 rAF layout read。剩下的三项（magnet、route transition、GSAP/Vue 竞争）报告只给了静态代码模式，没有帧数据。目录稳定后再做，避免在移动中的文件上误归因。

### Task D1 — Capture reproducible traces

**Files:** Create `docs/wiki/performance-baseline-0830.md`

**Worker boundary:** 这是 human/strong-agent evidence gate，不是静态代码 Task。弱模型如果没有 Chromium DevTools Performance trace 能力，必须停止并请求用户提供三组 trace；禁止根据源码模式编造 FPS、CLS、forced-layout 数字。

固定三个场景，每个重复 3 次取中位数：

| 场景 | 操作 | 关注指标 |
|---|---|---|
| S1 Aurora 首页播放 | 连续播放 30 秒，不操作 | FPS / frame time / forced layout 次数与总时长 |
| S2 Magnet 交互 | 鼠标在 magnet control 上持续移动 10 秒 | forced layout 次数、long task |
| S3 路由切换 | Home ↔ Lyrics ↔ Settings 快速切换 20 次 | Layout Shift (CLS)、frame drop |

必须同时记录：硬件、窗口尺寸、是否 Release build、devicePixelRatio。缺任何一项则 trace 不可比。

DevTools 录制步骤固定为：Performance panel → Disable screenshots off → CPU throttling `No throttling` → Clear → Record → 执行场景 → Stop → 保存 `.json.gz`。每个场景三次，文件名 `S<id>-run<1..3>-<width>x<height>-dpr<value>.json.gz`。只把汇总表提交到 Git；原始大 trace 保存在 `outputs/performance-traces-0830/`。

### Task D2 — Apply only evidence-backed fixes

按 trace 结果逐条判定。**没有对应热点就不改。**

| 条件 | 才允许的修改 |
|---|---|
| S2 显示 `attachMagnet` 产生可观测 forced layout | `motion.ts:251` 改为 `pointerenter` 缓存 rect，ResizeObserver/scroll 时 invalidate，`pointermove` 只算 offset |
| S1 显示 accent computed style 是可观测热点 | 另开 red-green Task，由 appearance/theme state 注入 RGB prop；B5 明确没有顺带修改它 |
| S3 显示 CLS > 0 且源头在 page transition | 优先修 stack containment / height ownership；**不默认切回 `out-in`** |
| S3 显示 GSAP clearProps 与 Vue patch 产生可重现闪烁 | 先写可复现的 characterization test，再改 |

### Verification

相同硬件、窗口、操作脚本重新录 trace。

Acceptance：目标热点的 forced-layout count 与总时长下降，且 Vitest/build/视觉 QA 全部通过。**没有显著热点就在文档里记「No Change」，不提交动画重写。**

### Rollback boundary

每项修复独立 commit。任何一项可单独 revert，不影响 Phase B/C。

## Phase E — Style & Native Boundary (P2)

### Why now

Feature 目录稳定后，global CSS 中的 Feature-specific selector 才有明确去处。先拆 CSS 会把规则移到未来还要移动的目录。

### Task E1 — Build token and selector ownership inventory

**Files:** Create `docs/wiki/style-ownership.md`

```powershell
rg -o --no-filename 'var\(--[A-Za-z0-9-]+' ui/src/style.css ui/src/styles ui/src -g '*.vue' | Sort-Object -Unique
rg -n '^\s*(:root|\[data-|\.|#|@media)' ui/src/style.css
```

Inventory 必须分开记录五类：legacy tokens（`--paper/--ink/--rule`）、semantic tokens（`--app-bg/--surface-*/--text-*`）、shared selectors、Feature selectors、skin overrides。

**禁止**把名称不同的 token 标为「重复」后直接删除。两套词汇必须逐项映射后才能合并。

### Task E2 — Preserve visual baseline

**Protected files:** `ui/design-qa-captures/**`（14 个 tracked 文件，不得覆盖）

**Worker boundary:** 需要可运行的 Vite 页面、Playwright 和人工图像对比。弱模型缺少浏览器/视觉能力时必须停止并请求 strong-agent/human 完成 E2；不得只看 exit code 就宣称视觉无回归。

- [ ] 先修改 `ui/scripts/capture-aurora-qa.mjs` 接受 `--out-dir=`，默认行为保持原样；本 Phase 必须把新截图写到唯一的 `remediation-baseline-*` 子目录。

把脚本当前的 `const outDir = ...` 替换为（输出路径必须留在 `ui/` 内）：

```javascript
const uiRoot = path.resolve(__dirname, '..');
const outPrefix = '--out-dir=';
const outArg = process.argv.find((arg) => arg.startsWith(outPrefix));
const outDir = outArg
  ? path.resolve(uiRoot, outArg.slice(outPrefix.length))
  : path.join(uiRoot, 'design-qa-captures');
const relativeOut = path.relative(uiRoot, outDir);
if (relativeOut.startsWith('..') || path.isAbsolute(relativeOut)) {
  throw new Error('capture_out_dir_must_stay_inside_ui');
}
```

```powershell
# Terminal A
Set-Location ui
pnpm dev --host 127.0.0.1

# Terminal B (after Vite reports ready)
Set-Location ui
$captureStamp = Get-Date -Format 'yyyyMMdd-HHmmss'
node scripts/capture-aurora-qa.mjs "--out-dir=design-qa-captures/remediation-baseline-$captureStamp"
```

Expected: 旧 14 个 tracked 文件 hash 不变；新目录包含 Aurora light/dark/reduced-motion。Newsprint 做同尺寸人工对比。

### Task E3 — Migrate rules by owner

- Shared reset/layout/scrollbar/focus → 最终的 `ui/src/styles/base.css`。
- Skin shell/chrome → 现有 `styles/skins/aurora.css` 或 `newsprint.css`。
- Feature page 规则 → 对应 Feature 的 Vue scoped style 或 Feature-local CSS。
- `AuroraHome.vue:658-1763` 保持 scoped；只有 Vinyl/Queue 等组件真正拆出时，样式随组件移动。
- 每次只移动一组 selector，保持 import/cascade order，跑双皮肤 QA。

### Task E4 — Rename only after ownership convergence

当 `style.css` 只剩 shared rules 时：`git mv ui/src/style.css ui/src/styles/base.css`，更新 `main.ts` import。**验收不设行数阈值。**

### Task E5 — Document the native dependency direction

**Files:** Create `native/core/README.md`

必须记录：

```text
C_API
  ↓
CompatApi → compat_routes/ (7 个路由组)
  ↓
  domain Services (15 cpp): Catalog, Device, DeviceRegister, Home, Login,
                            Lyric, PlayHistory, Playlist, Privilege, Rank,
                            Search, Song, SongUrl, User, UserCloud
  ↓
infrastructure: HttpClient (WinHTTP), HttpUtils, Crypto,
                KuGouProfile, KuGouAndroidRequest, Authorization,
                JsonHelpers, StringUtils
  ↓
storage/ (SQLite)  ·  stats/  ·  async/  ·  diagnostics/  ·  image/
```

并明确写入三条禁令：Native 不得 include UI/Tauri header；Service 不得直接构造 UI schema；`server/` 只供人工对照，不得进入 build。

本 Task **不移动任何 cpp/header**（ADR-007）。

### Phase E verification

```powershell
Set-Location ui
pnpm test
pnpm exec vue-tsc --noEmit
pnpm build
Set-Location ..
git diff --check

$projectRoot = (Resolve-Path '.').Path
$vsDevCmd = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path -LiteralPath $vsDevCmd)) { throw "Visual Studio developer environment not found: $vsDevCmd" }
$nativeGate = 'call "' + $vsDevCmd + '" -arch=x64 -host_arch=x64 && cmake --preset bottlemusic-check && cmake --build --preset bottlemusic-check && ctest --preset bottlemusic-check'
Push-Location native
cmd.exe /d /c $nativeGate
if ($LASTEXITCODE -ne 0) { Pop-Location; throw "native gate failed: $LASTEXITCODE" }
Pop-Location
$env:PATH = "$projectRoot\native\out\bottlemusic-check;$projectRoot\native\vcpkg_installed\x64-windows\bin;$env:PATH"
Push-Location ui/src-tauri
cargo test --lib --no-default-features -- --test-threads=1
cargo check --lib
cargo clippy --no-default-features -- -D warnings
Pop-Location
```

Acceptance：两 skin × 两 mode 无可见回归；QA captures 仍 tracked；无未定义 CSS variable 警告；reduced-motion 保持；CTest 14/14；cargo gates PASS。

### Rollback boundary

CSS 按 selector 组提交，每组可单独 revert。`native/core/README.md` 是纯新增文档，零风险。

## Phase F — Residual Cleanup (P3)

### Why last

报告把清理放第一位是错误排序。且实测根目录 `*.png`/`*.ps1`/`*.txt` 已为 0，`.pnpm-store/` 与 `.worktrees/` 已在 `.gitignore`，linked worktree 已移除。v2.0 的 Phase G 大部分任务已作废。

### Task F1 — Remove zero-reference experiment files

**Files:** `ui/test_url.js`, `ui/test_url.cjs`

- [ ] 先确认零引用：

```powershell
rg -n 'test_url' ui --glob '!test_url.*'
```

Expected: 无输出。有输出则停止，先处理引用方。

- [ ] 确认后删除两个文件。它们是重复的 protocol 实验代码，协议所有权属于 C++（ADR-004）。
- [ ] **不要**把其中的公开 protocol 常量当作用户凭据泄露来写 commit message。真实问题是零引用的重复实验代码。

```powershell
Remove-Item -LiteralPath ui/test_url.js,ui/test_url.cjs
git add ui/test_url.js ui/test_url.cjs
git commit -m "chore(ui): remove unreferenced protocol experiments"
```

### Task F2 — Classify remaining local artifacts, do not bulk-delete

| 对象 | 当前状态 | 动作 |
|---|---|---|
| `ui/design-qa-captures/` | 14 个 tracked | **保留，不改** |
| `docs/superpowers/plans/` | 目录被 ignore；当前 2 tracked + 3 local ignored | 逐文件评审；**禁止整目录删除** |
| `outputs/workspace-archive-2026-08-30/` | 已 ignore 的归档 | 保留至整改合并后，由 owner 决定 |
| 根级计划/报告 | 本 `0830-architecture-remediation-plan.md` 已 tracked；`0830-plan-corrections.md`、`0830-post-fix-audit.md`、`report_next_0830.md` 仍逐文件 ignore | 本计划随代码 review；其他本地材料需长期保留时转写入 `docs/adr/` 或 `docs/wiki/` |
| `codex/wip-0830-pre-architecture-remediation` + stash | 恢复边界 | **合并前不得删除** |

### Task F3 — Do not add broad ignore rules

当前 `.gitignore` 已含 `/.pnpm-store/`、`.worktrees/`、`/outputs/` 与其他本地计划文档规则。本 v3.1 计划例外：必须被 Git 跟踪。根目录诊断产物已清零，**本轮不需要新增通配 ignore 规则**。

禁止加入 `*.png`、`*.ps1`、`*.txt` 等全局通配符：会误伤 `assets/icons/`（2 个 tracked）、`ui/design-qa-captures/`（14 个 tracked）与 `ui/scripts/*.ps1`（3 个生产构建脚本）。

### Phase F verification

```powershell
git status --short
git ls-files assets/icons ui/design-qa-captures ui/scripts
git check-ignore --no-index -v assets/icons/icon.png ui/design-qa-captures/aurora-home-1280x720-dark.png ui/scripts/sync-backend.ps1
git diff --check
```

Expected: icons、QA captures、scripts 仍 tracked；`git check-ignore --no-index` **不应**报告任何规则将它们忽略；无 whitespace error。没有 `--no-index` 时 Git 会跳过 tracked 文件，不能作为此 gate。

### Rollback boundary

F1 删除的文件可从 Git 历史恢复。F2/F3 不执行不可逆操作。

---

# 10. Things We Should NOT Do

以下重构看起来合理，实际不应该做。每条附实测理由。

1. **不要把 `api/` 原样改名为 `core/`。** 47 个模块换个目录名仍无法回答「改歌单要动哪里」。同理拒绝 `utils/`、`common/`、`helpers/`。

2. **不要把所有播放状态、DOM、Backend、EQ 塞进一个 Pinia Store。** 单 Store 不等于单一所有权。真正的修法是 §4.1 的三类 ownership。

3. **不要宣称 `playbackPhase.ts` 是运行时 SoT。** 它只有 67 行，内容是 `LEGAL` 常量 + 3 个纯函数，零状态。

4. **不要重写 Coordinator / Orchestrator。** 1015 + 475 行的 command coalescing 与切歌事务已有测试覆盖。只修 owner 泄漏。

5. **不要为统一技术栈消灭 C++、WinHTTP、Rust 或 reqwest。** 两套 HTTP 栈服务不同边界（ADR-001）。当前所有 P0 都在 UI 层，跟语言数量无关。

6. **不要按 1763 / 1576 / 1166 / 1015 这些行数拆文件。** 只有独立 owner、生命周期或依赖边界才触发拆分。`audio_proxy.rs` 1166 行但职责单一，不动。

7. **不要把 AuroraHome 当作双皮肤组件。** 双皮肤选择在 `HomeView.vue:30`，AuroraHome 只实现 Aurora。报告在这一点上会误导拆分方向。

8. **不要删除 `style.css` 头部的 legacy token 块。** `--paper/--ink/--rule` 与 `tokens.css` 的 `--app-bg/--surface-*` 名称取值均不等价，必须先逐项迁引用。

9. **不要把 AuroraHome 的 1106 行 scoped CSS 合入 global `aurora.css`。** 这会扩大作用域并推高 specificity。

10. **不要在没有 trace 前重写 `attachMagnet`、route transition 或 GSAP 集成。** 静态代码模式只能证明风险。（但 `paint()` 的每帧 rect 读取是例外：在 Observer 先覆盖 reduced/hidden 分支并补 DPR-only 更新后，它是逻辑上确定的冗余，见 ADR-008。）

11. **不要删除、移动或改变 `server/` submodule 的存储方式。** 它已是 mode `160000`，build/runtime 零引用。报告称其为「未子模块化的死代码」是事实错误。

12. **不要使用 `*.png` / `*.ps1` / `*.txt` 全局 ignore。** 会误伤 2 个产品 icon、14 个 QA capture、3 个生产构建脚本。

13. **不要重跑 v2.0 的 Phase A。** 工作树已 clean，baseline 已存档，WIP 已入分支。重跑只会覆盖现有证据（ADR-009）。

14. **不要删除 `codex/wip-0830-pre-architecture-remediation` 分支或对应 stash。** 它们是唯一的 pre-remediation 恢复边界。

15. **不要在同一个 commit 里同时做目录移动和行为修复。** 这会让 review、bisect、rollback 同时失去边界（§8.1 硬规则）。

16. **不要先做 Phase C 再做 Phase B。** 目录移动会掩盖行为回归，使 `git bisect` 失效。

17. **不要为了消除 View 中的 route 而在 UI 建立 `services/kugou/`。** 那会复制 Native 的领域边界，制造两份协议真值（ADR-004）。Gateway 只做 typed use-case。

18. **不要现在重排 `native/core/`。** 它确实过宽（transport + crypto + utils + 15 个 `*Service.cpp` 同层），但未有 cycle/churn 证据，且会制造大量 CMake/include churn（ADR-007）。

19. **不要把 `ui/test_url.*` 的公开 protocol 常量夸大成用户凭据泄露。** 真实问题是零引用的重复实验代码与协议所有权漂移。

20. **不要用以下指标判定成功：** 「文件数变少」「大文件变短」「CSS 文件数增加」「`api/` 目录消失」。真正的成功指标是：**owner 唯一、依赖单向、Feature 变更半径收敛、测试与产品行为保持**。

---

## Success Criteria

整改完成时，下列命令按注释满足 expected（除最后一条应恰好命中一处外，其余均无输出）：

```powershell
# Store 不再拥有 DOM / Tauri
rg -n 'new Audio|addEventListener\(.pagehide|@tauri-apps' ui/src/playback/playerStore.ts
# Backend 不再写持久化
rg -n 'localStorage' ui/src/playback/runtime/html5Backend.ts
# shared 不反向依赖
rg -n "from ['\"].*(app|features|playback|platform)/" ui/src/shared
# platform 不依赖 playback 实现
rg -n "from ['\"].*playback/" ui/src/platform
# Tauri package import 只在 platform
rg -n '@tauri-apps' ui/src -g '*.ts' -g '*.vue' -g '!**/platform/tauri/**' -g '!**/__tests__/**'
# 旧目录已消失（目录不存在时不把 rg path error 当成 PASS）
if (Test-Path -LiteralPath ui/src/api) { rg --files ui/src/api }
# 展示层不再直连 generic client
rg -n 'apiGet|apiPost' ui/src/app ui/src/features ui/src/playback -g '!**/*Gateway.ts' -g '!**/__tests__/**'
# canvas layout read 只允许 mount 初始化 helper 一处；rAF/paint 不得调用
rg -n 'getBoundingClientRect' ui/src/features/home/AuroraAtmosphere.vue
#   ↑ Expected: exactly one match in the mount initial-measurement helper
```

并且：

| Gate | 预期 |
|---|---|
| `pnpm test` | ≥ 1077 tests PASS（不得低于 baseline） |
| `pnpm exec vue-tsc --noEmit` | exit 0 |
| `pnpm build` | exit 0 |
| `ctest --preset bottlemusic-check` | 14/14 PASS |
| `cargo test --lib --no-default-features` | 36/36 PASS |
| `cargo clippy --no-default-features -- -D warnings` | exit 0 |
| 双皮肤 × 双模式视觉 QA | 无可见回归 |

---

*Version: v3.1 · 2026-08-30 · Baseline: commit `55fff82e`，clean tree · Status: Phase A complete, ready to execute Task B0*
