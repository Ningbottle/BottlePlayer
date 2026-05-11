# Claude Code 工作清单

本文是分配给 Claude Code 的轻量任务池。Claude Code 的任务应更小、更确定，优先做文档、简单测试、命名一致性和低风险修补。

## 执行规则

- 每次只做一个小任务。
- 不改大架构，不重写 UI，不引入新依赖。
- 改代码时走 `tdd`：先补一个小测试，再做最小实现。
- 任务完成后运行 `EchoNativeSmokeTests`，如果环境无法构建，要说明原因。
- 不处理 Git 凭据、远程仓库登录、代理配置。

## C1：文档链接和名称一致性检查

Type：AFK

Blocked by：None

What to build：

检查 `docs/`、`AGENTS.md`、`CLAUDE.md` 中是否还残留过期的早期原生化文档入口、旧路径或与 BottleMusic 冲突的说法。

Acceptance criteria：

- [ ] `BottleMusic`、`EchoCompatServer`、`EchoWin32` 等名称使用一致。
- [ ] 不再引用已删除的旧原生化文档目录和旧英文占位入口。
- [ ] 文档入口都指向 `docs/README.zh-CN.md`。

## C2：ContractJsonMatches 边界测试

Type：AFK

Blocked by：List 09

Status：Done

What to build：

为 `ContractJsonMatches` 补两个小边界测试：actual 允许额外字段；actual 数组短于 expected 时失败。

Acceptance criteria：

- [x] 额外字段不会导致 fixture 比较失败。
- [x] 数组缺项会返回 false。
- [x] mismatch path 指向缺项所在数组路径。

Latest verification：

- 2026-05-11：`EchoNativeSmokeTests` 通过，覆盖 native-only extra fields 和 expected array 缺项。

## C3：验证命令文档微调

Type：AFK

Blocked by：None

Status：Done

What to build：

把当前最稳定的构建、测试、Win32 启动检查命令同步到 `docs/WORKLIST.zh-CN.md` 和 `docs/TDD_PLAN.zh-CN.md`，减少后续 agent 用错 PowerShell 引号。

Acceptance criteria：

- [x] 命令使用 `cmd.exe /d /s /c` 包住 Visual Studio Developer Command。
- [x] 路径保持当前机器可运行格式。
- [x] 不新增与项目无关的环境配置说明。

Latest verification：

- 2026-05-11：`docs/TDD_PLAN.zh-CN.md` 已同步 Debug / Release 稳定验证命令。

## C4：截图文件说明

Type：AFK

Blocked by：List 08

What to build：

在 `docs/UI_REFERENCE_MELODY.zh-CN.md` 中补一小节，说明当前回归截图输出目录和用户截图反馈对应关系。

Acceptance criteria：

- [ ] 标出 900x640、1280x720、1600x1060、2560x1620 的用途。
- [ ] 说明哪些截图需要用户人工确认。
- [ ] 不改 UI 代码。

## C5：清理结果复核

Type：AFK

Blocked by：None

Status：Done

What to build：

复核本轮清理后是否还有明显重复文档、空目录或被 `.gitignore` 覆盖但误加入 Git 的生成物。

Acceptance criteria：

- [x] 输出待删除候选清单，不直接删除不确定文件。
- [x] 确认 `native/out/`、`vcpkg_installed/` 不进入 Git；旧根 `node_modules/` 已删除。
- [x] 不删除源码、依赖锁文件和用户截图资料。

Latest verification：

- 2026-05-11：删除旧 Flutter 跨平台残留：`linux/`、`macos/`、根目录 `windows/`、`lib/`、`test/`、`pubspec.yaml`、`analysis_options.yaml`、`devtools_options.yaml`、`AppImageBuilder.yml`。
- 2026-05-11：删除旧 Electron/Vue 前端和根 Node 工具链：`src/`、`dist-electron/`、旧截图、旧打包资源、根 `package.json`、根 `pnpm-lock.yaml`、根 `node_modules/` 等。
- 2026-05-11：保留 `server/` 子模块作为 KuGouMusicApi 行为参考；保留 `assets/icons/icon.png` 和 `icon.ico` 给原生 UI/安装包使用。
