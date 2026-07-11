# BottleMusic 双界面播放器重构实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task with review checkpoints.

**目标：** 在不改变播放后端、AudioProxy、数据接口和现有技术栈的前提下，完成 Aurora 与 Newsprint 两套独立播放器界面，支持各自的明亮/深色光感，修复首页返回重载、深色进度不可见、顶部横线、歌词按钮遮挡与全屏控制拥挤问题，并升级两套界面的动效性格。

**架构：** 保留 Vue 3 + TypeScript + Tauri 2 + GSAP。业务状态下沉到共享无头控制器，Shell、首页、底部播放器和歌词舞台按皮肤使用独立模板。`themeStore` 只选择皮肤与明暗 token；数据、播放、导航、歌词解析和窗口命令不在皮肤组件中复制。首页通过稳定组件身份、`KeepAlive` 与单例 feed controller 保留数据和滚动位置。

**技术栈：** Vue 3 Composition API、TypeScript、Tauri 2、GSAP 3.15、Vitest 4、Vue Test Utils、Vite 6、CSS custom properties。

**视觉基线：** Aurora 采用用户确认的第一套沉浸式舞台方向：左侧导航、中部封面与当前音乐主舞台、右侧队列、底部一体化播放控制台；参考 Mineradio 的内容聚焦方式，不复制其品牌、素材、源码或具体交互。Newsprint 保持编辑版/广播台气质，通过排版、块面、编号和真实封面建立辨识度，不依赖重复横线。

**全局约束：**

- 两套皮肤必须拥有不同的 Shell、Home、PlayerBar、LyricStage 模板，不能只换颜色和字体。
- Aurora 与 Newsprint 都必须支持 light/dark，最终验收矩阵为 2 个皮肤 x 2 个模式。
- 不引入 Motion/Framer Motion 或其他动画依赖；所有动效通过现有 GSAP 与 CSS 完成。
- 播放、seek、导航必须先发生，动画不得延迟业务动作；交互反馈在 100ms 内开始。
- `prefers-reduced-motion: reduce`、窗口失焦、页面隐藏时关闭非必要连续动效。
- 本轮不修改 Rust、C++、AudioProxy、EQ、统计数据库、登录协议或后端响应结构。
- 每个任务遵循 Red -> Green -> Refactor：先写失败测试，确认失败原因正确，再写最小实现并运行相关测试。
- 每个阶段只提交该阶段文件；不得夹带构建产物、截图缓存或无关格式化。

---

## Task 1：首页数据控制器，先消除“返回后重新填充”

**文件：**

- 新建：`ui/src/api/homeFeedStore.ts`
- 新建：`ui/src/api/__tests__/homeFeedStore.test.ts`
- 修改：`ui/src/views/HomeView.vue`
- 修改：`ui/src/views/__tests__/HomeView.test.ts`

### Step 1：为首页缓存、并发去重和保留旧数据写失败测试

在 `homeFeedStore.test.ts` 覆盖以下契约：

```ts
it('reuses cached sections when ensureLoaded is called again', async () => {
  await store.ensureLoaded();
  await store.ensureLoaded();
  expect(apiGet).toHaveBeenCalledTimes(3);
});

it('deduplicates concurrent refresh calls and keeps old data visible', async () => {
  await store.ensureLoaded();
  const oldDaily = store.daily.items;
  const first = store.refresh();
  const second = store.refresh();
  expect(first).toBe(second);
  expect(store.daily.items).toBe(oldDaily);
  await first;
});

it('keeps successful sections when one refresh section fails', async () => {
  // daily rejects; playlists and albums resolve
  await store.refresh();
  expect(store.daily.items).toEqual(previousDaily);
  expect(store.daily.error).toBeTruthy();
  expect(store.playlists.items).toEqual(newPlaylists);
});
```

控制器公开稳定接口：

```ts
export interface HomeSectionState<T> {
  items: T[];
  loading: boolean;
  refreshing: boolean;
  error: string | null;
  loaded: boolean;
}

export function useHomeFeedStore(): {
  daily: HomeSectionState<Track>;
  playlists: HomeSectionState<PlaylistInfo>;
  albums: HomeSectionState<PlaylistInfo>;
  ensureLoaded(): Promise<void>;
  refresh(): Promise<void>;
};
```

### Step 2：运行测试并确认失败

```powershell
cd C:\BottleMusic\ui
pnpm test -- src/api/__tests__/homeFeedStore.test.ts
```

预期：测试因 `homeFeedStore.ts` 不存在或导出缺失而失败，而不是测试环境错误。

### Step 3：实现单例 reactive store

- 把 `HomeView.vue` 中三组 section state、三个 loader 和响应归一化逻辑迁入 `homeFeedStore.ts`。
- `ensureLoaded()` 只加载 `loaded === false` 的 section。
- `refresh()` 保留旧 `items`，只设置 `refreshing`；每个 section 独立更新、独立保留错误。
- 使用模块级 `inFlight: Promise<void> | null` 合并重复请求，并在 `finally` 清空；`refresh()` 不声明为 `async`，直接返回已有 `inFlight`，从而保证并发调用获得同一个 Promise 实例。
- 提供 `__resetHomeFeedForTest()`，只用于测试隔离。
- 不把任何 Aurora/Newsprint DOM 或样式写进控制器。

