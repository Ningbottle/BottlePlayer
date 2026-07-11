# Aurora Immersive Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 Aurora 首页和底部播放器重建为深浅两套光感下的沉浸式三栏音乐舞台，同时保留现有播放控制器和 Newsprint 行为。

**Architecture:** 状态层继续由 `playerStore`、`HomeViewModel` 与 `PlayerController` 提供。Aurora 只在展示层增加最多 12 首的只读队列数据、重组首页 DOM、重建壳层网格和底部播放器；Newsprint、播放编排器和音频后端不改动。GSAP 仅通过现有 `motion.ts` 与 `motionProfiles.ts` 驱动进入、卡片和控制器反馈。

**Tech Stack:** Vue 3 Composition API、TypeScript、Vitest、Vue Test Utils、GSAP、Vite、Tauri。

## Global Constraints

- 只改 Aurora 专属展示层；不得重写 `playerStore`、播放编排器、音频后端或 Newsprint。
- 深色 Aurora 使用石墨黑与翡翠绿；浅色 Aurora 使用冷白、烟灰与翡翠绿，禁止引入米黄纸张风格。
- 首屏桌面目标为左侧导航、中央舞台、右侧常驻队列和全宽液态播放器；顶部不得出现装饰横线。
- Aurora 首页队列最多渲染 12 首，窄窗口与非首页继续使用现有 `QueuePanel` 抽屉。
- 首页的文字摘要只使用现有歌曲、歌手、专辑与推荐数据，并提供进入歌词页的入口；不得在首页复用或复制 `useLyricStage()` 的歌词网络请求。
- 所有播放、seek、音量、音质、歌词、收藏和切歌继续调用既有 `PlayerController` 命令。
- 默认动效使用 `expo.out`；果冻反馈只用于控制器和卡片，必须尊重 `prefers-reduced-motion`。
- 视觉主代理在实现和截图对照时使用 `product-design:image-to-code` 处理目标图，并使用前端浏览器调试流程验证真实渲染。
- 所有视觉文件由主代理串行修改。非视觉子智能体仅负责 `homeViewModel.ts` 的数据契约及其测试，使用 `gpt-5.6-terra` 与 `high` 推理。
- 每个任务先运行目标测试确认失败，再写最小实现；现状刻画测试若直接通过，记录为回归保护，不得称为 RED。

---

## File Structure

| 文件 | 职责 |
| --- | --- |
| `ui/src/views/home/homeViewModel.ts` | 为 Aurora 首页派生当前曲目、播放状态、最多 12 首队列、总数和当前曲目标记。 |
| `ui/src/views/home/AuroraHome.vue` | 渲染沉浸式舞台、歌词摘要、推荐带和右侧队列；只向上发射既有事件。 |
| `ui/src/components/shell/AuroraShell.vue` | 标记沉浸式 Aurora 应用壳，保留现有 slot 与 Tauri 标题栏语义。 |
| `ui/src/styles/tokens.css` | 定义 Aurora 深浅主题的冷色表面、翡翠强调和进度对比。 |
| `ui/src/styles/skins/aurora.css` | 定义 Aurora 三栏网格、无横线顶部、液态播放器和响应式断点。 |
| `ui/src/components/player/AuroraPlayerBar.vue` | 渲染中央隆起控制台，复用 `PlayerController` 和 `PlayerProgress`。 |
| `ui/src/api/motionProfiles.ts` | 定义 Aurora 进入、卡片与控制器回弹参数。 |
| `ui/src/**/__tests__/*.test.ts` | 固化数据、事件、语义、可访问性和动效 profile 契约。 |

## Task 1: Expose Aurora Queue-Rail State in the Home View Model

**Files:**
- Create: `ui/src/views/home/__tests__/homeViewModel.contract.test.ts`
- Modify: `ui/src/views/home/homeViewModel.ts`
- Modify: `ui/src/views/home/__tests__/AuroraHome.test.ts`
- Modify: `ui/src/views/home/__tests__/NewsprintHome.test.ts`

**Interfaces:**
- Consumes: `playerStore.currentTrack`, `playerStore.isPlaying`, `playerStore.queue`, `useHomeFeedStore()`。
- Produces: `HomeViewModel.queuePreview: readonly Track[]`（最多 12 项）、`queueTotal: number`、`activeQueueHash: string | null`、`isPlaying: boolean`。
- Invariant: 不改变 `playerStore`，不请求新数据，不改变 Newsprint 的渲染路径。

