# BottleMusic V3 Cleanup And Verification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox state and must be checked only after the stated verification passes.

**Goal:** 在不触碰后端和保护项的前提下删除有证据的前端遗留，修复剩余死按钮与可访问性缺口，并以完整自动化测试和真实 Tauri 窗口矩阵验证方案 C。

**Architecture:** 清理以生产引用和测试证据为门槛；验证按单元/组件、类型、构建、真实窗口四层执行。视觉结果记录到独立报告，不把截图本身当作通过证据。

**Tech Stack:** ripgrep、Vitest、vue-tsc、Vite build、Tauri dev、Computer Use；若执行代理要使用 Playwright，必须先征得用户许可。

**Prerequisite:** 前三份 V3 计划全部完成并通过各自阶段回归。

**Command working directory:** pnpm、测试、类型检查、构建和 tauri dev 在 ui/ 目录运行；git、rg 及文档提交在仓库根目录运行，除非命令显式给出路径。

**Strict RED rule:** 测试收集失败、模块缺失和依赖缺失不算 RED；只接受能指出用户可见行为或状态合同未满足的失败断言。

---

## Task 1: 建立清理清单与保护测试

**Recommended agent:** Luna，高思考；只做引用盘点和测试。

**Files:**
- Create: ui/src/views/__tests__/legacySurface.contract.test.ts
- Modify: ui/src/components/__tests__/Topbar.skin.test.ts
- Modify: ui/src/components/shell/__tests__/Shells.test.ts

- [ ] **Step 1: 写保护合同**

必须保留：
- homeFeedStore 的 layoutDemo/seedLayoutDemo。
- nativeBackend.ts 及 PlayerBackend 合同。
- NewsprintShell 使用的 paper-base、paper-fibers、paper-grain、paper-vignette。
- QueuePanel。
- AuroraPlaylistShelf 的真实队列和选中态。
- native/storage 下真实缓存实现。

- [ ] **Step 2: 写保护回归与剩余清理 RED**

对前置计划已经删除的内容写 GREEN 保护回归，断言生产 UI 不再出现：
- Drawer/版面控制入口。
- fake clearCache/确认清理。
- 自定义背景、暖度、模糊、颗粒、字体控件。
- 单独英文 newspaper/newspaper mode。

本阶段有效 RED 只覆盖仍存在的目标：
- Topbar 只 alert“已复制链接”但没有复制行为的分享按钮。
- style.css 中无模板引用的 drawer-toggle、t-btn、p-icon 和旧播放器规则。

Expected: 保护回归先通过；分享按钮/遗留 CSS 的具体断言失败。若 Drawer/假缓存断言失败，说明前置计划回退，应返回前置任务修复，而不是把它当成本阶段 RED。

- [ ] **Step 3: 记录静态引用**

~~~powershell
rg -n "Drawer|toggle-tweaks|clearCache|确认清理|shareAlert|已复制链接|tweak_(warmth|blur|grain|bg|font)|drawer-toggle|\.t-btn|\.p-icon" ui/src
~~~

把每个命中分类为：生产引用、测试保护、可删除定义。不得仅凭名称删除。

- [ ] **Step 4: 运行 RED 并提交**

~~~powershell
pnpm test -- src/views/__tests__/legacySurface.contract.test.ts src/components/__tests__/Topbar.skin.test.ts src/components/shell/__tests__/Shells.test.ts
git add ui/src/views/__tests__/legacySurface.contract.test.ts ui/src/components/__tests__/Topbar.skin.test.ts ui/src/components/shell/__tests__/Shells.test.ts
git commit -m "test(ui): lock cleanup boundaries and protected surfaces"
~~~

## Task 2: 删除证实无用的前端遗留

**Recommended agent:** Luna，高思考；严格按清单机械删除。

**Files:**
- Modify: ui/src/components/Topbar.vue
- Modify: ui/src/components/__tests__/Topbar.skin.test.ts
- Modify: ui/src/style.css
- Modify: ui/src/styles/skins/aurora.css
- Modify: ui/src/styles/skins/newsprint.css
- Modify: ui/src/views/SearchView.vue
- Modify: ui/src/views/PlaylistView.vue
- Modify: ui/src/views/__tests__/legacySurface.contract.test.ts