### Step 4：让 HomeView 消费 controller

- `onMounted` 改为 `homeFeed.ensureLoaded()`。
- 显式“刷新推荐”调用 `homeFeed.refresh()`。
- 有旧数据时只显示局部刷新态，不切回骨架或空状态。
- 保留 `HomeView.test.ts` 现有“section 失败隔离”和 hero 播放测试，并新增“卸载后重新挂载不重复请求”。

### Step 5：运行测试与类型检查

```powershell
cd C:\BottleMusic\ui
pnpm test -- src/api/__tests__/homeFeedStore.test.ts src/views/__tests__/HomeView.test.ts
pnpm exec vue-tsc --noEmit
```

预期：相关测试全部通过，TypeScript 无错误。

### Step 6：提交

```powershell
git add ui/src/api/homeFeedStore.ts ui/src/api/__tests__/homeFeedStore.test.ts ui/src/views/HomeView.vue ui/src/views/__tests__/HomeView.test.ts
git commit -m "fix: preserve home feed across navigation"
```

---

## Task 2：稳定视图身份、KeepAlive 与滚动恢复

**文件：**

- 新建：`ui/src/api/viewRegistry.ts`
- 新建：`ui/src/api/__tests__/viewRegistry.test.ts`
- 修改：`ui/src/App.vue`
- 修改：`ui/src/views/__tests__/AppNetworkBanner.test.ts`
- 修改：`ui/src/views/HomeView.vue`

### Step 1：为稳定 cache key 与首页保活写失败测试

`viewRegistry.test.ts` 验证：

- `home` 的 `cacheKey` 永远是 `home`。
- playlist 使用 `playlist:<id>`，search 使用已提交查询 `search:<query>`。
- `transitionKey` 可变化，但不能改变 `cacheKey`。

`AppNetworkBanner.test.ts` 新增集成测试：

```ts
it('keeps the same HomeView instance when navigating away and back', async () => {
  // home mount count is 1
  // navigate to stats, then back home
  // home mount count remains 1 and its scrollTop is restored
});
```

### Step 2：运行测试并确认现状会重建 HomeView

```powershell
cd C:\BottleMusic\ui
pnpm test -- src/api/__tests__/viewRegistry.test.ts src/views/__tests__/AppNetworkBanner.test.ts
```

预期：稳定 key 测试因模块缺失失败；集成测试显示返回首页时 mount count 增加。

### Step 3：实现 view descriptor

在 `viewRegistry.ts` 定义：

```ts
export interface ViewDescriptor {
  name: ViewName;
  component: Component;
  cacheKey: string;
  keepAlive: boolean;
}

export function resolveViewDescriptor(entry: HistoryEntry): ViewDescriptor;
```

- 删除 `viewTransitionVersion` 对组件身份的参与。
- `App.vue` 使用动态组件；缓存视图放在 `<KeepAlive include="HomeView">` 内。
- 动画容器可使用独立 transition token，但不得作为 HomeView 的 Vue key。
- 只缓存首页；搜索和歌单仍按明确 key 新建，避免缓存无限增长。

### Step 4：保存并恢复首页滚动位置

- `HomeView.vue` 在 `onDeactivated` 记录自己的内容滚动容器位置，在 `onActivated` 的 `nextTick` 恢复。
- 位置按 `skinId` 分开保存：`home:aurora` 与 `home:newsprint`，避免两套布局高度不同导致跳位。
- 首次进入不应用缓存位置；已有实例返回时不重新播放完整首屏入场。

### Step 5：回归导航与网络 banner

```powershell
cd C:\BottleMusic\ui
pnpm test -- src/api/__tests__/viewRegistry.test.ts src/views/__tests__/AppNetworkBanner.test.ts src/views/__tests__/HomeView.test.ts
pnpm exec vue-tsc --noEmit
```

预期：返回首页 mount count 保持 1，三组首页 API 调用数不增加，已有网络 banner 与搜索不重建测试继续通过。

### Step 6：提交

```powershell
git add ui/src/api/viewRegistry.ts ui/src/api/__tests__/viewRegistry.test.ts ui/src/App.vue ui/src/views/__tests__/AppNetworkBanner.test.ts ui/src/views/HomeView.vue
git commit -m "refactor: keep home view identity stable"
```

---

## Task 3：四套光感 token、顶部融合与可访问进度组件

**文件：**

- 新建：`ui/src/styles/tokens.css`
- 新建：`ui/src/styles/progress.css`
- 新建：`ui/src/components/player/PlayerProgress.vue`
- 新建：`ui/src/components/player/__tests__/PlayerProgress.test.ts`
- 修改：`ui/src/main.ts`
- 修改：`ui/src/style.css`
- 修改：`ui/src/components/PlayerBar.vue`
- 修改：`ui/src/api/__tests__/themeStore.test.ts`

