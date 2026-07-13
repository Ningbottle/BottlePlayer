# BottleMusic V3 Dual Skin Experience Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox state and must be checked only after the stated verification passes.

**Goal:** 在共享行为合同之上完成两套独立的播放器、歌词页、加载状态和动效语言，并消除文字命令、遮挡、拥挤、进度不可见与错误点击语义。

**Architecture:** usePlayerControls 提供无视觉的动作模型；AuroraPlayerBar 与 NewsprintPlayerBar 分别使用成熟图标库渲染；歌词舞台共享数据模型但保持独立模板/CSS；useAutoHideControls 只共享全屏显隐状态机，不共享视觉。现有 AuroraPlaylistShelf 保持原实现，只补回归合同。

**Tech Stack:** Vue 3.5、GSAP 3、@phosphor-icons/vue、lucide-vue-next、Vitest、Vue Test Utils。

**Required design skills:** product-design:index、product-design:image-to-code、build-web-apps:frontend-testing-debugging、vue-application-structure、superpowers:test-driven-development。Aurora 视觉以用户截图为来源；不得凭文字自行发明另一套首页。

**Prerequisite:** 完成稳定性与导航外观两份计划。

**Command working directory:** pnpm、测试、类型检查和构建命令在 ui/ 目录运行；git add/commit 在仓库根目录运行。

**Strict RED rule:** 缺依赖、模块不存在或类型错误不算 RED；新 composable/component 先提供最小可编译骨架，新增测试必须因行为不满足而失败。

---

## Task 1: 控制语义与图标 RED

**Recommended agent:** Luna，高思考；先建立无障碍和点击合同。

**Files:**
- Modify: ui/src/components/player/__tests__/usePlayerControls.test.ts
- Modify: ui/src/components/player/__tests__/AuroraPlayerBar.test.ts
- Modify: ui/src/components/player/__tests__/NewsprintPlayerBar.test.ts
- Modify: ui/src/components/player/__tests__/PlayerProgress.test.ts
- Modify: ui/package.json
- Modify: ui/pnpm-lock.yaml

- [ ] **Step 1: 写播放器点击合同**

覆盖：
- 单击封面或歌曲信息只进入非全屏 lyric route。
- 单击 fullscreen icon 进入 lyric route 并设 fullscreen=true。
- 无曲目时封面和 fullscreen icon disabled。
- 核心传输顺序为 previous、play/pause、next。
- 所有命令按钮有中文 aria-label/title。
- 按钮可见文本中不存在“进入全屏”“上一首”“下一首”“播放”“暂停”“词”等命令字。
- 进度轨道在 Aurora/Newsprint × light/dark 下使用非透明 fill/track token。

- [ ] **Step 2: 写现有错误断言的替代测试**

先新增正确合同并运行失败，再删除锁定“进入全屏”文字或 Aurora 无歌曲信息的旧断言。提交说明要列出被替换的错误合同。

- [ ] **Step 3: 运行 RED**

~~~powershell
pnpm test -- src/components/player/__tests__/usePlayerControls.test.ts src/components/player/__tests__/AuroraPlayerBar.test.ts src/components/player/__tests__/NewsprintPlayerBar.test.ts src/components/player/__tests__/PlayerProgress.test.ts
~~~

- [ ] **Step 4: 安装图标依赖**

~~~powershell
pnpm add @phosphor-icons/vue lucide-vue-next
~~~

- [ ] **Step 5: 提交 RED 与依赖**

~~~powershell
git add ui/package.json ui/pnpm-lock.yaml ui/src/components/player/__tests__/usePlayerControls.test.ts ui/src/components/player/__tests__/AuroraPlayerBar.test.ts ui/src/components/player/__tests__/NewsprintPlayerBar.test.ts ui/src/components/player/__tests__/PlayerProgress.test.ts
git commit -m "test(ui): define icon player interaction contracts"
~~~

## Task 2: 两套独立播放器实现

**Recommended agent:** Terra，高思考；需要保持功能一致又视觉独立。

**Files:**
- Modify: ui/src/components/player/usePlayerControls.ts
- Modify: ui/src/components/player/AuroraPlayerBar.vue
- Modify: ui/src/components/player/NewsprintPlayerBar.vue
- Modify: ui/src/components/player/PlayerProgress.vue
- Modify: ui/src/styles/progress.css
- Modify: ui/src/components/player/__tests__/usePlayerControls.test.ts
- Modify: ui/src/components/player/__tests__/AuroraPlayerBar.test.ts
- Modify: ui/src/components/player/__tests__/NewsprintPlayerBar.test.ts

- [ ] **Step 1: 让 usePlayerControls 成为动作模型**