- [ ] **Step 1: Write the failing ViewModel contract test**

Create `ui/src/views/home/__tests__/homeViewModel.contract.test.ts` with reactive store doubles and this assertion:

```ts
it('exposes twelve queue rows, total count, active hash, and playback state', () => {
  playerStoreMock.currentTrack = makeTrack('hash-8');
  playerStoreMock.isPlaying = true;
  playerStoreMock.queue = Array.from({ length: 15 }, (_, index) => makeTrack(`hash-${index + 1}`));

  const model = useHomeViewModel().value;

  expect(model.queuePreview).toHaveLength(12);
  expect(model.queueTotal).toBe(15);
  expect(model.activeQueueHash).toBe('hash-8');
  expect(model.isPlaying).toBe(true);
});
```

Use `reactive()` for `playerStoreMock`, and make the `homeFeedMock` return loaded empty sections so this test only exercises queue derivation.

- [ ] **Step 2: Run the contract test and verify it fails**

Run from `ui/`:

```powershell
pnpm exec vitest run src/views/home/__tests__/homeViewModel.contract.test.ts
```

Expected: FAIL because `queuePreview` contains 5 items and `queueTotal`, `activeQueueHash`, and `isPlaying` do not exist.

- [ ] **Step 3: Add the minimal ViewModel fields**

In `homeViewModel.ts`, replace the preview count and extend the interface exactly as follows:

```ts
const QUEUE_PREVIEW_COUNT = 12;

export interface HomeViewModel {
  heroTrack: Track | null;
  dailyTracks: readonly Track[];
  playlists: readonly PlaylistInfo[];
  albums: readonly PlaylistInfo[];
  queuePreview: readonly Track[];
  queueTotal: number;
  activeQueueHash: string | null;
  isPlaying: boolean;
  isInitialLoading: boolean;
  isRefreshing: boolean;
  errors: readonly HomeSectionError[];
}
```

Add these fields to the computed return value:

```ts
queuePreview: playerStore.queue.slice(0, QUEUE_PREVIEW_COUNT),
queueTotal: playerStore.queue.length,
activeQueueHash: playerStore.currentTrack?.FileHash ?? null,
isPlaying: playerStore.isPlaying,
```

Add `queueTotal: 0`, `activeQueueHash: null`, and `isPlaying: false` to the `createViewModel()` defaults in both existing home test files.

- [ ] **Step 4: Run focused home tests and verify they pass**

```powershell
pnpm exec vitest run src/views/home/__tests__/homeViewModel.contract.test.ts src/views/home/__tests__/AuroraHome.test.ts src/views/home/__tests__/NewsprintHome.test.ts
```

Expected: PASS. The Newsprint suite proves the expanded interface did not break its typed fixture.

- [ ] **Step 5: Commit the data contract**

```powershell
git add ui/src/views/home/homeViewModel.ts ui/src/views/home/__tests__/homeViewModel.contract.test.ts ui/src/views/home/__tests__/AuroraHome.test.ts ui/src/views/home/__tests__/NewsprintHome.test.ts
git commit -m "feat: expose Aurora queue rail state"
```

## Task 2: Rebuild the Aurora Home Stage and Persistent Queue Rail

**Files:**
- Modify: `ui/src/views/home/AuroraHome.vue`
- Modify: `ui/src/views/home/__tests__/AuroraHome.test.ts`

**Interfaces:**
- Consumes: `HomeViewModel` fields from Task 1.
- Produces: `play-track`, `refresh`, and `navigate` emits with their current signatures; `data-test="aurora-stage"`, `data-test="queue-rail"`, and `data-test="queue-track-<FileHash>"` hooks.
- Invariant: Clicking a queue row or a daily recommendation emits the selected `Track`; this component never mutates `playerStore`.

- [ ] **Step 1: Write failing stage and queue tests**

Append these tests to `AuroraHome.test.ts`:

