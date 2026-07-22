# 贡献指南

感谢你考虑为 BottleMusic 贡献代码或文档。本文件描述如何在本地搭建开发环境、运行三层测试、提交符合 CI 规范的改动，以及在改动时需要遵守的架构与安全约束。

> 本仓库的架构事实以 [docs/wiki/](./docs/wiki/) 下的 Code Wiki 为准；其中 [docs/wiki/evidence-report.md](./docs/wiki/evidence-report.md) 记录了从代码核验得到的关键证据。若文档与代码不一致，以代码为准，并请顺手修订文档。

## 1. 项目概览

BottleMusic 是面向 Windows 10/11 x64 的非官方桌面客户端，采用三层架构：

| 层 | 目录 | 技术 |
|---|---|---|
| 前端 | `ui/src/` | Vue 3 + Vite 6 + Vanilla CSS + Web Audio API |
| Rust FFI 外壳 | `ui/src-tauri/` | Tauri 2.0 + reqwest + tokio + libloading |
| C++ 核心 | `native/` | MSVC C++20 + WinHTTP + SQLite，产物为 `EchoCAPI.dll` |

前端通过 Tauri IPC 调用 Rust 命令，Rust 通过 C ABI 动态加载 `EchoCAPI.dll` 完成网络请求、SQLite 统计与加密。播放使用 HTML5 Audio + Web Audio API 均衡器，不使用 Media Foundation。

详细架构见 [docs/wiki/architecture.md](./docs/wiki/architecture.md)。

## 2. 开发环境

| 工具 | 版本 | 说明 |
|---|---|---|
| Node.js | ≥ 22 | 前端构建 |
| pnpm | 11 | 包管理器（**不要用 npm 或 yarn**） |
| Rust | stable | Tauri 外壳 |
| CMake + MSVC | C++20 | C++ 核心，需 VS Studio Build Tools |
| vcpkg | 锁定 commit `a0400024` | C++ 依赖，见 [.github/workflows/ci.yml](./.github/workflows/ci.yml) |

Windows 10/11 x64 是唯一支持的开发与构建平台。

## 3. 克隆与启动

```powershell
git clone --recurse-submodules https://github.com/Ningbottle/BottlePlayer.git
cd BottlePlayer
cd ui
pnpm install
pnpm tauri dev
```

`server/` 是 git submodule，仅作为只读参考实现，不进入生产链路（证据见 [docs/wiki/evidence-report.md](./docs/wiki/evidence-report.md)）。若不打算对照服务端实现，可省略 `--recurse-submodules`，但此时不能修改 `server/` 相关引用。

## 4. 三层测试

CI 在 Windows 上依次运行以下检查（见 [ci.yml](./.github/workflows/ci.yml)）。本地提交前请确保全部通过。

### 4.1 C++ 核心（native/）

```powershell
cmake -S native --preset bottlemusic-check
cmake --build native/out/bottlemusic-check --config Debug
ctest --preset bottlemusic-check
```

工作目录为仓库根。`bottlemusic-check` preset 定义于 [native/CMakePresets.json](./native/CMakePresets.json)。

### 4.2 前端（ui/）

```powershell
cd ui
pnpm install --frozen-lockfile
pnpm test          # vitest run
npx vue-tsc --noEmit
pnpm build         # 生产构建（typecheck + vite build）
```