### Step 1：为进度语义和键盘 seek 写失败测试

`PlayerProgress.test.ts` 覆盖：

- 渲染 `role="slider"`、`aria-valuemin=0`、`aria-valuemax=duration`、`aria-valuenow=currentTime`。
- 点击轨道按相对位置 emit `seek`。
- `ArrowLeft/ArrowRight` 每次移动 5 秒，`Home/End` 跳首尾，并 clamp 到 `[0, duration]`。
- 轨道、fill、thumb 与时间标签使用稳定 class，不在组件内写皮肤条件分支。
- duration 为 0 时不产生 `NaN`，禁用 seek。

### Step 2：运行测试并确认失败

```powershell
cd C:\BottleMusic\ui
pnpm test -- src/components/player/__tests__/PlayerProgress.test.ts
```

预期：组件不存在导致失败。

### Step 3：定义四套语义 token

`tokens.css` 必须显式包含：

```css
:root[data-skin='aurora'][data-mode='light'] { /* Aurora 白光 */ }
:root[data-skin='aurora'][data-mode='dark']  { /* Aurora 黑光 */ }
:root[data-skin='newsprint'][data-mode='light'] { /* Newsprint 纸面白光 */ }
:root[data-skin='newsprint'][data-mode='dark']  { /* Newsprint 墨面黑光 */ }
```

每组至少定义：

- `--app-bg`、`--surface-1`、`--surface-2`、`--surface-elevated`
- `--text-primary`、`--text-secondary`、`--text-muted`
- `--accent`、`--focus-ring`、`--border-subtle`
- `--progress-track`、`--progress-buffered`、`--progress-fill`
- `--progress-thumb-fill`、`--progress-thumb-ring`、`--progress-time`

暗色值必须单独选色，禁止通过给浅色变量套低 opacity 推导。使用浏览器 contrast 工具确认进度轨道相对背景达到至少 3:1 非文本对比。

### Step 4：实现 PlayerProgress

- 组件只接收 `currentTime`、`duration`、可选 `buffered`，只 emit `seek`。
- 可见轨道约 3px，交互 hit area 至少 16px。
- thumb 默认可见；`:hover` 与 `:focus-visible` 增大并显示 ring。
- 使用 CSS 自定义属性传递百分比，不逐帧改 DOM 宽度动画。
- `PlayerBar.vue` 暂时接入共享进度组件，后续两套 player bar 继续复用 seek 语义。

### Step 5：删除顶部装饰横线

在 `style.css` 移除：

- `.titlebar` 的 `border-bottom`。
- `.titlebar::after`。
- `.page-head` 与 lyric header 只用于装饰的双线/伪元素。
- 不能承载层级信息的顶部 rule。

用背景表面、间距、字级和局部阴影建立分区，不为 Newsprint 偷偷保留顶部双线。

### Step 6：验证四种主题与交互

```powershell
cd C:\BottleMusic\ui
pnpm test -- src/components/player/__tests__/PlayerProgress.test.ts src/api/__tests__/themeStore.test.ts
pnpm exec vue-tsc --noEmit
```

浏览器检查 Aurora light/dark、Newsprint light/dark：默认、hover、focus、0%、50%、100% 五种进度状态均清晰。

### Step 7：提交

```powershell
git add ui/src/styles/tokens.css ui/src/styles/progress.css ui/src/components/player/PlayerProgress.vue ui/src/components/player/__tests__/PlayerProgress.test.ts ui/src/main.ts ui/src/style.css ui/src/components/PlayerBar.vue ui/src/api/__tests__/themeStore.test.ts
git commit -m "feat: add accessible progress and four light modes"
```

---

## Task 4：按皮肤建立可取消的 MotionProfile

**文件：**

- 新建：`ui/src/api/motionProfiles.ts`
- 新建：`ui/src/api/__tests__/motionProfiles.test.ts`
- 修改：`ui/src/api/motion.ts`
- 修改：`ui/src/api/__tests__/motion.test.ts`

### Step 1：先写 profile 选择和清理测试

测试契约：

```ts
expect(getMotionProfile('aurora').controlRelease.ease).toContain('elastic.out');
expect(getMotionProfile('newsprint').pageEnter.ease).toBe('power3.out');
expect(getMotionProfile('newsprint').ambient.enabled).toBe(false);
```

另测：

- 同一元素启动新 tween 前调用 `gsap.killTweensOf(el)`。
- helper 返回具有 `kill()` 的 handle。
- reduced-motion 直接写最终态且不调用 `gsap.to`。
- `document.hidden === true` 或 `window.blur` 时 ambient tween pause，重新可见时仅在正在播放且 Aurora 下 resume。

### Step 2：运行并确认失败

```powershell
cd C:\BottleMusic\ui
pnpm test -- src/api/__tests__/motionProfiles.test.ts src/api/__tests__/motion.test.ts
```

### Step 3：实现 profile 与统一 helper

类型保持显式：