```ts
it('renders a labelled queue rail and marks the active queued track', () => {
  const queue = Array.from({ length: 12 }, (_, index) => createTrack({
    FileHash: `queue-${index + 1}`,
    SongName: `Queue ${index + 1}`,
  }));
  const wrapper = mount(AuroraHome, {
    props: { model: createViewModel({ queuePreview: queue, queueTotal: 15, activeQueueHash: 'queue-3' }) },
  });

  expect(wrapper.get('[data-test="queue-rail"]').attributes('aria-label')).toBe('播放队列');
  expect(wrapper.findAll('[data-test^="queue-track-"]')).toHaveLength(12);
  expect(wrapper.get('[data-test="queue-track-queue-3"]').attributes('aria-current')).toBe('true');
  expect(wrapper.text()).toContain('15');
});

it('emits play-track when a queue row or daily card is selected', async () => {
  const queued = createTrack({ FileHash: 'queue-play', SongName: 'Queue Play' });
  const daily = createTrack({ FileHash: 'daily-play', SongName: 'Daily Play' });
  const wrapper = mount(AuroraHome, {
    props: { model: createViewModel({ queuePreview: [queued], dailyTracks: [daily] }) },
  });

  await wrapper.get('[data-test="queue-track-queue-play"]').trigger('click');
  await wrapper.get('[data-test="daily-track-daily-play"]').trigger('click');
  expect(wrapper.emitted('play-track')).toEqual([[queued], [daily]]);
});
```

- [ ] **Step 2: Run the AuroraHome suite and verify it fails**

```powershell
pnpm exec vitest run src/views/home/__tests__/AuroraHome.test.ts
```

Expected: FAIL because the queue rail lacks accessible hooks and daily recommendations are not track buttons.

- [ ] **Step 3: Replace the stage DOM while preserving emits**

Keep `onHeroPlay()` and add:

```ts
function onTrackPlay(track: Track): void {
  emit('play-track', track);
}

function isActiveQueueTrack(track: Track): boolean {
  return track.FileHash === props.model.activeQueueHash;
}
```

Use this queue rail structure inside the desktop stage:

```vue
<aside class="aurora-queue-rail" data-test="queue-rail" aria-label="播放队列">
  <header class="aurora-queue-rail-head">
    <h3>播放队列 <span>{{ model.queueTotal }}</span></h3>
    <button type="button" class="aurora-queue-clear" disabled aria-label="清空播放队列">清空</button>
  </header>
  <ol v-if="model.queuePreview.length" class="aurora-queue-list">
    <li v-for="(track, index) in model.queuePreview" :key="track.FileHash" class="aurora-queue-row">
      <button
        type="button"
        :data-test="`queue-track-${track.FileHash}`"
        :class="{ 'is-active': isActiveQueueTrack(track) }"
        :aria-current="isActiveQueueTrack(track) ? 'true' : undefined"
        @click="onTrackPlay(track)"
      >
        <span class="aurora-queue-index">{{ String(index + 1).padStart(2, '0') }}</span>
        <span class="aurora-queue-copy"><b>{{ track.SongName }}</b><small>{{ track.SingerName }}</small></span>
        <span class="aurora-queue-duration">{{ formatDuration(track.Duration) }}</span>
      </button>
    </li>
  </ol>
</aside>
```

Render the song information block from existing `SongName`, `SingerName`, and `AlbumName`, then add a labelled `查看歌词` button that emits `navigate('lyric')`. The visual copy block must not call `useLyricStage()` or any lyric API. Render the recommendation strip from `model.dailyTracks.slice(0, 6)`. Each item must be a `<button type="button">` with `:data-test="`daily-track-${track.FileHash}`"` and `@click="onTrackPlay(track)"`. Keep playlists and albums below the fold with their current `navigate` behavior.

- [ ] **Step 4: Add layout hooks without introducing fetches**

Structure the root as `aurora-home`, then `aurora-stage` and `aurora-recommendations`. Keep cover failure state local. Do not add `onMounted()` data fetches, `watch()` network calls, or `v-if` conditions that discard loaded content during `isRefreshing`.

- [ ] **Step 5: Run focused tests and verify they pass**

```powershell
pnpm exec vitest run src/views/home/__tests__/AuroraHome.test.ts
```

Expected: PASS, including the pre-existing hero, refresh, playlist navigation, loading, and error assertions.

- [ ] **Step 6: Commit the home-stage behavior**

```powershell
git add ui/src/views/home/AuroraHome.vue ui/src/views/home/__tests__/AuroraHome.test.ts
git commit -m "feat: rebuild Aurora home stage"
```

## Task 3: Establish Aurora Shell Geometry and Dual-Light Tokens

**Files:**
- Modify: `ui/src/components/shell/AuroraShell.vue`
- Modify: `ui/src/components/shell/__tests__/Shells.test.ts`
- Modify: `ui/src/styles/tokens.css`
- Modify: `ui/src/styles/skins/aurora.css`