模型公开 hasTrack、currentTrack、isPlaying、loopMode、volume 和命令方法；不返回图标、CSS class 或皮肤文案。

- [ ] **Step 2: Aurora 使用 Phosphor 图标**

删除手写 inline SVG 和中文命令文字。播放按钮降低视觉重量，使用局部 glow/fill 反馈，不使用厚边框。果冻反馈只作用 transform/opacity，按下后立即可再次操作。

- [ ] **Step 3: Newsprint 使用 Lucide 图标**

保留报刊式紧凑控制和线性图标；不做胶囊式重按钮。允许与 Aurora 不同的图标细节和布局，但 previous/play/next 顺序不变。

- [ ] **Step 4: 修复进度对比度**

PlayerProgress 使用 --progress-track、--progress-fill、--progress-thumb；四个 skin/mode 组合都定义明确 token。focus-visible 时显示对比清楚的 outline。

- [ ] **Step 5: 运行 GREEN**

~~~powershell
pnpm test -- src/components/player/__tests__/usePlayerControls.test.ts src/components/player/__tests__/AuroraPlayerBar.test.ts src/components/player/__tests__/NewsprintPlayerBar.test.ts src/components/player/__tests__/PlayerProgress.test.ts
~~~

- [ ] **Step 6: 提交**

~~~powershell
git add ui/src/components/player/usePlayerControls.ts ui/src/components/player/AuroraPlayerBar.vue ui/src/components/player/NewsprintPlayerBar.vue ui/src/components/player/PlayerProgress.vue ui/src/styles/progress.css ui/src/components/player/__tests__/usePlayerControls.test.ts ui/src/components/player/__tests__/AuroraPlayerBar.test.ts ui/src/components/player/__tests__/NewsprintPlayerBar.test.ts ui/src/components/player/__tests__/PlayerProgress.test.ts
git commit -m "feat(ui): render independent icon player controls"
~~~

## Task 3: 非全屏歌词双栏与点击语义

**Recommended agent:** Terra，高思考。

**Files:**
- Modify: ui/src/views/lyric/AuroraLyricStage.vue
- Modify: ui/src/views/lyric/NewsprintLyricStage.vue
- Modify: ui/src/views/lyric/LyricFollowFooter.vue
- Modify: ui/src/views/lyric/__tests__/LyricStages.test.ts
- Modify: ui/src/views/__tests__/LyricView.test.ts
- Modify: ui/src/styles/skins/aurora.css
- Modify: ui/src/styles/skins/newsprint.css

- [ ] **Step 1: 写双栏 RED**

断言非全屏两套皮肤都有：
- 左 meta column 和右 lyric column。
- 封面、歌曲名、歌手、专辑/音质摘要。
- 封面下方只有 icon fullscreen button。
- 双击封面进入全屏。
- LyricFollowFooter 位于歌词 column 的独立底部区域。
- 歌词请求失败显示独立错误与重试按钮，不把错误文字伪装成歌词行。
- app 支持的最窄 viewport 下仍是两栏，无歌词遮挡。

- [ ] **Step 2: 实现结构，不复制业务逻辑**

两组件消费同一 LyricStageModel/commands，但保留独立 DOM class、间距和视觉。Aurora 非全屏封面单击不打开 3D 歌单；只有 fullscreen 时单击大封面才 openShelf()。

- [ ] **Step 3: 修正封面比例与滚动**

封面统一 aspect-ratio: 1 / 1 和 object-fit: cover；歌词区 min-width: 0、flex: 1 1 0；隐藏视觉滚动条但保留滚动能力。

- [ ] **Step 4: 修正 follow footer**

footer 使用 sticky/flex footer slot，不使用覆盖歌词的 absolute 中心定位；按钮图标化，中文 aria-label/title。

- [ ] **Step 5: 运行 GREEN**

~~~powershell
pnpm test -- src/views/lyric/__tests__/LyricStages.test.ts src/views/__tests__/LyricView.test.ts
~~~

- [ ] **Step 6: 提交**

~~~powershell
git add ui/src/views/lyric/AuroraLyricStage.vue ui/src/views/lyric/NewsprintLyricStage.vue ui/src/views/lyric/LyricFollowFooter.vue ui/src/views/lyric/__tests__/LyricStages.test.ts ui/src/views/__tests__/LyricView.test.ts ui/src/styles/skins/aurora.css ui/src/styles/skins/newsprint.css
git commit -m "feat(ui): align both lyric stages around a two-column contract"
~~~

## Task 4: 全屏控制自动隐藏状态机

**Recommended agent:** Terra，高思考。

