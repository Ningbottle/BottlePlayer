# RFC：Release 工具链选型

- **状态**：Proposed
- **日期**：2026-07-23
- **作者**：Code Wiki 重建审计
- **关联**：[CHANGELOG.md](../CHANGELOG.md)、[.github/workflows/release.yml](../.github/workflows/release.yml)、[docs/wiki/testing-and-release.md](./wiki/testing-and-release.md)

## 1. 背景

BottleMusic 当前的 Release 流程存在以下问题（证据见 [docs/wiki/evidence-report.md](./wiki/evidence-report.md)）：

### 1.1 CHANGELOG 严重滞后

- [CHANGELOG.md](../CHANGELOG.md) 纯手工维护，最后一条停在 `2026-05-22`；
- 实际 git 提交已到 `2026-07-22`，滞后 2 个月、约 19 个 commit；
- 格式为 `### YYYY-MM-DDTHH:MM:SS` + 单行描述，非标准 [Keep a Changelog](https://keepachangelog.com/) 或 Conventional Commits 格式；
- 无 `Added` / `Changed` / `Fixed` / `Removed` 分类。

### 1.2 Release 流程无 CHANGELOG 步骤

[release.yml](../.github/workflows/release.yml) 的流程：

```
tag v* → vcpkg install → cmake release build → sync DLL
→ pnpm install → ctest → pnpm test + build → cargo test + clippy
→ tauri-action 打包发布
```

**没有**任何 CHANGELOG 生成或更新步骤。Release Notes 由 tauri-action 自动生成（默认为 commit list），但 CHANGELOG.md 不自动更新。

### 1.3 版本号同步

- `ui/src-tauri/tauri.conf.json` 的 `version` 字段由 `scripts/sync-version.mjs` 从 `package.json` 同步；
- `package.json` 的 `version` 由人工修改；
- tag 名（`v*`）与 `package.json` version 的一致性靠人工保证。

## 2. 候选方案

### 方案 A：git-cliff（推荐）

[git-cliff](https://git-cliff.org/) 是 Rust 实现的 CHANGELOG 自动生成工具，从 Conventional Commits 生成 CHANGELOG。

**优点**：
- 项目已使用 Conventional Commits（`feat(scope):` / `fix(scope):` / `docs(scope):`），git-cliff 原生支持；
- Rust 实现，与项目技术栈一致，可用 `cargo install git-cliff` 安装，也可在 CI 中用预编译二进制；
- 高度可定制模板（Tera），可输出 Keep a Changelog 格式；
- 支持 `--tag` 参数在 release 时生成对应版本段落；
- 不改变现有 release.yml 的 tauri-action 流程，只在 release 前加一步 CHANGELOG 生成；
- 可在 CI 中运行 `git-cliff --output CHANGELOG.md` 自动更新。

**缺点**：
- 需要新增 `cliff.toml` 配置文件；
- 历史提交（非 Conventional Commits 格式的早期提交）需要配置 `ignore_tags` 或跳过策略；
- 需要人工将生成的 CHANGELOG 变更提交（或用 bot 自动提交）。

**集成方式**：
1. 在 `release.yml` 的 `tauri-action` 步骤前加一步：
   ```yaml
   - name: Generate CHANGELOG
     run: |
       cargo install git-cliff
       git-cliff --tag ${{ github.ref_name }} --output CHANGELOG.md
   ```
2. 或用 GitHub Action `orhun/git-cliff-action` 避免安装；
3. 生成的 CHANGELOG 变更随 release commit 一起推送。

### 方案 B：Release Please

[Release Please](https://github.com/googleapis/release-please) 是 Google 出品的自动化 Release 工具，从 Conventional Commits 生成 Release PR + CHANGELOG。

**优点**：
- 自动创建 Release PR（包含版本号 bump + CHANGELOG 更新），merge 后自动 tag + release；
- 支持 monorepo（但 BottleMusic 不是 monorepo）；
- Node 实现，与前端技术栈一致。

**缺点**：
- 改变现有 release 流程：从「人工 tag → release.yml」变为「Release PR → 自动 tag → release.yml」；
- 需要额外的 GitHub Action 配置；
- Release PR 模式与 tauri-action 的 tag 触发模式需要协调；
- 对于单人/小团队项目，Release PR 模式可能过重。

### 方案 C：维持现状 + 手工补齐

**优点**：零改动成本。

**缺点**：CHANGELOG 继续滞后；Release Notes 质量依赖人工；无法追溯版本间变更。

## 3. 推荐

**推荐方案 A（git-cliff）**，理由：

1. **最小侵入**：不改变现有 tag → release.yml → tauri-action 流程，只在 release 前加一步 CHANGELOG 生成；
2. **技术栈一致**：Rust 实现，CI 中可用 `cargo install` 或预编译二进制；
3. **Conventional Commits 已就绪**：项目 commit 风格已符合，无需迁移历史提交；
4. **可渐进**：先在 CI 中生成，人工 review 后提交；后续可升级为 bot 自动提交。

## 4. 实施计划（若批准）

### Phase 1：配置（1 commit）
- 新增 `cliff.toml`，配置 Conventional Commits → Keep a Changelog 格式映射；
- 配置 `ignore_tags` 跳过早期非规范提交。

### Phase 2：CI 集成（1 commit）
- 在 `release.yml` 的 `tauri-action` 前加 `git-cliff` 步骤；
- 生成 `CHANGELOG.md` 并 commit 到 release tag。

### Phase 3：历史补齐（1 commit）
- 运行 `git-cliff --unreleased` 生成 `2026-05-22` 至今的变更；
- 人工 review 后合并到 `CHANGELOG.md`。

### Phase 4：本地开发（文档）
- 在 `CONTRIBUTING.md` 中补充：commit message 需符合 Conventional Commits，否则不出现在 CHANGELOG 中。

## 5. 不做的事

- **不安装 standard-version 或 changesets**：standard-version 已 deprecated；changessets 主要面向 monorepo/npm 包发布，与 Tauri 桌面应用场景不匹配；
- **不改变 tauri-action 流程**：tauri-action 的 tag 触发 + minisign 签名 + GitHub Release 发布流程保持不变；
- **不自动 bump version**：版本号仍由人工决定（通过 `package.json` + `sync-version.mjs`），git-cliff 只负责 CHANGELOG 生成。

## 6. 风险

| 风险 | 缓解 |
|---|---|
| 历史提交格式不规范导致 CHANGELOG 缺失 | `cliff.toml` 配置 `ignore_tags` 或 `--latest` 只生成首个版本 |
| CI 中 `cargo install git-cliff` 耗时 | 改用 `orhun/git-cliff-action` 预编译二进制 |
| CHANGELOG commit 与 release tag 竞态 | 在 tag 触发的 release.yml 中生成 CHANGELOG 并附到 GitHub Release body，不额外 commit |