**Interfaces:**
- Consumes: existing `lyricFullscreen` prop and all existing shell slots.
- Produces: `data-layout="immersive"` on the Aurora app root; unchanged landmarks and slots.
- Invariant: Newsprint selectors and tokens remain byte-for-byte outside Aurora blocks.

- [ ] **Step 1: Write a failing shell identity test**

Append this test in the `AuroraShell` suite:

```ts
it('identifies the modern immersive Aurora layout without changing landmarks', () => {
  const wrapper = mount(AuroraShell);
  const root = wrapper.get('[data-shell="aurora"]');

  expect(root.attributes('data-layout')).toBe('immersive');
  expect(wrapper.get('.titlebar-logo').text()).toContain('BottleMusic');
  expect(wrapper.get('.titlebar-logo').text()).toContain('Aurora');
  expect(wrapper.find('nav.shell-sidebar').exists()).toBe(true);
  expect(wrapper.find('main.shell-main').exists()).toBe(true);
  expect(wrapper.find('footer.shell-playerbar').exists()).toBe(true);
});
```

- [ ] **Step 2: Run the shell test and verify it fails**

```powershell
pnpm exec vitest run src/components/shell/__tests__/Shells.test.ts
```

Expected: FAIL because `data-layout` is absent.

- [ ] **Step 3: Add the layout marker and preserve shell semantics**

Change the Aurora root opening tag to:

```vue
<div
  class="app aurora-app"
  data-shell="aurora"
  data-layout="immersive"
  :class="{ 'lyric-fullscreen-active': lyricFullscreen }"
>
```

Replace the titlebar logo body with the Aurora wordmark while retaining the existing drag-region parent:

```vue
<div class="titlebar-logo" aria-label="BottleMusic Aurora">
  <span class="aurora-wordmark">BottleMusic</span>
  <span class="aurora-wordmark-subtitle">Aurora</span>
</div>
```

Do not rename `shell-sidebar`, `shell-main`, `shell-content`, or `shell-playerbar`; the App, shell tests, and keyboard landmarks depend on them.

- [ ] **Step 4: Replace only Aurora token blocks**

In `tokens.css`, set Aurora light and dark semantic variables to these exact starting values:

```css
:root[data-skin='aurora'][data-mode='light'] {
  --app-bg: #eef3f0;
  --surface-1: #f7faf8;
  --surface-2: #e7eeea;
  --surface-elevated: #ffffff;
  --text-primary: #16201d;
  --text-secondary: #5f6d68;
  --text-muted: #8b9792;
  --accent: #18875b;
  --focus-ring: rgba(24, 135, 91, 0.36);
  --progress-track: #a9b6af;
  --progress-fill: #18875b;
  --progress-thumb-ring: #18875b;
}

:root[data-skin='aurora'][data-mode='dark'] {
  --app-bg: #070b0c;
  --surface-1: #0d1213;
  --surface-2: #141a1b;
  --surface-elevated: #1a2222;
  --text-primary: #f2f5f2;
  --text-secondary: #929c98;
  --text-muted: #626d69;
  --accent: #62d6a2;
  --focus-ring: rgba(98, 214, 162, 0.42);
  --progress-track: #394541;
  --progress-fill: #62d6a2;
  --progress-thumb-ring: #62d6a2;
}
```

Keep every Newsprint block unchanged. Preserve the existing `--progress-buffered`, `--progress-thumb-fill`, and `--progress-time` declarations with equivalent contrast values.

- [ ] **Step 5: Rebuild Aurora-only CSS geometry**

In `aurora.css`, use the existing semantic class names and establish these constraints:

```css
.app[data-shell='aurora'] {
  grid-template-columns: clamp(232px, 16vw, 252px) minmax(0, 1fr);
  grid-template-rows: 32px auto minmax(0, 1fr) 128px;
  background: var(--app-bg);
}

[data-shell='aurora'] .titlebar,
[data-shell='aurora'] .shell-topbar {
  border: 0;
  background: transparent;
}

[data-shell='aurora'] .shell-playerbar {
  padding: 0 18px 14px;
  background: transparent;
}

@media (max-width: 1099px) {
  .app[data-shell='aurora'] { grid-template-columns: minmax(208px, 22vw) minmax(0, 1fr); }
}
```

The final CSS must also define an Aurora-only `@media (max-width: 899px)` block that collapses the sidebar and removes the desktop rail from the home grid. Do not use a horizontal border as a substitute for spacing.

