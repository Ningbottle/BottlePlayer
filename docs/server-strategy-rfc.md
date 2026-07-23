# RFC：server/ Submodule 策略

- **状态**：Proposed
- **日期**：2026-07-23
- **作者**：Code Wiki 重建审计
- **关联**：[docs/wiki/evidence-report.md](./wiki/evidence-report.md)、[docs/wiki/architecture.md](./wiki/architecture.md)、[.gitmodules](../.gitmodules)

## 1. 背景

BottleMusic 仓库包含一个 `server/` git submodule，指向 [MakcRe/KuGouMusicApi](https://github.com/MakcRe/KuGouMusicApi)（Node.js 实现的酷狗 API 参考实现）。

### 1.1 事实核验（证据见 evidence-report.md）

- **`server/` 不进入生产链路**：
  - `ui/src-tauri/src/lib.rs` 注册的 19 个 Tauri 命令均通过 `EchoCAPI.dll` 的 C ABI 调用 C++ 核心，**无任何**命令引用 `server/`；
  - `native/CMakeLists.txt` 的所有 target（`EchoCore` / `EchoCAPI` / `EchoStorage` 等）**无任何**源文件或 include 路径引用 `server/`；
  - CI（[ci.yml](../.github/workflows/ci.yml)）和 Release（[release.yml](../.github/workflows/release.yml)）的 `actions/checkout` 虽带 `submodules: recursive`，但后续步骤**不构建、不运行、不测试** `server/`。
- **`server/` 的用途**：纯只读参考实现，开发者在实现 C++ `CompatApi` 路由时对照 Node.js 版本的请求参数与响应格式。
- **`server/` 的成本**：
  - `git clone --recurse-submodules` 会额外克隆一个外部仓库；
  - CI 每次 checkout 都拉取 submodule（虽不构建，但增加 clone 时间）；
  - 新贡献者可能误以为 `server/` 是项目的一部分。

### 1.2 约束

用户明确要求：
> 「不改变 `server/` 的存储方式；只提交 `server-strategy-rfc.md`。」

本 RFC **不修改** `server/` 的存储方式，只输出策略建议。

## 2. 候选方案

### 方案 A：维持 submodule 现状（推荐）

保留 `server/` 作为 git submodule，不改变任何配置。

**优点**：
- 零改动，零风险；
- 开发者可继续在本地对照参考实现；
- submodule 的 `--recurse-submodules` 是 opt-in，不需要的开发者可跳过。

**缺点**：
- CI clone 时间略增（但 < 5s，可接受）；
- 新贡献者需要理解 submodule 用途。

**配套文档**：
- 在 [CONTRIBUTING.md](../CONTRIBUTING.md) 中已说明「`server/` 是 git submodule，仅作为只读参考实现，不进入生产链路」；
- 在 [docs/wiki/evidence-report.md](./wiki/evidence-report.md) 中已记录核验证据。

### 方案 B：archive 为独立分支

将 `server/` submodule 的引用快照打包为仓库的一个 `archive/server-reference` 分支，移除 `.gitmodules` 中的 submodule 声明。

**优点**：
- `git clone` 不再拉取外部仓库；
- 参考实现仍可通过 `git checkout archive/server-reference` 获取。

**缺点**：
- 参考实现无法跟随上游更新；
- 增加仓库分支管理复杂度；
- **违反用户约束**（改变存储方式）。

### 方案 C：完全移除

移除 `server/` submodule 和 `.gitmodules` 中的声明。

**优点**：
- 仓库最精简；
- CI clone 最快。

**缺点**：
- 丢失参考实现，开发者实现新路由时无法对照；
- **违反用户约束**（改变存储方式）。

### 方案 D：转为独立仓库 + 文档链接

将 `server/` 的引用替换为文档中的链接（`https://github.com/MakcRe/KuGouMusicApi`），开发者需要时自行 clone。

**优点**：
- 仓库最精简；
- 参考实现仍可获取。

**缺点**：
- 无法 pin 到特定 commit（上游可能 breaking change）；
- **违反用户约束**（改变存储方式）。

## 3. 推荐

**推荐方案 A（维持 submodule 现状）**，理由：

1. **用户约束**：明确要求不改变存储方式；
2. **成本可接受**：submodule 的 clone 开销 < 5s，不影响 CI；
3. **价值明确**：开发者在实现新路由时可对照 pin 的 commit，避免上游 breaking change；
4. **文档已就绪**：CONTRIBUTING.md 和 evidence-report.md 已说明 server/ 的用途与边界。

## 4. 未来提案（不实施）

若未来决定精简仓库，推荐 **方案 D（文档链接）**，并：
1. 在 `docs/wiki/native-cpp.md` 中记录最后一次 pin 的 commit SHA；
2. 在 CONTRIBUTING.md 中说明「参考实现见外部仓库 [MakcRe/KuGouMusicApi](https://github.com/MakcRe/KuGouMusicApi)，注意自行核对 API 兼容性」；
3. 移除 `.gitmodules` 中的 `server/` 声明。

## 5. 不做的事

- **不修改** `.gitmodules`；
- **不删除** `server/` 目录；
- **不改变** CI/Release 的 `submodules: recursive` 配置；
- **不构建/测试** `server/`（维持只读参考定位）。