- [ ] **Step 1: 移除无意义分享按钮**

桌面应用没有稳定可分享 URL，删除 Topbar 的 shareAlert 与对应按钮，而不是伪造 clipboard 成功。不得删除设置、队列或窗口按钮。

- [ ] **Step 2: 删除无模板实例的旧全局 CSS**

优先候选：drawer-toggle、t-btn、p-icon 和旧全局 PlayerBar 规则。每删除一组先用 rg 证明无模板/class binding 引用，再运行相关组件测试。

- [ ] **Step 3: 清理功能英文**

SearchView、PlaylistView 和剩余页面的功能标签改为中文；英文只作为中文主标签后的装饰副标题。不要翻译歌曲、艺人、品牌或音质缩写。

- [ ] **Step 4: 保护 Newsprint 纸张层**

保留 NewsprintShell 真实使用的 paper-base/paper-fibers/paper-grain/paper-vignette；只删除为已取消 custom background 服务的变量与分支。

- [ ] **Step 5: 运行 GREEN**

~~~powershell
pnpm test -- src/views/__tests__/legacySurface.contract.test.ts src/components/__tests__/Topbar.skin.test.ts src/components/shell/__tests__/Shells.test.ts src/views/__tests__/SearchView.test.ts src/views/__tests__/PlaylistView.test.ts
~~~

- [ ] **Step 6: 提交**

~~~powershell
git add ui/src/components/Topbar.vue ui/src/components/__tests__/Topbar.skin.test.ts ui/src/style.css ui/src/styles/skins/aurora.css ui/src/styles/skins/newsprint.css ui/src/views/SearchView.vue ui/src/views/PlaylistView.vue ui/src/views/__tests__/legacySurface.contract.test.ts
git commit -m "refactor(ui): remove proven dead controls and styles"
~~~

## Task 3: 无障碍与交互完整性审查

**Recommended agent:** Terra，高思考。

**Files:**
- Modify: ui/src/components/player/__tests__/AuroraPlayerBar.test.ts
- Modify: ui/src/components/player/__tests__/NewsprintPlayerBar.test.ts
- Modify: ui/src/views/lyric/__tests__/LyricStages.test.ts
- Modify: ui/src/views/__tests__/SettingsView.test.ts
- Modify: ui/src/components/shell/__tests__/FullscreenWindowControls.test.ts
- Modify: ui/src/components/player/AuroraPlayerBar.vue
- Modify: ui/src/components/player/NewsprintPlayerBar.vue
- Modify: ui/src/views/lyric/AuroraLyricStage.vue
- Modify: ui/src/views/lyric/NewsprintLyricStage.vue
- Modify: ui/src/views/SettingsView.vue
- Modify: ui/src/components/shell/FullscreenWindowControls.vue

- [ ] **Step 1: 写键盘与可访问名称测试**

所有 icon-only button 必须有唯一中文 accessible name/title；disabled 与 focus-visible 状态明确；进度和音量 input 有 label；Settings 控件有 label/role/checked；错误恢复按钮可键盘使用。

- [ ] **Step 2: 写交互互斥测试**

单击封面、双击非全屏封面、fullscreen icon、fullscreen 大封面和 Escape 各只触发一个动作；不得因 click + dblclick 同时触发两次导航或粘住样式。

- [ ] **Step 3: 写 reduced-motion 测试**

Aurora 与 Newsprint 在 reduced-motion 下直接进入最终可见状态，无无限 GSAP tween；控制显隐和导航仍工作。

- [ ] **Step 4: RED → 最小 GREEN**

先运行目标测试确认失败，再只修改直接相关实现。禁止在本任务重排布局。

~~~powershell
pnpm test -- src/components/player/__tests__ src/views/lyric/__tests__/LyricStages.test.ts src/views/__tests__/SettingsView.test.ts src/components/shell/__tests__/FullscreenWindowControls.test.ts
~~~