```ts
export interface MotionProfile {
  pageEnter: TweenSpec;
  pageLeave: TweenSpec;
  controlPress: TweenSpec;
  controlRelease: TweenSpec;
  cardEnter: TweenSpec & { stagger: number; maxItems: number };
  ambient: { enabled: boolean; duration: number; scale: number };
}
```

- Aurora：`expo.out` / `back.out(1.35)`，主控件释放使用低强度 `elastic.out`，舞台呼吸周期 >=5 秒、scale <=1.015。
- Newsprint：`power3.out` 页面入场、`power2.out` 按压，禁止 elastic 与环境循环。
- 把散落的 transition 参数改为通过 `getMotionProfile(skinId)` 读取。
- 为 `animateElement`、`animateStagger`、`startAmbientMotion` 提供可取消 handle；组件卸载统一 `kill()`。
- 不 tween 大面积 `filter`、`box-shadow`、`width`、`height` 或 `backdrop-filter`。

### Step 4：验证快速导航和 reduced motion

```powershell
cd C:\BottleMusic\ui
pnpm test -- src/api/__tests__/motionProfiles.test.ts src/api/__tests__/motion.test.ts
pnpm exec vue-tsc --noEmit
```

### Step 5：提交

```powershell
git add ui/src/api/motionProfiles.ts ui/src/api/__tests__/motionProfiles.test.ts ui/src/api/motion.ts ui/src/api/__tests__/motion.test.ts
git commit -m "feat: define distinct skin motion profiles"
```

---

## Task 5：独立 Shell 与皮肤原语

**文件：**

- 新建：`ui/src/components/shell/AuroraShell.vue`
- 新建：`ui/src/components/shell/NewsprintShell.vue`
- 新建：`ui/src/components/shell/WindowControls.vue`
- 新建：`ui/src/components/shell/__tests__/Shells.test.ts`
- 新建：`ui/src/styles/skins/aurora.css`
- 新建：`ui/src/styles/skins/newsprint.css`
- 修改：`ui/src/App.vue`
- 修改：`ui/src/main.ts`
- 修改：`ui/src/components/Sidebar.vue`
- 修改：`ui/src/components/Topbar.vue`

### Step 1：为不同结构而非不同 class 写失败测试

`Shells.test.ts` 必须断言：

- Aurora 渲染 `data-shell="aurora"`、沉浸舞台 slot、紧凑左侧导航和统一命令区。
- Newsprint 渲染 `data-shell="newsprint"`、目录式导航、编辑版 masthead 区和内容版面。
- 两者的关键 DOM 层级和 landmark 不相同，而不是同一模板加不同根 class。
- 切换 `skinId` 后当前 view、当前曲目、queue、currentTime 不变。
- 普通模式的窗口控制恰好为最小化、最大化、关闭。

### Step 2：运行测试并确认失败

```powershell
cd C:\BottleMusic\ui
pnpm test -- src/components/shell/__tests__/Shells.test.ts
```

### Step 3：实现 AuroraShell

- 对齐用户选择的第一套视觉：窄左导航、中部舞台、右侧可选上下文区、底部播放器插槽。
- 标题栏与顶部搜索/命令区融合为连续表面，不显示顶横线。
- Aurora light 使用清洁冷白表面与高辨识绿色 accent；Aurora dark 使用 graphite 黑光、清晰层级和实色文字。
- 控件圆角、表面和焦点形态由 Aurora token 控制。

### Step 4：实现 NewsprintShell

- 使用目录编号、字重、缩进与报刊块面组织导航，不复用 Aurora 胶囊。
- Newsprint light 为纸面白光，Newsprint dark 为墨面黑光；二者都保持正文高对比。
- 复古感来自 serif 标题、紧凑 sans/mono 元数据和排版节奏，不恢复顶部装饰横线。

### Step 5：收敛共享窗口命令

- `WindowControls.vue` 只封装 Tauri minimize/maximize/close 行为与可访问标签。
- Shell 只决定控件的位置与视觉，不复制 invoke 逻辑。
- `Sidebar.vue` / `Topbar.vue` 保留业务事件，外观由 Shell 或皮肤原语组合；禁止继续作为两套皮肤共同的完整布局骨架。

### Step 6：测试与提交

```powershell
cd C:\BottleMusic\ui
pnpm test -- src/components/shell/__tests__/Shells.test.ts src/views/__tests__/AppNetworkBanner.test.ts
pnpm exec vue-tsc --noEmit
```

```powershell
git add ui/src/components/shell ui/src/styles/skins ui/src/App.vue ui/src/main.ts ui/src/components/Sidebar.vue ui/src/components/Topbar.vue
git commit -m "feat: introduce independent aurora and newsprint shells"
```

---

## Task 6：两套独立首页，落地选定 Aurora 舞台与 Newsprint 头版

**文件：**

