# BottleMusic 安全加固与分支合并设计规格

> 状态：已批准
> Revision：r1（2026-07-17）
> 实施分支：`codex/dual-interface-deep-refactor`（加固与修复在此分支完成后合并入 `main`）
> 实施方式：TDD、子代理审阅、主代理逐任务提交
> 发布目标：合并到 `main` 并推送（不打 tag、不构建安装包、不做签名/NSIS/虚拟机验收）

## 1. 文档地位

本规格承接 `2026-07-13-dual-interface-deep-refactor-design.md`。前者定义双界面深度重构（已由 25 个提交实现并全量验证通过）；本规格定义在深度重构基础上叠加的**安全加固收尾、RustSec 扫描、测试噪声治理、分支合并与清理**。两者冲突时，安全加固以本规格为准。

## 2. 背景

上一轮在 `codex/dual-interface-deep-refactor` 工作树完成了深度重构的 25 个提交（已提交、未推送），并叠加了一批**未提交的安全加固改动**（687 行，23 文件）：

- 生产 CSP、收紧 Tauri capability、移除 shell 插件
- 音频代理禁止重定向到非酷狗域名/私网/本机，脱敏签名 URL
- 账号会话改用 Windows DPAPI 加密，旧明文首次读取迁移
- 扫码登录与用户详情响应不再向 WebView 暴露 `token/t1`
- DeepSeek API Key 不再写入 localStorage，提示数据外发
- release.yml 全部固定到 40 位 SHA，发布前跑前端/C++/Rust/Clippy
- 新增 PRIVACY.md、SECURITY.md

三个并行安全审阅代理已逐文件复核。结论：加固是真材实料的进步，但存在必须修的 HIGH 问题和若干 MEDIUM 问题。

## 3. 已确认问题

### 3.1 必须修（HIGH）

1. **token 脱敏只在 3/72 个响应路径生效**（`native/core/CompatApi.cpp` + `compat_routes/*.cpp`）。`StripSessionCredentials` 只在 `HandleLoginQrCheck`、`HandleUserDetail` 等少数路由调用，`/user/vip/detail`、`/user/playlist`、`/user/history`、`/user/cloud` 等上游若回显 token，仍会直泄 WebView。这正是本次加固要防的故障模式。
2. **release.yml 校验步骤不传递失败**（`.github/workflows/release.yml`）。PowerShell 多行步骤中，`pnpm test` 失败但 `pnpm build` 成功时步骤仍判绿，可能导致带 failing tests 签名发版。`cargo test`/`cargo clippy` 同理。

### 3.2 应当修（MEDIUM）

3. **明文迁移读取路径永久开放**（`SessionRepository.cpp`）。任何时刻写入的明文行都会被当作合法 session 信任，无日志，等于加密可被无声绕过。
4. **脱敏字段名只覆盖精确 `token`/`t1`**（`CompatApiUtils.h`），大小写敏感，不覆盖 `access_token`/`auth_token`/`secret`/`cookie`/`signature`。
5. **明文 buffer 加密/解密后未清零**（`SessionRepository.cpp`），无 `SecureZeroMemory`，明文可能残留堆内存。
6. **CSP `connect-src` 允许 `http://127.0.0.1:*`**（`tauri.conf.json`）。音频代理动态端口所致；XSS 可扫描本机任意端口。

### 3.3 降级为后续（LOW / 防御纵深）

记入第 9 节"已知后续项"，本轮不修：DNS rebinding 无解析 IP 校验；`redact_url_queries` 不覆盖 fragment/path 内 secret；`attempt.stop()` 回流 3xx body；`img-src`/`media-src` 过宽；缺 `frame-ancestors`、`style-src 'unsafe-inline'`；`process:allow-restart` DoS；迁移 TOCTOU；`Load()` 迁移时抛异常；DWORD 截断；`ai_analysis.rs` 错误体直显；`releaseSecurity.test.ts` 未锁 C++ 步骤与 `continue-on-error`；StatsView 惰性清理旧 key；README 与 UI 的 VIP 描述不一致。

## 4. 非目标

- 不打 tag、不构建 NSIS 安装包、不做签名证书/Updater 私钥/干净虚拟机验收。
- 不删除远程分支（`origin/product-stability` 等保留）。
- 不解决第三方服务/音乐内容授权问题（GitHub 个人使用范围内，PRIVACY/SECURITY 已声明）。
- 不重做深度重构已实现的能力，仅做安全收尾与合并。
- 不在本轮修第 3.3 节的 LOW 项。

## 5. 方案：先加固后合并（Approach A）

所有安全修复、cargo-audit、Vitest 噪声治理都在 `codex/dual-interface-deep-refactor` 分支完成并全量验证绿，**然后一次性 `--no-ff` 合并入 `main` 并推送**，最后清理分支与 worktree。

理由：让 `main` 落地单一干净、已验证的合并点，深度重构的 TDD 提交历史完整保留。对比"先合并再在 main 加固"（main 合并后还要吃提交、跑两轮验证）与"拆分"（割裂相关安全工作），A 更干净。

合并策略：`git merge --no-ff codex/dual-interface-deep-refactor`，保留 25+ 提交历史，不 squash。

## 6. 分阶段实施

### 阶段 0 · 主工作树未提交改动处置（合并前必做）

主工作树（`C:/BottleMusic`，`main`）有 394 行未提交改动（8 文件），即深度重构 spec §11.0 所述"8 个候选改动"。深度重构实施时已按"按行为和 RED 测试选择性移植"处理。

处置流程：
1. 逐文件 diff 主工作树未提交改动 vs deep-refactor 同名文件。
2. 确认 deep-refactor 已覆盖该行为（已确认：lyric click-to-seek 已在 `AuroraLyricStage.vue:59-61,405`）。
3. 已覆盖：`git stash` 备份（保留 patch 以防万一）后丢弃。
4. 未覆盖：移植到 deep-refactor 分支（RED->GREEN）。
5. 处置结论写入实施计划报告。