`pnpm test` 运行 [vitest](https://vitest.dev/)，配置见 [ui/vitest.config.ts](./ui/vitest.config.ts)，环境为 jsdom。

### 4.3 Rust FFI（ui/src-tauri/）

```powershell
cd ui/src-tauri
# 关键：lib 测试必须关闭默认 features，否则 tray-icon 触发 STATUS_ENTRYPOINT_NOT_FOUND
cargo test --lib --no-default-features -- --test-threads=1
cargo check --lib                    # 确保默认 features 仍可编译
cargo clippy --no-default-features -- -D warnings
```

> `--no-default-features` 仅用于 lib 测试与 clippy；Release 构建使用默认 features（tray-icon / global-shortcut）。这是已知约束，记录于 [docs/wiki/tauri-rust.md](./docs/wiki/tauri-rust.md) 与 [docs/wiki/maintenance.md](./docs/wiki/maintenance.md)。

### 4.4 测试与关键路径矩阵

不要用测试数量跨语言比较覆盖率。改动落地前请对照 [docs/wiki/testing-and-release.md](./docs/wiki/testing-and-release.md) 中的「测试与关键路径矩阵」，确认你触碰的路径有对应测试覆盖；若无，请补测试或说明理由。

## 5. 代码规范

### 5.1 提交信息（Conventional Commits）

本仓库使用 Conventional Commits 风格：

```
<type>(<scope>): <subject>
```

常见 `type`：`feat` / `fix` / `docs` / `refactor` / `test` / `chore` / `perf`。
常见 `scope`：`aurora` / `stats` / `audio` / `wiki` / `plan` / `spec` / `qa` / `tauri` / `native` / `ci`。

示例：`feat(stats): daily/weekly/monthly ranges`、`fix(audio): shared analyser graph never closes on navigation`、`docs(wiki): rebuild Code Wiki from code evidence`。

### 5.2 提交粒度

- 每项清理或修复一个独立 commit，**禁止**「cleanup everything」式大提交。
- 纯格式化或无关重命名的提交不接受。
- 删除依赖、脚本或文件前，必须先在 commit 信息或 PR 描述中给出无引用证据与恢复方式。

### 5.3 语言与风格

- 代码注释、文档用中文，关键英文符号（类名、函数名、配置键）保留原文。
- Rust 遵守 `cargo clippy -D warnings`；TypeScript 遵守 `vue-tsc --noEmit`。
- 行尾换行符遵循各目录既有约定（仓库未强制 `.editorconfig`，请勿在功能提交中混入全文件 CRLF/LF 转换）。

## 6. 架构与安全约束

以下边界为**硬约束**，改动前请在 PR 中明确说明是否触碰，并附 RFC 或测试证据：

1. **FFI 边界**：Rust 与 C++ 之间仅通过 `EchoCAPI.dll` 导出的 C ABI 符号通信（`EchoInitializeWithPathsV2` / `EchoHandleRequest` / `EchoStats*` / `EchoShutdown` / `Echo_free_string`）。不得在 Rust 端直接链接 C++ 类，也不得在 C++ 端暴露 STL 类型。参见 [docs/wiki/architecture.md](./docs/wiki/architecture.md) 与 ADR（待补：FFI 边界）。
2. **播放运行时**：播放栈为 HTML5 Audio + Web Audio API 均衡器（`captureStream → MediaStreamSource → AudioWorkletNode → GainNode → destination`）。不得重新引入 Media Foundation 或其它原生播放栈。参见 [docs/wiki/playback-runtime.md](./docs/wiki/playback-runtime.md)。
3. **Storage Actor**：SQLite 仅通过 C++ 端 Storage Actor 单线程访问，前端与 Rust 不得直连数据库文件。参见 [docs/wiki/storage-and-data.md](./docs/wiki/storage-and-data.md)。
4. **DeepSeek API Key**：仅内存会话生命周期（`ref('')` + 页面卸载清理），**不得**写入磁盘或 localStorage。参见 [docs/wiki/security-and-privacy.md](./docs/wiki/security-and-privacy.md) 与 [PRIVACY.md](./PRIVACY.md)。
5. **CSP 与权限**：Tauri CSP 与 capabilities 白名单为最小权限集，新增网络端点或 IPC 权限必须同步更新 [ui/src-tauri/tauri.conf.json](./ui/src-tauri/tauri.conf.json) 与 [ui/src-tauri/capabilities/default.json](./ui/src-tauri/capabilities/default.json)，并在 PR 中说明必要性。
6. **LICENSE 署名**：不得修改 [LICENSE](./LICENSE) 中的作者信息。

## 7. PR 流程

1. 从 `main` 拉取最新代码创建特性分支（`feat/...`、`fix/...`、`docs/...`）。
2. 本地运行第 4 节全部测试。
3. PR 描述包含：
   - 改动目的与影响的层；
   - 是否触碰第 6 节任一硬约束；
   - 测试结果（粘贴关键输出）；
   - 若删除文件/依赖，附无引用证据与恢复方式。
4. CI 全绿后方可请求 review。CI 检查项：CMake build + CTest、Vitest、vue-tsc、`cargo test --lib --no-default-features`、`cargo check --lib`、`cargo clippy --no-default-features -D warnings`。
5. 大规模架构调整（如 Pinia 迁移、路由系统重写、deadline 简化、测试框架更换）**先输出 RFC**，不直接修改生产实现。

## 8. 文档维护

- 长期文档存放于 `docs/wiki/` 与 `docs/adr/`，**可追踪**。
- 临时计划、报告、截图存放于 `docs/superpowers/`、`docs/captures/`、`docs/tmp/`，被 `.gitignore` 忽略，不入仓库。
- 改动代码后请同步更新对应 Wiki 文件；若发现 Wiki 与代码矛盾，以 [docs/wiki/evidence-report.md](./docs/wiki/evidence-report.md) 的核验流程为准重新核对，并更新证据。
- 动态测试数量写进生成报告中，不写成长期架构事实。

## 9. 不确定时

- 架构事实存疑 → 先读 [docs/wiki/evidence-report.md](./docs/wiki/evidence-report.md)，再读对应 Wiki 章节。
- 代码行为与文档矛盾 → 以代码为准，提 PR 修文档。
- 想做大重构 → 先写 RFC 放入 `docs/`，等 review 通过再实施。

感谢你的贡献。