- 新建：`ui/src/views/home/AuroraHome.vue`
- 新建：`ui/src/views/home/NewsprintHome.vue`
- 新建：`ui/src/views/home/homeViewModel.ts`
- 新建：`ui/src/views/home/__tests__/AuroraHome.test.ts`
- 新建：`ui/src/views/home/__tests__/NewsprintHome.test.ts`
- 修改：`ui/src/views/HomeView.vue`
- 修改：`ui/src/views/__tests__/HomeView.test.ts`
- 修改：`ui/src/styles/skins/aurora.css`
- 修改：`ui/src/styles/skins/newsprint.css`

### Step 1：写两套首页结构和共享行为测试

Aurora 测试：

- 主舞台优先显示当前播放曲目；无当前曲目时使用每日推荐第一首。
- 中部展示完整封面、歌名、歌手、来源和主播放按钮。
- 右侧队列/推荐预览有真实曲目内容，不用装饰 SVG 唱片填空。
- 长歌名换行且不挤压主播放按钮。

Newsprint 测试：

- 头版模块展示主封面、主推荐标题、编辑短句和前三首编号推荐。
- 与 Aurora 的 DOM 结构不同。
- hover/播放状态只改变展示，不发起新 API。

共享测试：

- 两套组件收到同一个 `HomeViewModel`。
- 主播放按钮都 emit 同一种 `play-track` 事件。
- 手动刷新都 emit `refresh`，刷新期间旧内容仍存在。

### Step 2：运行并确认失败

```powershell
cd C:\BottleMusic\ui
pnpm test -- src/views/home/__tests__/AuroraHome.test.ts src/views/home/__tests__/NewsprintHome.test.ts
```

### Step 3：建立只读 HomeViewModel

`homeViewModel.ts` 只负责把 store 与 player 状态整形成展示数据：

```ts
export interface HomeViewModel {
  heroTrack: Track | null;
  dailyTracks: readonly Track[];
  playlists: readonly PlaylistInfo[];
  albums: readonly PlaylistInfo[];
  queuePreview: readonly Track[];
  isInitialLoading: boolean;
  isRefreshing: boolean;
  errors: readonly HomeSectionError[];
}
```

不得在 Aurora/Newsprint 子组件中调用 `apiGet`。

### Step 4：实现 AuroraHome

- 中部使用大幅完整正方形封面，旁边/下方提供歌曲名、歌手、来源、队列数量和主播放入口。
- 右侧为队列与推荐预览，首屏信息密度接近已选视觉，不留大面积纯装饰空白。
- 封面加载前保留 `aspect-ratio: 1`；失败时使用 Aurora 占位，不造成布局跳动。
- 入场使用 Aurora profile；返回缓存首页不重新播放整套入场。
- 主播放按钮 press 立即 scale，release 低强度回弹；业务播放先执行。

### Step 5：实现 NewsprintHome

- 主推荐采用非对称头版排版，完整真实封面 + 标题 + 3 条编号推荐。
- 封面默认轻微降饱和可用静态样式实现，hover/播放用 opacity overlay 过渡恢复颜色，不连续 tween filter。
- 推荐列表以最多 10 项、20-35ms stagger 入场；页面稳定后停止。

### Step 6：HomeView 只做皮肤分发

- `HomeView.vue` 读取 `themeStore.skinId`，选择 AuroraHome 或 NewsprintHome。
- 保留同一 controller 和同一缓存实例；切皮肤不重新请求。
- 切皮肤时保存各自滚动位置并执行短交叉淡化。

### Step 7：测试与提交

```powershell
cd C:\BottleMusic\ui
pnpm test -- src/views/home/__tests__/AuroraHome.test.ts src/views/home/__tests__/NewsprintHome.test.ts src/views/__tests__/HomeView.test.ts
pnpm exec vue-tsc --noEmit
```

```powershell
git add ui/src/views/home ui/src/views/HomeView.vue ui/src/views/__tests__/HomeView.test.ts ui/src/styles/skins/aurora.css ui/src/styles/skins/newsprint.css
git commit -m "feat: build distinct home experiences"
```

---

## Task 7：共享 PlayerController 与两套独立底部播放器

**文件：**

- 新建：`ui/src/components/player/usePlayerControls.ts`
- 新建：`ui/src/components/player/AuroraPlayerBar.vue`
- 新建：`ui/src/components/player/NewsprintPlayerBar.vue`
- 新建：`ui/src/components/player/__tests__/usePlayerControls.test.ts`
- 新建：`ui/src/components/player/__tests__/AuroraPlayerBar.test.ts`
- 新建：`ui/src/components/player/__tests__/NewsprintPlayerBar.test.ts`
- 修改：`ui/src/components/PlayerBar.vue`
- 修改：`ui/src/styles/skins/aurora.css`
- 修改：`ui/src/styles/skins/newsprint.css`

### Step 1：先锁定共享行为契约

`usePlayerControls.test.ts` 验证：

- play/pause、上一首、下一首、循环、随机、音量、质量、歌词和 queue 行为与现有 `PlayerBar.vue` 一致。
- seek 会 clamp 并调用现有 player store/API 一次。
- controller 暴露 readonly view model 和命令函数，不暴露皮肤 DOM。