- [ ] **Step 5: 提交**

~~~powershell
git add ui/src/components/player/AuroraPlayerBar.vue ui/src/components/player/NewsprintPlayerBar.vue ui/src/components/player/__tests__/AuroraPlayerBar.test.ts ui/src/components/player/__tests__/NewsprintPlayerBar.test.ts ui/src/views/lyric/AuroraLyricStage.vue ui/src/views/lyric/NewsprintLyricStage.vue ui/src/views/lyric/__tests__/LyricStages.test.ts ui/src/views/__tests__/SettingsView.test.ts ui/src/views/SettingsView.vue ui/src/components/shell/FullscreenWindowControls.vue ui/src/components/shell/__tests__/FullscreenWindowControls.test.ts
git commit -m "fix(ui): complete keyboard and accessible player interactions"
~~~

若 RED 测试要求修改其他对应实现，只显式追加实际修改的文件路径；禁止用目录级 git add。

## Task 4: 全量自动化验证

**Recommended agent:** Luna，高思考；只执行命令、保存完整日志并分类失败。

**Files:**
- Create: docs/superpowers/reports/2026-07-13-dual-interface-v3-verification.md

- [ ] **Step 1: 确认工作树范围**

~~~powershell
git status --short
git diff --stat main...HEAD
git diff --name-only main...HEAD
~~~

Expected: 仅方案 C 的 UI、测试、依赖和文档；native、src-tauri、layout QA 种子无意外修改。

- [ ] **Step 2: 运行全量 Vitest**

~~~powershell
pnpm test
~~~

报告总文件数、总测试数、通过/失败数；Canvas jsdom 警告单列为已知测试环境限制，不当作静默通过。

- [ ] **Step 3: 类型检查与生产构建**

~~~powershell
pnpm exec vue-tsc --noEmit
pnpm build
~~~

Expected: exit code 0，无 TypeScript error，无 build error。

- [ ] **Step 4: 运行静态保护扫描**

~~~powershell
rg -n "layoutDemo|seedLayoutDemo" ui/src/api/homeFeedStore.ts
rg -n "nativeBackend" ui/src ui/package.json
rg -n "<svg|进入全屏|>词<|>播放<|>暂停<|shareAlert|clearCache|toggle-tweaks" ui/src/components/player ui/src/views/lyric ui/src/components/Topbar.vue ui/src/views/SettingsView.vue
~~~

- [ ] **Step 5: 写验证报告初稿**

报告按命令记录日期、commit、exit code、测试数量和未解决警告。不要在视觉 QA 前标“完成”。

## Task 5: 真实 Tauri 窗口视觉与交互 QA

**Recommended agent:** Terra，高思考并具备识图能力。必须使用 product-design:index、product-design:image-to-code、build-web-apps:frontend-testing-debugging 和 superpowers:verification-before-completion。

**Reference:** 用户提供的 Aurora 目标图 C:/Users/w1521/AppData/Local/Temp/codex-clipboard-b5a6407f-638b-4e98-bf62-b20e7a8b3f3e.png。若执行时文件已失效，请用户重新附图；不得凭记忆伪造视觉对比。

**Files:**
- Modify: docs/superpowers/reports/2026-07-13-dual-interface-v3-verification.md
- Create: ui/design-qa-captures/v3/aurora-light-home-1586x1024.png
- Create: ui/design-qa-captures/v3/aurora-dark-home-1280x720.png
- Create: ui/design-qa-captures/v3/aurora-light-lyric-1100x700.png
- Create: ui/design-qa-captures/v3/aurora-dark-lyric-fullscreen-1586x1024.png
- Create: ui/design-qa-captures/v3/newsprint-light-home-1586x1024.png
- Create: ui/design-qa-captures/v3/newsprint-dark-home-1280x720.png
- Create: ui/design-qa-captures/v3/newsprint-light-lyric-1100x700.png
- Create: ui/design-qa-captures/v3/newsprint-dark-lyric-fullscreen-1586x1024.png

视觉 QA 若发现问题，只修改观察结果直接证明有问题的 Vue/CSS/test 文件，并先补 RED 测试；不要预先扩大写入范围。

