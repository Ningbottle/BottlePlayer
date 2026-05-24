# BottleMusic 注意事项

BottleMusic 是面向 Windows 10/11 x64 的音乐客户端，目标是用更轻量的技术栈提供流畅的音乐体验，播放中整进程内存 ≤ 180 MB。

---

## 1. 技术栈

### 前端（EchoMusic-tauri 分支）

| 层 | 技术 |
| --- | --- |
| 窗口壳 | Tauri 2.0（Rust，WRY WebView2） |
| UI 框架 | Vue 3（`<script setup>` Composition API） |
| 样式 | Vanilla CSS（CSS 自定义属性，无 CSS 框架） |
| 构建 | Vite 6 + pnpm 11 |
| 前后端通信 | HTTP fetch → `127.0.0.1:6609`（EchoCompatServer loopback） |
| 辅助 | `tauri-plugin-shell`（拉起 sidecar）、`@tauri-apps/api`（invoke） |

### 后端（C++，所有分支共用）

| 模块 | 职责 |
| --- | --- |
| EchoCompatServer | HTTP sidecar（`127.0.0.1:6609`），暴露酷狗业务接口 |
| EchoCore | 酷狗业务接口、DTO、错误模型、搜索/播放/歌词/歌单 |
| EchoStorage | SQLite、migration、缓存元数据、登录态、历史 |
| EchoPlayback | Media Foundation 播放状态机，URL 流式播放 |
| EchoImage | WIC 解码、磁盘缓存、内存 LRU（默认 16 MB，上限 32 MB） |
| EchoAsync | 线程池、事件队列、取消令牌、UI 线程投递 |
| EchoDiagnostics | logging、trace、memory snapshot |

### 运行时架构

```
┌────────────────────────────────┐
│  Tauri Window (WebView2)       │  ← Vue 3 + Vanilla CSS
│  fetch / tauri invoke          │
└───────────────┬────────────────┘
                │ HTTP loopback 127.0.0.1:6609
┌───────────────▼────────────────┐
│  EchoCompatServer.exe (C++)    │  ← Tauri sidecar，随窗口启停
│  EchoCore / EchoStorage /      │
│  EchoPlayback / EchoImage      │
└────────────────────────────────┘
```

---

## 2. 从 Win32 D2D 迁移到 Tauri+Vue3 的注意事项

### 保留不动

- `EchoCore`、`EchoStorage`、`EchoPlayback`、`EchoImage`、`EchoAsync`、`EchoDiagnostics`——所有 C++ 业务逻辑模块原样保留。
- `EchoCompatServer`——从 dev-only 晋升为生产 sidecar；由 Tauri `externalBin` 拉起，随主窗口关闭被 `kill()`。
- CMake 构建系统、vcpkg 依赖、`native/` 目录结构——完全不动。
- `docs/WORKLIST.zh-CN.md`——在 Tauri 分支继续推进 List 20 和后续列表。

### 被替换

| 旧（Win32+D2D） | 新（Tauri+Vue3） |
| --- | --- |
| `EchoWin32`（MainWindow / RenderPipeline / Painter / GlassPanel） | Vue 3 组件树 + Vanilla CSS |
| `ID2D1HwndRenderTarget` / `ID2D1DeviceContext` 绘制 | WebView2 内 HTML/CSS 渲染 |
| D2D Effects 玻璃面板 | CSS `backdrop-filter: blur()` + 纸纹叠加 |
| Newsprint Theme.h 颜色常量 | CSS 自定义属性（见下表） |
| `IPage` / `IBackendFacade` / `IPlaybackController` typed 接口 | Vue 组合式 API + `src/api/backend.ts` 封装 HTTP 调用 |

### Newsprint CSS token 与 Theme.h 对照

| CSS 变量 | Theme.h 常量 | 值 |
| --- | --- | --- |
| `--paper` | `theme::color::Paper()` | `#f1ead8` |
| `--paper-alt` | `theme::color::PaperAlt()` | `#ebe2cb` |
| `--paper-edge` | `theme::color::PaperEdge()` | `#d8cdb1` |
| `--ink` | `theme::color::Ink()` | `#221b12` |
| `--ink-soft` | `theme::color::InkSoft()` | `#4a3f2f` |
| `--ink-mute` | `theme::color::InkMute()` | `#847460` |
| `--ink-faint` | `theme::color::InkFaint()` | `#b5a98e` |
| `--accent` | `theme::color::Accent()` | `#a8311b` |
| `--accent-deep` | `theme::color::AccentDeep()` | `#7a2010` |
| `--glass-tint` | `theme::color::GlassTint()` | `rgba(248,243,230,0.46)` |
| `--glass-tint-2` | `theme::color::GlassTint2()` | `rgba(248,243,230,0.62)` |
| `--glass-edge` | `theme::color::GlassEdge()` | `rgba(255,252,243,0.85)` |
| `--rule` | `theme::color::Rule()` | `rgba(34,27,18,0.14)` |
| `--rule-soft` | `theme::color::RuleSoft()` | `rgba(34,27,18,0.07)` |