两个展示测试验证：

- Aurora 与 Newsprint 都消费同一 controller stub。
- 两套模板都渲染当前封面、歌名、歌手、时间、PlayerProgress 和核心播放控制。
- Aurora 与 Newsprint 的 DOM 分区不同。
- 空曲目、加载、长歌名和无封面状态不溢出。

### Step 2：运行并确认失败

```powershell
cd C:\BottleMusic\ui
pnpm test -- src/components/player/__tests__/usePlayerControls.test.ts src/components/player/__tests__/AuroraPlayerBar.test.ts src/components/player/__tests__/NewsprintPlayerBar.test.ts
```

### Step 3：从现有 PlayerBar 抽离业务逻辑

- 把 computed、播放命令、seek、音量、模式切换和快捷入口迁入 `usePlayerControls.ts`。
- 保持现有 store/API 调用顺序，避免视觉重构改变播放语义。
- `PlayerBar.vue` 变为按 `skinId` 选择展示组件的薄 wrapper。

### Step 4：实现 AuroraPlayerBar

- 采用已选视觉的一体化控制台：左侧封面/元数据，中部主控制 + 横向进度，右侧音质/队列/歌词/音量。
- 收紧高度和空白，主播放按钮成为明确视觉中心但不挤压进度。
- 主按钮果冻反馈只作用于按钮；进度 thumb 拖动/点击反馈短促，不延迟 seek。
- light/dark 均使用 Task 3 的专用进度 token。

### Step 5：实现 NewsprintPlayerBar

- 采用紧凑广播台播出条，不复用 Aurora 胶囊容器与圆形按钮组。
- 通过字级、编号、直线进度和块面建立节奏；按压反馈为 1-2px 位移与阴影收紧，无弹性过冲。
- 保证 1280px 宽度下核心播放、进度和音量不重叠；次要控制可收进菜单。

### Step 6：测试、键盘验收与提交

```powershell
cd C:\BottleMusic\ui
pnpm test -- src/components/player/__tests__
pnpm exec vue-tsc --noEmit
```

手动测试：鼠标点击、键盘 seek、0/100% 边界、暂停/播放快速切换、长歌名、无封面、四种皮肤/模式组合。

```powershell
git add ui/src/components/PlayerBar.vue ui/src/components/player ui/src/styles/skins/aurora.css ui/src/styles/skins/newsprint.css
git commit -m "feat: redesign both player consoles"
```

---

## Task 8：独立歌词舞台、底部跟随区与精简全屏控制

**文件：**

- 新建：`ui/src/views/lyric/useLyricStage.ts`
- 新建：`ui/src/views/lyric/AuroraLyricStage.vue`
- 新建：`ui/src/views/lyric/NewsprintLyricStage.vue`
- 新建：`ui/src/views/lyric/LyricFollowFooter.vue`
- 新建：`ui/src/components/shell/FullscreenWindowControls.vue`
- 新建：`ui/src/views/lyric/__tests__/LyricStages.test.ts`
- 新建：`ui/src/components/shell/__tests__/FullscreenWindowControls.test.ts`
- 修改：`ui/src/views/LyricView.vue`
- 修改：`ui/src/views/__tests__/LyricView.test.ts`
- 修改：`ui/src/App.vue`
- 修改：`ui/src/style.css`
- 修改：`ui/src/styles/skins/aurora.css`
- 修改：`ui/src/styles/skins/newsprint.css`

### Step 1：扩展歌词与全屏失败测试

在现有 `LyricView.test.ts` 基础上增加：

- 普通歌词为 `meta / scroll viewport / follow footer` 三行布局。
- 自动跟随开启时 footer 仍保留高度，但按钮不可见且不可聚焦。
- 暂停跟随时按钮只出现在 footer，不是 lyric viewport 的 absolute/fixed 子元素。
- 最后一行歌词有足够底部空间，不被 footer 遮挡。
- 全屏恰好显示两个窗口操作：最小化、退出全屏。
- 全屏隐藏 Logo、Working Set、最大化、关闭和普通 fullscreen toggle。
- Esc 与退出按钮都能退出；封面始终为正方形。

`LyricStages.test.ts` 断言 Aurora/Newsprint 模板结构和 motion profile 不同，但当前行、自动跟随与播放数据相同。

### Step 2：运行并确认当前重叠/重复按钮测试失败

```powershell
cd C:\BottleMusic\ui
pnpm test -- src/views/__tests__/LyricView.test.ts src/views/lyric/__tests__/LyricStages.test.ts src/components/shell/__tests__/FullscreenWindowControls.test.ts
```

### Step 3：抽离歌词 controller

- 把歌词加载、解析、active index、自动跟随、wheel 暂停、恢复当前行、track change reset 和 fullscreen 状态放入 `useLyricStage.ts`。
- 继续使用现有 `lyricFullscreen.ts` 作为全局全屏信号。
- 同一时刻只允许一个 scroll 行为；新 active line 到达前取消/覆盖旧行为。
- controller 不包含皮肤 class 或 GSAP ease。