### 阶段 1 · 安全修复并提交（在 codex 分支）

按主题拆提交，每个 RED->GREEN：

1. **H1 咽喉点脱敏**：把 `StripSessionCredentials` 上移到 `CompatApi::Handle`（所有响应必经点），移除 3 个分散调用。补测试：`/user/vip/detail`、`/user/playlist` 等上游回显 token 时 WebView 不泄漏。
2. **H2 release.yml 失败传递**：每个校验步骤加 `shell: bash`（`set -e`）或 `&&` 串联。补 `releaseSecurity.test.ts`：断言 C++ 步骤（`ctest`）存在、`continue-on-error: true` 不存在。
3. **M1 迁移闭合 + M3 内存清零**：一次性迁移标志/schema 版本，迁移后关闭明文路径并记日志；`SecureZeroMemory` 清零明文 buffer 与 DPAPI 输出后再 `LocalFree`。补测试：corrupt-ciphertext 返回 nullopt 且 DB 不变；迁移后明文路径不再被信任。
4. **M2 凭证 allowlist**：扩成大小写无关的凭证字段集合（`token`、`t1`、`access_token`、`auth_token`、`session_token`、`secret`、`cookie`、`signature`、`set-cookie`）。补测试：嵌套对象与变体字段名被脱敏。

### 阶段 1.5 · 移植 shiny-cabin 键盘导航（在 codex 分支，合并前）

`opencode/shiny-cabin` worktree 未提交的 `AuroraLyricStage.vue` 改动中，click-to-seek 已在 deep-refactor；ArrowUp/ArrowDown 键盘导航（无障碍 bonus）未在 deep-refactor。按 RED->GREEN 移植到 deep-refactor 的 `AuroraLyricStage.vue`（文件已大幅重构，按当前结构适配，含 `LyricStages.test.ts` 键盘导航测试）。此阶段必须在阶段 4 合并前完成，使键盘导航随合并一起落地。

### 阶段 2 · cargo-audit 扫描

1. `cargo install cargo-audit`（若环境无网络，如实记录为遗留项，不假装通过）。
2. `cargo audit`（在 `ui/src-tauri`）。
3. 有漏洞：升 `Cargo.lock`/依赖并复测；无：记录"RustSec 扫描通过"。

### 阶段 3 · Vitest jsdom 噪声治理

1. 在测试 setup 加统一的 `HTMLMediaElement`/`CanvasRenderingContext2D` mock，消除背景未实现噪声。
2. 复跑确认真实失败仍被识别（人为造一个失败用例验证 mock 不掩盖异常，再删除）。
3. 补回归测试锁定 mock 行为。

### 阶段 4 · 合并并推送

全量验证全绿后执行：
- Vitest、`vue-tsc --noEmit`、`vite build`
- `cargo test`、`cargo clippy -D warnings`、`cargo check`（在 `ui/src-tauri`）
- CTest（在 `native`）
- `git merge --no-ff codex/dual-interface-deep-refactor` -> `main`
- `git push origin main`

### 阶段 5 · 分支与 worktree 清理

- `codex/dual-interface-player-redesign` worktree：未提交改动为琐碎中文化 + 两个 BOM 污染，已被 deep-refactor 覆盖 -> 丢弃，`git worktree remove`，`git branch -D`。
- `opencode/shiny-cabin` worktree：键盘导航已在阶段 1.5 移植；click-to-seek 已在 deep-refactor -> `git worktree remove`，`git branch -D`。
- `opencode/happy-harbor` worktree：干净、已合并 -> 直接 `git worktree remove`，`git branch -d`。
- `playback-orchestrator-tdd` worktree：干净、已合并 -> 直接 `git worktree remove`，`git branch -d`。
- 远程分支不动。

### 阶段 6 · 完成验证

在 `main` 上跑 verification-before-completion 完整清单，确认合并结果干净、处于可推送状态。记录最终验证结果。

## 7. 保护项

- `ui/src/api/nativeBackend.ts`、`?layoutDemo=1` 数据种子、3D 歌单、Rust/Tauri 音频代理与诊断链路（深度重构 spec §9）。
- 用户主工作树未提交改动：阶段 0 先 stash 备份再处置，不直接丢弃。
- deep-refactor 已通过的 781/781 Vitest、29+2 Rust、11 CTest 基线不得回退。

## 8. 测试策略

严格 RED -> GREEN -> REFACTOR。每个安全修复先写失败测试（如"上游回显 access_token 时 WebView 响应不含该字段"），再实现。旧测试若锁定错误要求，先写新产品合同测试再改写冲突断言，提交说明指出原因。阶段 4 前全量验证必须全绿。

## 9. 已知后续项（本轮不修）

第 3.3 节所列 LOW 项，以及：
- 真实签名证书、Updater 私钥、NSIS 安装包、干净虚拟机安装/升级验收（商店正式版前置）。
- 第三方服务与音乐内容授权（上架前置）。
- `connect-src` 收窄需音频代理固定端口（M4，本轮仅文档化折中）。

这些在合并后单独排期。

## 10. 成功标准

1. H1/H2/M1/M2/M3 修复完成且有测试锁定。
2. cargo-audit 扫描已执行（通过或如实记录遗留）。
3. Vitest jsdom 噪声显著降低且不掩盖真实失败。
4. `codex/dual-interface-deep-refactor` 已 `--no-ff` 合并入 `main` 并推送。
5. 被取代/已合并的 4 个本地分支及其 worktree 已清理；远程分支不动。
6. `main` 上全量验证全绿。