**Files:**
- Create: ui/src/views/lyric/useAutoHideControls.ts
- Create: ui/src/views/lyric/__tests__/useAutoHideControls.test.ts
- Modify: ui/src/views/lyric/AuroraLyricStage.vue
- Modify: ui/src/views/lyric/NewsprintLyricStage.vue
- Modify: ui/src/views/lyric/__tests__/LyricStages.test.ts
- Modify: ui/src/components/shell/FullscreenWindowControls.vue
- Modify: ui/src/components/shell/__tests__/FullscreenWindowControls.test.ts

- [ ] **Step 1: 添加最小可编译 composable 骨架并写显隐 RED**

骨架只返回 visible=true 和空 handler，使测试能够收集但行为失败。使用 fake timers 覆盖：
- fullscreen 初始短暂可见后隐藏。
- pointermove 显示并重启 idle timer。
- 指针接近底边显示。
- focusin 保持可见，focusout 后恢复 idle。
- Escape 退出 fullscreen。
- dispose 清 timer/listener。
- reduced-motion 不影响可操作性，只影响动画方式。

- [ ] **Step 2: 实现无视觉状态机**

~~~ts
export interface AutoHideControls {
  visible: Readonly<Ref<boolean>>;
  onPointerMove(event: PointerEvent): void;
  onFocusIn(): void;
  onFocusOut(): void;
  dispose(): void;
}
~~~

状态机不直接查询播放器或皮肤。

- [ ] **Step 3: 两套皮肤独立渲染**

Aurora 控制为轻量流动 dock；Newsprint 为紧凑工具条。默认 opacity/pointer-events 隐藏，visible/focus-within 时恢复。不得遮住当前歌词行。

- [ ] **Step 4: 顶部窗口控件收敛**

全屏只显示最小化和退出全屏图标；删除额外“全屏/退出全屏”文字。保持 title/aria-label。

- [ ] **Step 5: 运行 GREEN**

~~~powershell
pnpm test -- src/views/lyric/__tests__/useAutoHideControls.test.ts src/views/lyric/__tests__/LyricStages.test.ts src/components/shell/__tests__/FullscreenWindowControls.test.ts
~~~

- [ ] **Step 6: 提交**

~~~powershell
git add ui/src/views/lyric/useAutoHideControls.ts ui/src/views/lyric/__tests__/useAutoHideControls.test.ts ui/src/views/lyric/AuroraLyricStage.vue ui/src/views/lyric/NewsprintLyricStage.vue ui/src/views/lyric/__tests__/LyricStages.test.ts ui/src/components/shell/FullscreenWindowControls.vue ui/src/components/shell/__tests__/FullscreenWindowControls.test.ts
git commit -m "feat(ui): reveal fullscreen transport only on intent"
~~~

## Task 5: 首页状态、品牌与中文化

**Recommended agent:** Terra，高思考；视觉状态和文案跨多个组件。

**Files:**
- Modify: ui/src/views/home/AuroraHome.vue
- Modify: ui/src/views/home/NewsprintHome.vue
- Modify: ui/src/views/home/__tests__/AuroraHome.test.ts
- Modify: ui/src/views/home/__tests__/NewsprintHome.test.ts
- Modify: ui/src/components/Sidebar.vue
- Modify: ui/src/components/shell/AuroraShell.vue
- Modify: ui/src/components/shell/NewsprintShell.vue
- Modify: ui/src/components/shell/__tests__/Shells.test.ts
- Modify: ui/src/styles/skins/aurora.css
- Modify: ui/src/styles/skins/newsprint.css

- [ ] **Step 1: 写状态与文案 RED**

覆盖首次 skeleton、缓存刷新不闪空、单区 error/retry、空状态；断言首页当前播放舞台不等待 feed 请求，已有 currentTrack 时立即显示，没有歌曲时显示明确空播放状态；断言 skin label 为“极光 Aurora”“报刊 Newsprint”，功能文案中无单独 newspaper/newspaper mode。

- [ ] **Step 2: 为两套皮肤分别实现状态**

Aurora skeleton/refresh 有柔和流动和渐进 reveal；Newsprint 用轻微淡入/版面占位。二者复用 store 语义，不共享视觉 skeleton 组件。

- [ ] **Step 3: 品牌字标**

统一内容 BottleMusic；Aurora 使用轻盈现代字标并显示“极光 Aurora”；Newsprint 使用 masthead 式衬线字标并显示“报刊 Newsprint”。不生成图片或手写 SVG logo。

- [ ] **Step 4: 全局功能文案中文化**

搜索 Settings、Shell、Player、Lyric、Home 中英文功能标签；品牌、歌曲、艺人和标准音质缩写不翻译。装饰英文必须有中文主标题。