### Step 4：实现底部跟随操作区

- `LyricFollowFooter.vue` 固定占据歌词网格第三行。
- 跟随开启时用 `visibility: hidden` + `pointer-events: none` 保留高度，不用 `display: none` 引发布局跳动。
- 按钮文案统一为“回到当前行”，点击立即恢复并滚动当前行。

### Step 5：实现两套歌词舞台

Aurora：

- 普通模式保持完整封面与高对比当前歌词，邻近歌词三级弱化。
- 全屏使用独立舞台模板和轻微容器淡入/封面 scale，不在普通模板上叠 fixed 面板。
- 黑光/白光都避免整页低 opacity。

Newsprint：

- 使用编辑排版、行号/元数据与纸面块面，当前行通过字重和块面切换。
- 入场使用短 `power3.out`，歌词滚动不使用弹性。

### Step 6：实现专用全屏窗口控制

- `FullscreenWindowControls.vue` 只提供 minimize 与 exit fullscreen。
- `App.vue` 在 `lyricFullscreen` 时渲染该控件并隐藏普通 titlebar/window controls。
- 删除 LyricView 内重复的普通 fullscreen 入口和额外 fixed exit 按钮。
- 保留安全拖拽区域，不让按钮互相挤压。

### Step 7：验证与提交

```powershell
cd C:\BottleMusic\ui
pnpm test -- src/views/__tests__/LyricView.test.ts src/views/lyric/__tests__/LyricStages.test.ts src/components/shell/__tests__/FullscreenWindowControls.test.ts src/api/__tests__/lyricFullscreen.test.ts
pnpm exec vue-tsc --noEmit
```

```powershell
git add ui/src/views/LyricView.vue ui/src/views/lyric ui/src/views/__tests__/LyricView.test.ts ui/src/components/shell/FullscreenWindowControls.vue ui/src/components/shell/__tests__/FullscreenWindowControls.test.ts ui/src/App.vue ui/src/style.css ui/src/styles/skins
git commit -m "feat: rebuild lyric stages and fullscreen controls"
```

---

## Task 9：次级页面皮肤原语与密度收口

**文件：**

- 新建：`ui/src/components/primitives/SkinPageHeader.vue`
- 新建：`ui/src/components/primitives/SkinButton.vue`
- 新建：`ui/src/components/primitives/SkinListRow.vue`
- 新建：`ui/src/components/primitives/SkinEmptyState.vue`
- 新建：`ui/src/components/primitives/__tests__/SkinPrimitives.test.ts`
- 修改：`ui/src/views/StatsView.vue`
- 修改：`ui/src/views/HistoryView.vue`
- 修改：`ui/src/views/SettingsView.vue`
- 修改：`ui/src/views/EqualizerView.vue`
- 修改：`ui/src/style.css`
- 修改：`ui/src/styles/skins/aurora.css`
- 修改：`ui/src/styles/skins/newsprint.css`

### Step 1：为皮肤原语写结构测试

- 同一语义按钮在 Aurora/Newsprint 下使用不同结构性 variant，而非直接复用另一皮肤的胶囊/边框。
- page header 不生成顶部双横线。
- list row、empty state、focus 状态在 light/dark 下均使用语义 token。
- props 与 emits 保持业务无关，次级页面不复制主题判断。

### Step 2：最小实现并逐页迁移

迁移顺序：Stats -> History -> Equalizer -> Settings。每迁移一页运行该页现有测试，保持功能与数据加载不变，只替换页面标题、按钮、列表行与空状态外观。

- Stats 数字与柱图继续使用已有 `animateCountUp` / `animateBarHeight`，进入可见区才触发，卸载清理。
- Aurora 次级页面使用清晰表面与适度圆角；Newsprint 使用排版块面、编号和字级，禁止复用 Aurora card 形状。
- 去除大面积空白与过宽单列，1280px 下仍保持可扫读密度。

### Step 3：测试与提交

```powershell
cd C:\BottleMusic\ui
pnpm test -- src/components/primitives/__tests__/SkinPrimitives.test.ts src/views/__tests__
pnpm exec vue-tsc --noEmit
```

```powershell
git add ui/src/components/primitives ui/src/views/StatsView.vue ui/src/views/HistoryView.vue ui/src/views/SettingsView.vue ui/src/views/EqualizerView.vue ui/src/style.css ui/src/styles/skins
git commit -m "refactor: apply skin primitives to secondary views"
```

---

## Task 10：四套视觉矩阵、性能验证与回归收口

**文件：**

- 修改：`ui/src/views/__tests__/AppNetworkBanner.test.ts`
- 修改：`ui/src/api/__tests__/motion.test.ts`
- 修改：`ui/src/api/__tests__/themeStore.test.ts`
- 修改：`docs/superpowers/specs/2026-07-10-dual-interface-player-redesign-design.md`
- 新建：`docs/superpowers/reports/2026-07-11-dual-interface-player-verification.md`

### Step 1：运行完整自动化回归