字体栈（CSS 与 DWrite 一致）：
```css
font-family: 'Noto Serif SC', 'EB Garamond', 'Songti SC', 'STSong', 'Times New Roman', serif;
```

---

## 3. EchoCompatServer HTTP API 约定

所有前端调用走 `src/api/backend.ts` 中的 `apiGet<T>(path, query?)` 封装。

- Base URL：`http://127.0.0.1:6609`（从 `invoke('backend_base_url')` 读取）
- 认证：无（loopback only，sidecar 由 Tauri 拉起）
- 健康检查：`GET /healthz` → `{ "status": "ok", "version": "..." }`
- 错误格式：`{ "code": "...", "message": "...", "native_not_implemented": true }` 表示接口尚未迁移

新增接口步骤：
1. 在 `native/compat_server/CompatServer.cpp` 加 HTTP route
2. `pnpm backend:build` 重编 sidecar
3. 在 `src/api/backend.ts` 加对应 wrapper 函数
4. 在 Vue 组件里调用

---

## 4. 内存预算

整进程（Tauri + WebView2 + EchoCompatServer + C++ 模块）：

| 状态 | 目标 |
| --- | --- |
| 冷启动后空闲 | ≤ 150 MB（WebView2 自身约占 60-80 MB） |
| 播放中 | ≤ 220 MB |
| 连续播放 4 小时 | 增长 ≤ 30 MB |

> 注：WebView2 比 Win32+D2D 壳多占约 60-80 MB 基线，因此内存预算相应上调。
> C++ 后端模块（EchoImage LRU、EchoPlayback 缓冲等）预算不变，参见 `WORKLIST.zh-CN.md` 历史内存基线。

---

## 5. 开发约束

- **网络、SQLite、图片解码、Media Foundation 调用一律在后台线程**；UI 线程（包括 WebView2 主线程）不阻塞。
- **EchoCore 不依赖 UI**；不持有 Tauri/WebView2/窗口句柄。
- **EchoImage 是图片唯一归口**：内存 LRU 默认 16 MB，上限 32 MB；磁盘缓存按 TTL + 容量淘汰。
- **EchoAsync 是后台任务唯一归口**：线程池规模受控；页面切换取消不可见任务。
- **引入新依赖前说明**：体积影响、替代方案、退出策略；不引入 Electron / Chromium（WebView2 由 Tauri 托管，不重复引入）。
- **沟通和文档用中文；代码标识符用英文。**
- **EchoCompatServer 是唯一前后端通信通道**；Vue 组件不直接调用 Tauri 核心 Rust 命令做业务（只用 `invoke('ping')` / `invoke('backend_base_url')` 取初始化信息）。

---

## 6. 目录速览

```
EchoMusic-tauri/ui/           ← Tauri + Vue 3 前端工作树（tauri-experiment 分支）
├── src/
│   ├── api/backend.ts        ← 唯一后端调用入口
│   ├── components/           ← Vue 组件（PlayerBar / Sidebar / HomePage 等）
│   ├── style.css             ← Newsprint CSS token
│   └── App.vue
├── src-tauri/
│   ├── src/lib.rs            ← 启动 sidecar + 关窗 kill
│   ├── binaries/             ← EchoCompatServer-<triple>.exe（.gitignore）
│   └── tauri.conf.json
└── scripts/
    ├── build-backend.ps1     ← cmake --build EchoCompatServer + sync
    └── sync-backend.ps1      ← 仅拷贝 .exe

EchoMusic-main/native/        ← C++ 后端（main 分支，所有分支共用）
├── compat_server/            ← EchoCompatServer HTTP 路由
├── echo_core/                ← 业务接口
├── echo_storage/             ← SQLite
├── echo_playback/            ← Media Foundation
├── echo_image/               ← WIC 图片
├── echo_async/               ← 线程池
└── echo_diagnostics/         ← 日志与诊断
```