- [ ] **Step 6: Run shell tests and a light/dark token smoke check**

```powershell
pnpm exec vitest run src/components/shell/__tests__/Shells.test.ts src/api/__tests__/themeStore.test.ts
```

Expected: PASS.

- [ ] **Step 7: Commit shell and token work**

```powershell
git add ui/src/components/shell/AuroraShell.vue ui/src/components/shell/__tests__/Shells.test.ts ui/src/styles/tokens.css ui/src/styles/skins/aurora.css
git commit -m "feat: establish immersive Aurora shell"
```

## Task 4: Build the Liquid Aurora Player Console

**Files:**
- Modify: `ui/src/components/player/AuroraPlayerBar.vue`
- Modify: `ui/src/components/player/__tests__/AuroraPlayerBar.test.ts`
- Modify: `ui/src/styles/skins/aurora.css`

**Interfaces:**
- Consumes: unchanged `PlayerController` prop and `toggle-queue` emit.
- Produces: `data-test="aurora-player-console"` and `data-test="aurora-player-progress"`; all existing aria labels remain stable.
- Invariant: `togglePlay`, `prev`, `next`, `seek`, `setVolume`, quality menu, lyric toggle, favorite and queue emit keep their existing invocation paths.

- [ ] **Step 1: Write failing player-console tests**

Append this to `AuroraPlayerBar.test.ts`:

```ts
it('keeps transport and progress inside the liquid player console', () => {
  const wrapper = mount(AuroraPlayerBar, {
    props: { controller: createStubController({ currentTrack: mkTrack(), duration: 180 }) },
  });

  const console = wrapper.get('[data-test="aurora-player-console"]');
  expect(console.find('.aurora-pb-transport').exists()).toBe(true);
  expect(console.get('[data-test="aurora-player-progress"]').find('.progress-root').exists()).toBe(true);
  expect(console.get('[aria-label="play"]').exists()).toBe(true);
});

it('keeps labelled queue, lyric, and volume controls outside the main play button', () => {
  const wrapper = mount(AuroraPlayerBar, { props: { controller: createStubController({ currentTrack: mkTrack() }) } });
  expect(wrapper.get('[aria-label="queue"]').exists()).toBe(true);
  expect(wrapper.get('[aria-label="lyric"]').exists()).toBe(true);
  expect(wrapper.find('.aurora-pb-volume').exists()).toBe(true);
});
```

- [ ] **Step 2: Run the player test and verify it fails**

```powershell
pnpm exec vitest run src/components/player/__tests__/AuroraPlayerBar.test.ts
```

Expected: FAIL because the console and progress hooks do not exist.

- [ ] **Step 3: Introduce structural wrappers without altering controller calls**

Wrap the transport and `PlayerProgress` in this structure:

```vue
<div class="aurora-pb-center" data-test="aurora-player-console">
  <div class="aurora-pb-transport" role="group" aria-label="播放控制">
    <!-- retain the existing shuffle, previous, play, next, repeat buttons unchanged -->
  </div>
  <div class="aurora-pb-progress-wrap" data-test="aurora-player-progress">
    <PlayerProgress :current-time="c.currentTime" :duration="c.duration" @seek="c.seek" />
  </div>
</div>
```

Give the lyric button an explicit `title="歌词"` and retain `aria-label="lyric"`. Do not hide the queue button in DOM; CSS may visually de-emphasize it when the desktop queue rail is visible.

- [ ] **Step 4: Replace rectangle styling with layered liquid surfaces**

Use CSS surface layers rather than a large custom SVG:

```css
[data-shell='aurora'] .aurora-pb {
  min-height: 114px;
  grid-template-columns: minmax(230px, 0.8fr) minmax(420px, 1.45fr) minmax(250px, 0.8fr);
  border: 1px solid color-mix(in srgb, var(--text-primary) 9%, transparent);
  border-radius: 34px 34px 28px 28px;
  background: color-mix(in srgb, var(--surface-elevated) 86%, transparent);
  box-shadow: 0 20px 46px rgba(0, 0, 0, 0.26), inset 0 1px rgba(255, 255, 255, 0.08);
}

[data-shell='aurora'] .aurora-pb-center {
  align-self: stretch;
  margin: -20px 0 0;
  padding: 14px 28px 10px;
  border-radius: 46% 46% 30px 30px;
  background: color-mix(in srgb, var(--surface-2) 74%, transparent);
}
```