```powershell
cd C:\BottleMusic\ui
pnpm test
pnpm exec vue-tsc --noEmit
pnpm build
```

预期：Vitest 全绿、无 TypeScript 错误、Vite 构建成功。任何失败先按 `superpowers:systematic-debugging` 找根因，不通过放宽断言掩盖回归。

### Step 2：启动应用并执行视觉矩阵

```powershell
cd C:\BottleMusic\ui
pnpm dev -- --host 127.0.0.1
```

通过 Browser/Playwright 截图检查：

| 皮肤 | 模式 | 页面 | 尺寸 |
|---|---|---|---|
| Aurora | light/dark | 首页、普通歌词、全屏歌词 | 1280x720、1440x900、1920x1080 |
| Newsprint | light/dark | 首页、普通歌词、全屏歌词 | 1280x720、1440x900、1920x1080 |

每组再覆盖：正常数据、初始加载、刷新中、单 section 错误、长歌名、无封面、0/50/100% 进度。

截图验收必须同时满足：

- 无顶部装饰横线。
- 进度轨道、fill、thumb 和时间在四套组合中可见。
- 首页播放器不再大片空荡；Aurora 与 Newsprint 一眼可区分。
- 返回当前行按钮不覆盖歌词。
- 全屏只显示最小化和退出全屏。
- 1280px 下按钮不拥挤、不裁切、不覆盖。

### Step 3：验证首页网络与组件生命周期

在浏览器记录：首次进入首页 -> 统计 -> 返回首页 -> 歌词 -> 返回首页。

- 首次首页三组请求各 1 次。
- 两次返回首页新增首页请求为 0。
- HomeView mount 总数为 1。
- 数据与滚动位置立即恢复，无先空后填。
- 显式刷新只发起一轮并发请求；请求期间旧内容保持。

把请求计数、操作步骤和结果写入 verification report。

### Step 4：验证动效与性能

- Aurora：主舞台、播放按钮和 progress thumb 有低强度果冻反馈；正文、歌词和列表无弹性晃动。
- Newsprint：区块与列表以短距离、短 stagger 落版；稳定后无循环动画。
- 快速连续切换页面 10 次，同一元素不存在叠加 tween；离开视图后 tween 已 kill。
- `prefers-reduced-motion` 下立即到最终态。
- 切换窗口失焦/隐藏标签页时 Aurora ambient pause，恢复后仅在播放中继续。
- Performance 录制中页面切换和歌词跟随无持续长任务；不对大面积 filter/layout 属性逐帧动画。

### Step 5：检查本轮边界

```powershell
cd C:\BottleMusic
$base = git merge-base HEAD main
git diff --name-only "$base..HEAD"
git status --short
```

确认没有修改 `ui/src-tauri`、`native`、AudioProxy、EQ 数据逻辑或后端 API；没有提交构建产物、测试截图和 Mineradio 参考图片。

### Step 6：更新规格状态并提交验证记录

- 将设计规格状态改为“已实现并验证”。
- verification report 记录测试命令、通过数量、浏览器尺寸、四套截图结论、网络调用计数、已知非阻塞限制。

```powershell
git add -f docs/superpowers/specs/2026-07-10-dual-interface-player-redesign-design.md docs/superpowers/reports/2026-07-11-dual-interface-player-verification.md
git commit -m "docs: record dual-interface verification"
```

---

## 最终完成检查

执行者在宣布完成前必须使用 `superpowers:requesting-code-review` 与 `superpowers:verification-before-completion`，并逐项确认：

- [ ] 首页返回不重新请求、不重建、不先空后填，滚动位置保留。
- [ ] Aurora / Newsprint 的 Shell、Home、PlayerBar、LyricStage 均为独立模板。
- [ ] Aurora light、Aurora dark、Newsprint light、Newsprint dark 均完成视觉验收。
- [ ] 用户确认的第一套 Aurora 舞台方向已落地，但未复制 Mineradio 素材或源码。
- [ ] 两套底部播放器均更紧凑、内容完整，1280px 下无拥挤与遮挡。
- [ ] 深色模式进度条清晰，鼠标和键盘均可 seek。
- [ ] 顶部装饰横线全部移除。
- [ ] “回到当前行”位于底部保留操作区，不覆盖歌词。
- [ ] 全屏仅显示最小化和退出全屏，Esc 可退出。
- [ ] Aurora 果冻动效和 Newsprint 排版动效可感知且互不混用。
- [ ] reduced-motion、失焦、隐藏、快速导航均能正确清理动效。
- [ ] `pnpm test`、`pnpm exec vue-tsc --noEmit`、`pnpm build` 全部通过。
- [ ] 未修改后端、AudioProxy、EQ、统计数据库或登录协议。

## 执行建议

推荐使用 `superpowers:subagent-driven-development` 在当前任务中逐 Task 执行：每个 Task 由实现子代理完成，再进行规格审查与代码质量审查，验证通过后才进入下一 Task。Task 1-2 是后续视觉工作的稳定地基，不应与 Task 5-8 合并成一次大改。