- [ ] **Step 1: 启动前检查端口**

~~~powershell
Get-NetTCPConnection -LocalPort 1420 -ErrorAction SilentlyContinue
~~~

若端口已有正确 Vite 实例，复用；若是陈旧 BottleMusic dev 进程，先确认进程归属再关闭。不得盲目结束其他应用。

- [ ] **Step 2: 启动真实应用**

~~~powershell
pnpm tauri dev
~~~

保持会话运行，记录 URL/进程；不要同时启动第二个 1420 实例。

- [ ] **Step 3: 执行视觉矩阵**

每套皮肤检查 light/dark，至少覆盖 1586×1024、1280×720、1100×700：
- 首页首次 skeleton、加载完成、单区错误。
- 非全屏歌词双栏。
- 全屏歌词默认控制隐藏、唤醒和自动淡出。
- 底部播放器空曲目、播放、暂停。
- Settings 外观。
- 页面错误恢复态。

- [ ] **Step 4: 执行交互场景**

1. 底栏封面单击进入非全屏歌词。
2. 非全屏封面双击进入全屏。
3. 封面下 icon 进入全屏。
4. Aurora fullscreen 大封面打开 3D 歌单。
5. 选下一首后仍全屏，歌单/歌词/真实音频一致。
6. 离开歌词路由一次退出全屏。
7. 返回 Newsprint 首页立即显示缓存，无空白。
8. 慢请求期间导航、播放、窗口控制可用。
9. Esc 一次退出全屏，不出现选择/粘贴样式。

- [ ] **Step 5: 对照检查**

把参考图与当前 Aurora 截图以相同 viewport 并排检查：内容焦点、舞台比例、留白、控制重量、封面清晰度、队列密度、绿色强调和底部 dock。截图不是通过证据；必须在报告中列出观察、修正和复测结果。

- [ ] **Step 6: 修复必须重新走 RED**

每个发现先补组件/状态测试，再最小修复，重跑目标测试和受影响视觉场景。不得在 QA 阶段做未验证的大重构。

- [ ] **Step 7: 完成验证报告并提交**

~~~powershell
git add -f docs/superpowers/reports/2026-07-13-dual-interface-v3-verification.md ui/design-qa-captures/v3/aurora-light-home-1586x1024.png ui/design-qa-captures/v3/aurora-dark-home-1280x720.png ui/design-qa-captures/v3/aurora-light-lyric-1100x700.png ui/design-qa-captures/v3/aurora-dark-lyric-fullscreen-1586x1024.png ui/design-qa-captures/v3/newsprint-light-home-1586x1024.png ui/design-qa-captures/v3/newsprint-dark-home-1280x720.png ui/design-qa-captures/v3/newsprint-light-lyric-1100x700.png ui/design-qa-captures/v3/newsprint-dark-lyric-fullscreen-1586x1024.png
git commit -m "docs(ui): record dual interface v3 verification"
~~~

## Task 6: 最终代码审查与分支交付

**Recommended agent:** Terra，高思考，使用 superpowers:requesting-code-review 和 superpowers:finishing-a-development-branch。

- [ ] **Step 1: 以 main merge-base 做代码审查**

按 P0/P1/P2/P3 输出 findings；重点检查竞态、路由生命周期、错误边界、死按钮、a11y、测试误改、保护项。

- [ ] **Step 2: 修复 findings**

每个实质 finding 回到对应任务，用 RED 测试证明后修复。无 finding 时明确记录残余风险。

- [ ] **Step 3: 最终验证**

~~~powershell
pnpm test
pnpm exec vue-tsc --noEmit
pnpm build
git status --short
~~~

- [ ] **Step 4: 更新报告最终状态**

只有上述命令和视觉矩阵均通过时才写“完成”。报告列出 commit 范围、测试计数、视觉矩阵和已知限制。

- [ ] **Step 5: 等待用户决定合并/推送**

不要自动合并到 main，不要自动 push。向用户提供本地分支、最后 commit、验证摘要和可选交付方式。