- [ ] **Step 5: 运行 GREEN**

~~~powershell
pnpm test -- src/views/home/__tests__/AuroraHome.test.ts src/views/home/__tests__/NewsprintHome.test.ts src/components/__tests__/Sidebar.test.ts src/components/shell/__tests__/Shells.test.ts
~~~

- [ ] **Step 6: 提交**

~~~powershell
git add ui/src/views/home/AuroraHome.vue ui/src/views/home/NewsprintHome.vue ui/src/views/home/__tests__/AuroraHome.test.ts ui/src/views/home/__tests__/NewsprintHome.test.ts ui/src/components/Sidebar.vue ui/src/components/shell/AuroraShell.vue ui/src/components/shell/NewsprintShell.vue ui/src/components/shell/__tests__/Shells.test.ts ui/src/styles/skins/aurora.css ui/src/styles/skins/newsprint.css
git commit -m "feat(ui): complete skin-specific states and Chinese labels"
~~~

## Task 6: 动效强化与 3D 回归

**Recommended agent:** Terra，高思考。

**Files:**
- Modify: ui/src/api/motionProfiles.ts
- Modify: ui/src/api/motion.ts
- Modify: ui/src/api/__tests__/motionProfiles.test.ts
- Modify: ui/src/api/__tests__/motion.test.ts
- Modify: ui/src/components/player/AuroraDockParticles.vue
- Modify: ui/src/views/lyric/CoverWebGLParticles.vue
- Modify: ui/src/views/lyric/AuroraPlaylistShelf.vue
- Modify: ui/src/views/lyric/__tests__/LyricStages.test.ts
- Modify: ui/src/views/home/__tests__/AuroraAtmosphere.test.ts

- [ ] **Step 1: 写动效合同 RED**

Aurora 使用更长但可中断的弹性/果冻反馈；Newsprint 只短距离淡入。粒子速度受统一 profile 控制并低于当前值；reduced-motion 时停止循环粒子和位移，但保留最终状态。

- [ ] **Step 2: 只动画 transform/opacity**

修复首页歌单边框抖动：禁止循环动画 border-width、box-shadow spread、width/height 或布局位置。粒子 canvas 与 DOM 不触发列表 reflow。

- [ ] **Step 3: 保持 3D 歌单现有合同**

只补测试：
- 数据源等于 playerStore.queue。
- active item 等于 currentTrack。
- 自动跟随只在用户未 hover/drag 时执行。
- 选择歌曲保持 fullscreen。
- 空队列显示明确空状态。
- 非全屏点击封面不打开 shelf。

- [ ] **Step 4: 运行 GREEN**

~~~powershell
pnpm test -- src/api/__tests__/motionProfiles.test.ts src/api/__tests__/motion.test.ts src/views/home/__tests__/AuroraAtmosphere.test.ts src/views/lyric/__tests__/LyricStages.test.ts
~~~

- [ ] **Step 5: 提交**

~~~powershell
git add ui/src/api/motionProfiles.ts ui/src/api/motion.ts ui/src/api/__tests__/motionProfiles.test.ts ui/src/api/__tests__/motion.test.ts ui/src/components/player/AuroraDockParticles.vue ui/src/views/lyric/CoverWebGLParticles.vue ui/src/views/lyric/AuroraPlaylistShelf.vue ui/src/views/lyric/__tests__/LyricStages.test.ts ui/src/views/home/__tests__/AuroraAtmosphere.test.ts
git commit -m "feat(ui): deepen skin motion without layout jitter"
~~~

## Task 7: 双皮肤阶段回归

**Recommended agent:** Luna，高思考；运行验证和静态扫描。

- [ ] **Step 1: 测试**

~~~powershell
pnpm test -- src/components/player/__tests__ src/components/shell/__tests__ src/views/home/__tests__ src/views/lyric/__tests__
~~~

- [ ] **Step 2: 手写资产与中文命令扫描**

~~~powershell
rg -n "<svg|进入全屏|>词<|>播放<|>暂停<|>上一首<|>下一首<" ui/src/components/player ui/src/views/lyric
~~~

Expected: 播放器/歌词命令无手写 SVG 和可见中文命令；aria-label/title 中文命中允许存在。

- [ ] **Step 3: 类型与构建**

~~~powershell
pnpm exec vue-tsc --noEmit
pnpm build
~~~

- [ ] **Step 4: 主代理审阅**

重点手工检查交互映射、两栏布局、进度对比、follow footer、全屏自动隐藏、3D 保持全屏、两套皮肤动效差异。未完成视觉 QA 前不得宣称收尾。