Define a fallback `background: var(--surface-elevated)` before each `color-mix()` declaration for WebView compatibility. Keep `PlayerProgress` interactive and ensure the visual progress track does not become thinner than its pointer hit target.

- [ ] **Step 5: Run focused tests and verify they pass**

```powershell
pnpm exec vitest run src/components/player/__tests__/AuroraPlayerBar.test.ts
```

Expected: PASS, including existing controller-call tests.

- [ ] **Step 6: Commit the liquid player console**

```powershell
git add ui/src/components/player/AuroraPlayerBar.vue ui/src/components/player/__tests__/AuroraPlayerBar.test.ts ui/src/styles/skins/aurora.css
git commit -m "feat: build Aurora liquid player console"
```

## Task 5: Upgrade Aurora Motion and Respect Reduced Motion

**Files:**
- Modify: `ui/src/api/motionProfiles.ts`
- Modify: `ui/src/api/__tests__/motionProfiles.test.ts`
- Modify: `ui/src/views/home/AuroraHome.vue`
- Modify: `ui/src/views/home/__tests__/AuroraHome.test.ts`
- Modify: `ui/src/components/player/AuroraPlayerBar.vue`

**Interfaces:**
- Consumes: `animateElement`, `animateStagger`, `startAmbientMotion`, `isReducedMotion`, and the motion profile keys already exposed by `motion.ts`.
- Produces: cancellable stage, recommendation, and control feedback motion; no timer or tween survives component unmount.
- Invariant: Newsprint's profile values remain untouched and reduced-motion never starts ambient or stagger animation.

- [ ] **Step 1: Write failing motion-profile tests**

Append these tests to `motionProfiles.test.ts`:

```ts
it('uses a longer expo entrance and a bounded jelly card entrance for Aurora', () => {
  const profile = getMotionProfile('aurora');
  expect(profile.pageEnter).toMatchObject({ duration: 0.42, ease: 'expo.out' });
  expect(profile.cardEnter).toMatchObject({ duration: 0.36, stagger: 0.04, maxItems: 12 });
  expect(profile.cardEnter.ease).toContain('back.out');
});

it('keeps Aurora control release elastic without changing Newsprint', () => {
  expect(getMotionProfile('aurora').controlRelease).toMatchObject({ duration: 0.42 });
  expect(getMotionProfile('aurora').controlRelease.ease).toContain('elastic.out');
  expect(getMotionProfile('newsprint').controlRelease.ease).toBe('power2.out');
});
```

- [ ] **Step 2: Run the profile suite and verify it fails**

```powershell
pnpm exec vitest run src/api/__tests__/motionProfiles.test.ts
```

Expected: FAIL because Aurora page and card durations are currently shorter and `maxItems` is 20.

- [ ] **Step 3: Set the bounded Aurora profile values**

Update only `auroraProfile`:

```ts
const auroraProfile: MotionProfile = {
  pageEnter: { duration: 0.42, ease: 'expo.out' },
  pageLeave: { duration: 0.24, ease: 'power2.in' },
  controlPress: { duration: 0.1, ease: 'power2.out' },
  controlRelease: { duration: 0.42, ease: 'elastic.out(1, 0.55)' },
  cardEnter: { duration: 0.36, ease: 'back.out(1.25)', stagger: 0.04, maxItems: 12 },
  ambient: { enabled: true, duration: 3, scale: 1.01 },
};
```

- [ ] **Step 4: Bind motion handles in AuroraHome**

Add `ref`, `onMounted`, and `onUnmounted` imports. Keep handles local and always kill them:

```ts
const stageEl = ref<HTMLElement | null>(null);
const recommendationEls = ref<HTMLElement[]>([]);
const motionHandles: Array<{ kill(): void }> = [];

function setRecommendationRef(el: Element | null): void {
  if (el instanceof HTMLElement) recommendationEls.value.push(el);
}

onMounted(() => {
  if (stageEl.value) motionHandles.push(animateElement(stageEl.value, { opacity: 0, y: 20 }, { opacity: 1, y: 0 }, 'pageEnter'));
  motionHandles.push(animateStagger(recommendationEls.value, 'cardEnter'));
  if (stageEl.value) motionHandles.push(startAmbientMotion(stageEl.value, () => props.model.isPlaying));
});

onUnmounted(() => motionHandles.splice(0).forEach((handle) => handle.kill()));
```

