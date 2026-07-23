# Evidence Report — BottleMusic 事实核验

> 生成时间:2026-07-23
> 基线 commit:`22ba7951` (main, codex/wiki-audit worktree)
> 核验人:codex/wiki-audit 子代理
> 目的:从代码重新验证旧 Code-Wiki.md 的每个重要结论,不接受旧 Wiki 断言。证据不足标记 UNKNOWN,不删除。
> 编写规范:所有结论附**证据文件 + 行号**(行号仅作定位参考,不作长期锚点)。

## 1. 模块及运行时边界

### 1.1 三层架构(已确认)

| 层 | 入口 | 证据 |
|---|---|---|
| Vue 3 前端 | `ui/src/main.ts` | 仓库存在,`ui/src/App.vue` 挂载根组件 |
| Rust FFI 外壳 | `ui/src-tauri/src/lib.rs` | `pub fn run()` 注册 `invoke_handler!`,5 个 mod: `ai_analysis, audio_proxy, backend_api, os_media_session, stats` |
| C++ 核心 DLL | `native/core/C_API.cpp` → `EchoCAPI.dll` | CMakeLists.txt L140 `add_library(EchoCAPI SHARED core/C_API.cpp)` |

### 1.2 Tauri IPC 命令(已确认,19 个)

`lib.rs` `invoke_handler!` 注册(`lib.rs:216-236`):
- 通用:`ping`, `backend_base_url`, `get_memory_usage`, `native_request`
- audio_proxy:`audio_proxy_url`
- ai_analysis:`ai_analyze`
- stats(6):`stats_record_play`, `stats_get_summary`, `stats_get_top`, `stats_get_timeline`, `stats_get_recent`, `stats_get_recommendations`
- os_media(7):`os_media_bind`, `os_media_unbind`, `os_media_set_now_playing`, `os_media_set_playback_status`, `os_media_set_enabled_controls`, `os_media_inject_button`, `os_media_debug_snapshot`

**重要纠正**:旧 Code-Wiki.md 称主请求命令为 `echo_request` — **错误**,实际是 `native_request`(`lib.rs:64`)。命令签名为 `(method, path, query_json, headers_json, body) -> Result<String, String>`。

### 1.3 server/ 是否进入生产链路(已确认:**不进入**)