Reset `recommendationEls.value = []` before Vue re-renders the recommendation list. In `AuroraPlayerBar.vue`, replace direct inline transform mutation with `animateElement(..., 'controlPress')` and `animateElement(..., 'controlRelease')`; do not animate progress dragging.

- [ ] **Step 5: Add and run reduced-motion component assertions**

In `AuroraHome.test.ts`, mock `isReducedMotion` as `true` and assert that mounting still renders stage, queue rail, and all controls without requiring animation completion. Then run:

```powershell
pnpm exec vitest run src/api/__tests__/motionProfiles.test.ts src/views/home/__tests__/AuroraHome.test.ts src/components/player/__tests__/AuroraPlayerBar.test.ts
```

Expected: PASS. Existing `motion.test.ts` remains the authority for visibility and reduced-motion ambient cancellation.

- [ ] **Step 6: Commit the motion upgrade**

```powershell
git add ui/src/api/motionProfiles.ts ui/src/api/__tests__/motionProfiles.test.ts ui/src/views/home/AuroraHome.vue ui/src/views/home/__tests__/AuroraHome.test.ts ui/src/components/player/AuroraPlayerBar.vue
git commit -m "feat: add Aurora jelly motion"
```

## Task 6: Responsive, Accessibility, and Visual Verification

**Files:**
- Modify: `ui/src/styles/skins/aurora.css`
- Modify: `ui/src/views/home/AuroraHome.vue`
- Modify: `ui/src/views/home/__tests__/AuroraHome.test.ts`
- Modify: `ui/src/components/player/__tests__/AuroraPlayerBar.test.ts`

**Interfaces:**
- Consumes: DOM hooks and aria labels produced by Tasks 2 and 4.
- Produces: desktop queue rail visibility rules, keyboard reachable queue and player controls, no horizontal overflow at narrow layouts.
- Invariant: desktop uses the right rail at 1360px and above; below 1100px it is hidden from layout and the existing queue drawer remains reachable.

- [ ] **Step 1: Write failing accessibility and rail-visibility tests**

Add these assertions to `AuroraHome.test.ts`:

```ts
it('uses buttons for every interactive daily and queue item', () => {
  const track = createTrack({ FileHash: 'interactive-track' });
  const wrapper = mount(AuroraHome, {
    props: { model: createViewModel({ dailyTracks: [track], queuePreview: [track] }) },
  });

  expect(wrapper.get('[data-test="daily-track-interactive-track"]').element.tagName).toBe('BUTTON');
  expect(wrapper.get('[data-test="queue-track-interactive-track"]').element.tagName).toBe('BUTTON');
});
```

Add this assertion to `AuroraPlayerBar.test.ts`:

```ts
it('keeps the primary player commands named for assistive technology', () => {
  const wrapper = mount(AuroraPlayerBar, { props: { controller: createStubController({ currentTrack: mkTrack() }) } });
  for (const label of ['shuffle', 'prev', 'play', 'next', 'repeat', 'queue', 'lyric']) {
    expect(wrapper.find(`[aria-label="${label}"]`).exists()).toBe(true);
  }
});
```

- [ ] **Step 2: Run tests and verify the new assertions fail before markup changes**

```powershell
pnpm exec vitest run src/views/home/__tests__/AuroraHome.test.ts src/components/player/__tests__/AuroraPlayerBar.test.ts
```

Expected: FAIL until Task 2's recommendation and queue items are buttons. If the player assertion passes, record it as pre-existing accessibility coverage.

- [ ] **Step 3: Implement responsive CSS constraints**

Add these concrete rules to the Aurora block in `aurora.css`:

```css
[data-shell='aurora'] .aurora-stage {
  display: grid;
  grid-template-columns: minmax(250px, 0.86fr) minmax(320px, 1.12fr) minmax(280px, 0.82fr);
  gap: clamp(20px, 2vw, 32px);
}

@media (max-width: 1099px) {
  [data-shell='aurora'] .aurora-stage { grid-template-columns: minmax(220px, 0.8fr) minmax(0, 1.2fr); }
  [data-shell='aurora'] .aurora-queue-rail { display: none; }
  [data-shell='aurora'] .aurora-recommendation-grid { grid-auto-flow: column; grid-auto-columns: minmax(132px, 1fr); overflow-x: auto; }
}

@media (max-width: 899px) {
  [data-shell='aurora'] .aurora-stage { grid-template-columns: 1fr; }
  [data-shell='aurora'] .aurora-cover { width: min(72vw, 320px); margin-inline: auto; }
  [data-shell='aurora'] .aurora-pb { grid-template-columns: 1fr; min-height: 0; }
  [data-shell='aurora'] .aurora-pb-right { justify-content: space-between; }
}
```