**证据**:
- `.gitmodules` 声明 `server` 为 submodule,指向 `https://github.com/MakcRe/KuGouMusicApi.git`
- 对 `ui/` 全树 grep `from '...server'`、`require('...server')`、`:3000`、`:4000`、`KuGouMusicApi` — **零命中**
- `tauri.conf.json` `bundle.resources` 仅含 `EchoCAPI.dll` 和 `sqlite3.dll`,**不含 server/**
- `release.yml` 构建步骤不构建/不打包 server/
- Rust `lib.rs` 启动流程无 server/ 进程派生

**结论**:server/ 是**纯只读参考实现**,用于翻译 KuGou API 路由到 C++ CompatApi 时对照,不进入运行时。生产链路完全通过 `EchoCAPI.dll` 的 `EchoHandleRequest` 系列 C ABI。

### 1.4 EchoImage 库的真实状态(已确认:**预留功能,非死代码**)

**证据**:
- `CMakeLists.txt:144-149` 定义 `EchoImage STATIC`,源文件 `image/ImageCache.cpp` + `image/ImageLoader.cpp`
- `CMakeLists.txt:157` 仅 `EchoNativeSmokeTests` 链接 `EchoImage`(测试用)
- **主链路不链接**:`EchoCore`(L98-138)和 `EchoCAPI`(L140-142)的 `target_link_libraries` 均不含 `EchoImage`
- 代码完整:`MemoryImageCache`(内存 LRU)+ `DiskImageCache`(磁盘 LRU)+ `ImageLoader`(WIC 解码,LoadFile/LoadRemote)
- 测试覆盖:`tests/basic_contract_tests.cpp:1117-1118` 验证 `MemoryImageCache` 默认 16MB 预算

**结论**:EchoImage 是**封面图缓存的预留基础设施**,代码完整且有测试,但尚未挂载到主请求链路。不应删除;应在 Wiki 中明确标注"当前未挂载主链路,为未来封面缓存预留"。

## 2. 无引用依赖证据(httplib / spdlog / wil)

### 2.1 vcpkg.json 声明 vs 实际使用

| 依赖 | vcpkg.json | CMakeLists find_package | target_link_libraries | 代码 #include | 结论 |
|---|---|---|---|---|---|
| `nlohmann-json` | ✓ | REQUIRED | EchoStorage, EchoCore | 全项目 | **必需** |
| `sqlite3` | ✓ | QUIET(unofficial-sqlite3 / SQLite3) | EchoStorage(条件) | `storage/Database.cpp` | **必需**(Release 强制) |
| `cpp-httplib` | ✓ | QUIET L24 | **无** | **零 #include** | **死依赖** |
| `spdlog` | ✓ | QUIET L25 | **无** | **零 #include** | **死依赖** |
| `wil` | ✓ | QUIET L26 | **无** | **零 #include** | **死依赖** |

**grep 证据**(在 `native/` 全树搜 `httplib|spdlog|wil::|include <wil|include "wil`):
- 4 行命中,全部在 `CMakeLists.txt` 和 `vcpkg.json`,**零源码 #include**

**清理候选**(按"先证据,再独立构建测试"原则):
1. 删除 `vcpkg.json` 中 `cpp-httplib`、`spdlog`、`wil` 三项
2. 删除 `CMakeLists.txt` L24-26 的 `find_package(httplib/spdlog/wil CONFIG QUIET)`
3. 每删一项独立 commit + 跑 ctest 验证

**恢复方式**:`git revert <commit>` 即可恢复 vcpkg.json + CMakeLists.txt 行。

## 3. 测试入口与真实用例统计

### 3.1 三层测试入口

| 层 | 入口 | 配置 | 证据 |
|---|---|---|---|
| C++ | `ctest --preset bottlemusic-check` | `native/CMakeLists.txt` L151-242 | 11 个 test 可执行,`enable_testing()` |
| Rust | `cargo test --lib --no-default-features` | `ui/src-tauri/Cargo.toml` | CI `ci.yml:68` 用 `--no-default-features`(避免 tray-icon 崩溃) |
| 前端 | `pnpm test` (= `vitest run`) | `ui/vitest.config.ts` | environment: jsdom,globals: true,setupFiles: `./src/test/setup.ts` |

### 3.2 真实用例统计(2026-07-23 基线)

| 层 | 统计方式 | 数量 | 说明 |
|---|---|---|---|
| C++ | `*.cpp` test 文件 | 11 | 与 CMake add_test 数量一致 |
| Rust | `#[test]` + `#[tokio::test]` 计数(src/ 目录) | 34 | 包括 lib.rs 6 + ai_analysis.rs 3 + audio_proxy.rs ~11 + backend_api.rs ~10 + stats.rs + os_media_session.rs 等 |
| Rust | 集成测试文件(tests/) | 1 | `playback_ffi_test.rs`(需 EchoCAPI.dll 存在,默认不跑) |
| 前端 | `*.test.ts` 文件 | 78 | 包括 api/、components/、views/、navigation/、test/ 各目录 |

**与旧文档对比**:
- CONTEXT.md L161-166 称 "C++ 11, Rust 22, Frontend 98, Total 131"(2026-07-03 基线) — Rust 已从 22 → 34,前端从 98 → 78 文件(用例数远多于文件数)
- 旧 Code-Wiki.md 称 "前端 917 用例"(2026-07-22 基线)— 需实际 `pnpm test` 才能核实用例数
- **本报告只记录文件数和 #[test] 计数作为可核实证据;用例数留待 §5 实际运行后填入 final-report.md**

### 3.3 Rust 测试约束(已确认)

`Cargo.toml:24-25` 注释 + `ci.yml:67-68` 注释:**`cargo test --lib` 必须带 `--no-default-features`**,否则 `desktop-shell` feature 启用 `tray-icon`,导致 `STATUS_ENTRYPOINT_NOT_FOUND` 崩溃测试 harness。

CI 同时跑 `cargo check --lib`(默认 features,L71-78)和 `cargo clippy --no-default-features -D warnings`(L80-81)作为补偿。

## 4. Playwright 是否实际使用

### 4.1 证据

- `ui/package.json:48` devDependencies: `playwright ^1.61.1`
- `ui/pnpm-lock.yaml` 显示 `playwright-core@1.61.1` + `@vitest/browser-playwright`(vitest 可选)
- `ui/scripts/capture-aurora-qa.mjs:1` `import { chromium } from 'playwright'`
- `ui/scripts/capture-stage-preview.mjs` / `capture-player-preview.mjs` 同类(截图脚本)
- **package.json `scripts` 中无 playwright 命令**(无 `test:e2e` 等)
- `vitest.config.ts` 未启用 `@vitest/browser`

### 4.2 结论

Playwright **确实在用**,但仅用于**设计 QA 截图脚本**(本地人工跑,产出 `ui/design-qa-captures/` 下的 PNG),**不是 E2E 测试**。CI 不执行 playwright。

**建议**:
- **不删除** playwright 依赖(脚本在用)
- **不添加** E2E 烟测到 CI(本轮约束:能稳定执行则添加,否则保留依赖并记录阻塞条件)— 阻塞条件:playwright 需要安装浏览器二进制(~300MB),CI 未配置 `playwright install`,且 Tauri 应用需打包后才能 E2E,本地 dev server 无法直接 E2E
- 在 `maintenance.md` 记录此阻塞条件,等 Tauri E2E 工具链成熟再启用

## 5. DeepSeek Key 真实存储生命周期

### 5.1 证据(代码事实)

| 文件 | 行 | 内容 | 含义 |
|---|---|---|---|
| `ui/src/views/StatsView.vue` | 53 | `localStorage.removeItem('deepseek_api_key')` | **模块加载时清理旧 localStorage Key** |
| `ui/src/views/StatsView.vue` | 54 | `const aiApiKey = ref('')` | 内存 ref,不持久化 |
| `ui/src/views/StatsView.vue` | 194 | `apiKey: aiApiKey.value` | 调用 `ai_analyze` 时传内存值 |
| `ui/src-tauri/src/ai_analysis.rs` | 56 | `api_key: String` | Rust 接收从前端传入,不持久化 |
| `ui/src-tauri/src/ai_analysis.rs` | 92 | `.header("Authorization", format!("Bearer {}", api_key))` | 用完即弃 |
| `ui/src/views/__tests__/StatsView.test.ts` | 191 | `localStorage.setItem('deepseek_api_key', 'legacy-secret')` | 测试用 'legacy' 字样 |
| `ui/src/views/__tests__/StatsView.test.ts` | 197, 203 | `expect(localStorage.getItem('deepseek_api_key')).toBeNull()` | 断言清理生效 |

### 5.2 结论

**当前实现**:Key **仅在当前页面会话内存**(`ref('')`),不写入 localStorage/磁盘。代码中保留 `localStorage.removeItem` 是**清理升级用户旧数据**的迁移路径。

**文档对齐**:
- `PRIVACY.md`(2026-07-17 更新)L48 "不写入磁盘或 localStorage" — **正确**
- `CONTEXT.md` S5 Details L98 "User provides API key via localStorage `deepseek_api_key`" — **过时**,需更新为"内存会话"

**处理**:在低风险改进阶段同步 CONTEXT.md L98 描述。

## 6. release / changelog 当前流程

### 6.1 Release 流程(已确认)

`release.yml` tag `v*` 触发:
1. checkout + submodule
2. pnpm install
3. vcpkg install
4. `node scripts/sync-version.mjs` 同步版本号
5. CMake build Release(`bottlemusic-release` preset)→ `EchoCAPI.dll`
6. Copy DLL 到 `ui/src-tauri/libs/`
7. `pnpm test` + `pnpm build` 验证
8. `cargo test` + `cargo clippy --all-targets -D warnings` 验证 Rust
9. `tauri-apps/tauri-action@v0` 构建 NSIS + 上传 GitHub Releases + 生成 `latest.json`

**签名**:`TAURI_SIGNING_PRIVATE_KEY` + `TAURI_SIGNING_PRIVATE_KEY_PASSWORD` 从 GitHub Secrets 读取,minisign 公钥内置 `tauri.conf.json:94`。

### 6.2 Changelog 流程(已确认:**纯手工维护,严重滞后**)

**证据**:
- `CHANGELOG.md` 15 个条目,日期范围 2026-02-03 至 2026-05-22
- git log 实际范围 2026-02-02 至 2026-07-22
- 最后条目 "2026-05-22 Frontend resilience for API calls" 早于 v1.0.0(2026-06-04)发布
- `release.yml` 无 changelog 生成步骤
- `package.json` 无 `standard-version` / `changesets` / `git-cliff` 依赖

**结论**:CHANGELOG 是**纯人工维护**,无自动化。已滞后 2 个月,后续 v1.0.0+ 的所有发布均未记录。

**处理**(约束五.5):输出 `release-tooling-rfc.md` 比较 `git-cliff`、`Release Please`、现有手工方式,不直接安装工具。

## 7. 其它核验项

### 7.1 C_API.cpp 全局状态(已确认)

`native/core/C_API.cpp:27-28`:
```cpp
std::shared_ptr<echo::core::CompatApi> api;
echo::async::RequestScheduler scheduler{4};
```
**无 `g_playback`** — CONTEXT.md L124 描述 "g_api, g_playback, g_scheduler, g_stats" 中 `g_playback` **过时**(MFS 播放栈移除后已清理)。

### 7.2 native/playback/ 目录(已确认:**不存在**)

`native/playback/` 目录在仓库中**不存在**。CONTEXT.md L127-128 描述的 `PlaybackControllerMFP.cpp` 和 `BiquadFilter.cpp` **已随 MFS 移除而删除**(2026-07-17 commit `refactor(native): remove MF playback stack and BackendFacade`)。

### 7.3 ui/src-tauri/src/playback.rs(已确认:**不存在**)

`playback.rs` 文件**不存在**。CONTEXT.md L119 "13 Tauri playback commands (unused)" **过时**。`lib.rs` mod 声明仅 5 个,无 playback。

### 7.4 README "Media Foundation" 描述(已确认:**过时**)

`README.md:98` "C++ 核心处理 KuGou API 请求调度、SQLite 统计存储与 Media Foundation 接口" — **过时**,MF 已移除。
`README.md:126` 技术栈表 "C++ 核心 | ... Media Foundation ..." — **过时**。

**处理**:在低风险改进阶段更新 README 这两行。

### 7.5 pnpm vs npm(已确认:**pnpm**)

- `ui/pnpm-lock.yaml` 存在
- `ui/pnpm-workspace.yaml` 存在
- `ui/.npmrc` 存在
- CI `ci.yml:15-17` 用 `pnpm/action-setup@v4` version 11
- `tauri.conf.json:7-9` `beforeDevCommand: "pnpm dev"`, `beforeBuildCommand: "pnpm build"`
- README/CONTEXT.md 称 `pnpm tauri dev` — **正确**

### 7.6 docs/ gitignore 策略(已确认:**整目录忽略**)

`.gitignore:110` `docs/` — 整个 docs 目录被忽略。这就是用户约束四.2 要调整的点。

**调整方案**(约束四.2:"长期 Wiki/ADR 可追踪,临时 plans/reports/captures 保持忽略"):
- 改 `docs/` 为 `docs/superpowers/`(忽略临时 plans/specs/reports)
- 新增 `docs/captures/`(忽略截图)
- `docs/wiki/`、`docs/adr/` 自然可追踪(无 ignore 规则)

### 7.7 旧 Code-Wiki.md 的其它错误

- 称主请求命令为 `echo_request` — **错**,实际 `native_request`
- 称 `tauri.conf.json bundle.targets` 为 `["nsis"]` — **错**,实际 `"all"`(L64)
- 称 `C_API.cpp` 导出 `Echo_createContext / Echo_destroyContext / Echo_defaultContext` — **需核验**,本次只读 backend_api.rs 看到的是 `EchoInitializeWithPathsV2` 系列(Lib.rs:206 hint),与旧 Wiki 描述不一致

## 8. UNKNOWN 项(证据不足,不删不改)

| 项 | 状态 | 原因 |
|---|---|---|
| EchoImage 是否计划挂载主链路 | UNKNOWN | 代码完整但无主链路引用,无 ADR 说明意图 |
| `playback_ffi_test.rs` 集成测试是否在 CI 跑 | UNKNOWN | CI 只跑 `cargo test --lib`,集成测试默认不跑,但本地 `cargo test` 会跑(需 DLL 存在) |
| `Music Player.html` 是否仍随应用分发 | UNKNOWN | 文件在仓库根,但 `tauri.conf.json` `bundle.resources` 未显式包含(仅 EchoCAPI.dll + sqlite3.dll),需核验 dist/ 是否包含 |

## 9. 核验方法学

本报告所有结论由以下工具调用得出:
- `Read`:读取 12 个核心文件(README/CONTEXT/PRIVACY/CHANGELOG/CMakeLists/Cargo.toml/package.json/lib.rs/ai_analysis.rs/backend_api.rs/tauri.conf.json/vcpkg.json/capabilities/default.json)
- `Grep`:5 次定向搜索(httplib/spdlog/wil 引用、EchoImage 引用、playwright 引用、deepseek key 引用、server 引用)
- `RunCommand`:3 次(stat 文件存在性 + 统计测试用例数)
- `LS`:1 次顶层目录结构

**未调用**子代理(本任务核验范围可控,直接读文件比并行子代理更快且可追溯)。

---

> 本报告是后续 Wiki 重建和清理决策的唯一事实来源。任何与本报告冲突的旧 Wiki 断言均以本报告为准。