Ensure all cards, time labels, player metadata, and queue text use `min-width: 0`, ellipsis, or wrapping rules appropriate to their role. Do not use `overflow: hidden` on a parent to conceal required controls.

- [ ] **Step 4: Run the focused test suites and static checks**

```powershell
pnpm exec vitest run src/views/home/__tests__/AuroraHome.test.ts src/components/player/__tests__/AuroraPlayerBar.test.ts src/components/shell/__tests__/Shells.test.ts
pnpm exec vue-tsc --noEmit
pnpm build
```

Expected: every command exits 0.

- [ ] **Step 5: Run visual QA at the required viewports**

Start the existing desktop development workflow and capture Aurora Home in these states:

1. 1586 × 1024, dark Aurora, active queue item and playing track.
2. 1586 × 1024, light Aurora.
3. 1440 × 900, dark Aurora.
4. 1280 × 720, dark Aurora.
5. 900px-wide layout, queue rail absent and queue drawer reachable.
6. Reduced-motion enabled, no stage drift, card stagger, or control spring.

Compare each desktop capture against `C:\Users\w1521\AppData\Local\Temp\codex-clipboard-b5a6407f-638b-4e98-bf62-b20e7a8b3f3e.png`. Record the following before handoff: top divider absent, three-column proportions, cover size, song title hierarchy, queue density, liquid player silhouette, progress contrast, dark/light distinction, and no clipped controls.

- [ ] **Step 6: Commit verification-ready responsive work**

```powershell
git add ui/src/styles/skins/aurora.css ui/src/views/home/AuroraHome.vue ui/src/views/home/__tests__/AuroraHome.test.ts ui/src/components/player/__tests__/AuroraPlayerBar.test.ts
git commit -m "fix: refine Aurora responsive layout"
```

## Task 7: Full Regression Gate and Design QA Ledger

**Files:**
- Create: `ui/design-qa.md`
- Modify: no production files unless the visual comparison finds a P0, P1, or P2 mismatch.

**Interfaces:**
- Consumes: completed Aurora implementation, target screenshot, all test suites, and browser screenshots.
- Produces: a concise fidelity ledger with `final result: passed` only after every P0–P2 mismatch is fixed.

- [ ] **Step 1: Run the complete frontend suite**

```powershell
pnpm test
pnpm exec vue-tsc --noEmit
pnpm build
```

Expected: all three commands exit 0. Do not claim success from a partial suite.

- [ ] **Step 2: Write the fidelity ledger**

Create `ui/design-qa.md` after viewing both target and rendered screenshots. The first three lines must name the target image path, the actual rendered screenshot file path, and the viewport. Add a four-column table named `Check`, `Target evidence`, `Render evidence`, and `Result` with rows for: three-column desktop composition, top divider removal, liquid player silhouette, dark/light distinction, progress contrast, and motion reduction.

Every `Render evidence` cell must state the actual viewport and visual observation, for example: `1586 × 1024 dark: queue rail shows 12 rows; titlebar has no horizontal border.` Do not use angle-bracket markers, generic confirmations, or unchecked rows. Set `final result: passed` only when every row passes. If an item fails at P0, P1, or P2 severity, set `final result: blocked`, fix it in the owning task, recapture, and rewrite the ledger.

- [ ] **Step 3: Commit the QA ledger**

```powershell
git add -f ui/design-qa.md
git commit -m "docs: record Aurora design QA"
```

## Execution Order and Ownership

1. Main visual agent: Tasks 2–7, plus review of Task 1 integration.
2. Nonvisual subagent (`gpt-5.6-terra`, `high`): Task 1 data contract and its tests only. Required skills: `superpowers:using-superpowers`, `vue-application-structure`, `superpowers:test-driven-development`.
3. Another bug-fix agent: retains exclusive ownership of `ui/src/api/playerStore.ts`, playback orchestrator files, audio backends, lyric race fixes, and related tests.
4. Run Task 1 before Task 2. Run Tasks 3 and 4 after Task 1; Task 5 after Tasks 2 and 4; Task 6 after Tasks 2–5; Task 7 last.
