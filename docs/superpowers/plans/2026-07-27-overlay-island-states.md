# 浮层双态 + 歌词条设置 + 飞行变形 + 频谱环移除 实施计划

> **For agentic workers:** 每个任务独立完成、独立验证、独立提交。步骤用 checkbox 跟踪。Spec:`docs/superpowers/specs/2026-07-27-overlay-island-states-design.md`。

**Goal:** 灵动岛折叠↔展开双态(首次顶部中央、可拖动)、歌词条设置面板+实色底+拖宽、飞行幽灵方变圆、频谱环移除。

**Architecture:** 纯前端层。浮层窗口框架(`overlayWindows.ts`/`playerSync.ts`)与两个浮层页已有;本计划在其上迭代,不动数据层/播放链路/Newsprint 结构。

**Tech Stack:** Vue 3 `<script setup>`、GSAP 3.15 + Flip、Tauri v2 JS API(`getCurrentWindow`/`WebviewWindow`/`LogicalSize`)、Vitest + @vue/test-utils。

## Global Constraints

- 尊重 `prefers-reduced-motion`:尺寸切换直接生效无动画;飞行变形整体跳过。
- 持久化键:`overlay_island_pos`(已有)、`overlay_lyric_prefs`、`overlay_lyric_size`。
- 浮层窗口不初始化播放器;传输命令一律 `sendPlayerCommand`。
- 不新增依赖、不新增 Tauri 权限(创建/定位/拖拽已齐)。
- 工作目录 `C:/BottleMusic/ui`;测试 `pnpm test`;类型 `pnpm vue-tsc --noEmit`。

## 测试基线

- `IslandView.test.ts`、`DesktopLyricView.test.ts` 已有 mock 结构(playerSync/overlayWindows/motion/window),新增用例沿用。
- `overlayWindows.test.ts` 已 mock `WebviewWindow`/`getCurrentWindow`/`dpi`,断言位置逻辑在此扩展。
- 歌词页测试(LyricStages/LyricView)挂载 `AuroraLyricStage`——移除 SpectrumRing 后不应有任何 `spectrum-ring` 断言(执行时 grep 确认)。

---

### Task 1: 频谱环移除

**Files:**
- Delete: `ui/src/views/lyric/SpectrumRing.vue`
- Modify: `ui/src/views/lyric/AuroraLyricStage.vue`

**Interfaces:** 无产出。歌词页保留:黑胶(`.lyric-vinyl-disc`)+ 沟槽 + spindle + 封面 wash。

- [ ] **Step 1: 解除挂载**

`AuroraLyricStage.vue` 删除 import 行 `import SpectrumRing from './SpectrumRing.vue';` 与模板行 `<SpectrumRing :is-playing="model.isPlaying" />`。

- [ ] **Step 2: 删除组件 + 确认无残留引用**

```bash
rm src/views/lyric/SpectrumRing.vue
grep -rn "SpectrumRing\|spectrum-ring" src/ || echo "clean"
```

Expected: `clean`(除可能的 CHANGELOG/文档文本外无代码引用)。

- [ ] **Step 3: 跑歌词相关测试**

Run: `pnpm vitest run src/views/lyric/__tests__ src/views/__tests__/LyricView.test.ts`
Expected: PASS(现有 68+ 用例不受影响)。

- [ ] **Step 4: Commit**

```bash
git add -A && git commit -m "refactor(lyric): remove spectrum ring overlay from stage"
```

---

### Task 2: 飞行幽灵方形→圆形变形

**Files:**
- Modify: `ui/src/api/coverFlight.ts`
- Test: `ui/src/api/__tests__/coverFlight.test.ts`(新建)

**Interfaces:**
- Produces:`flyCoverToElement` / `flyCoverToDock` 的 Flip vars 新增 `borderRadius: '50%'`(对调用方透明)。

- [ ] **Step 1: coverFlight 加变形**

`flyCoverToElement` 中 `Flip.fit(ghost, target, { ... })` 的 vars 改为:

```ts
    Flip.fit(ghost, target, {
      duration: 0.55,
      ease: 'expo.inOut',
      absolute: true,
      opacity: 0.9,
      borderRadius: '50%',
      onComplete: () => ghost.remove(),
    });
```

(初始 `borderRadius: '10px'` 不变——由 10px 渐变到 50%,与位移同步。)

- [ ] **Step 2: 新建单测**

`ui/src/api/__tests__/coverFlight.test.ts`:

```ts
import { describe, it, expect, vi, beforeEach } from 'vitest';

const fitMock = vi.hoisted(() => vi.fn((_el: unknown, _target: unknown, vars: unknown) => vars));

vi.mock('gsap', () => ({
  gsap: { registerPlugin: vi.fn() },
}));
vi.mock('gsap/Flip', () => ({
  Flip: { fit: fitMock },
}));
vi.mock('../motion', () => ({
  isReducedMotion: vi.fn(() => false),
}));

import { flyCoverToElement, flyCoverToDock } from '../coverFlight';

describe('coverFlight', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    document.body.innerHTML = '<div class="aurora-pb-cover"></div>';
  });

  it('morphs the ghost from square to round while flying', () => {
    const from = document.createElement('div');
    document.body.appendChild(from);

    flyCoverToElement(from, '.aurora-pb-cover', 'http://img.example/c.jpg');

    expect(fitMock).toHaveBeenCalledTimes(1);
    const vars = fitMock.mock.calls[0][2] as Record<string, unknown>;
    expect(vars.borderRadius).toBe('50%');
    expect(vars.duration).toBe(0.55);
  });

  it('flyCoverToDock targets the dock cover', () => {
    const from = document.createElement('div');
    document.body.appendChild(from);

    flyCoverToDock(from, 'http://img.example/c.jpg');

    const target = fitMock.mock.calls[0][1] as HTMLElement;
    expect(target.className).toBe('aurora-pb-cover');
  });

  it('skips entirely without an image url', () => {
    const from = document.createElement('div');
    document.body.appendChild(from);

    flyCoverToDock(from, '');
    expect(fitMock).not.toHaveBeenCalled();
  });
});
```

- [ ] **Step 3: 跑测试**

Run: `pnpm vitest run src/api/__tests__/coverFlight.test.ts`
Expected: PASS 3 例。

- [ ] **Step 4: Commit**

```bash
git add ui/src/api/coverFlight.ts ui/src/api/__tests__/coverFlight.test.ts
git commit -m "feat(motion): cover flight morphs from square to round on landing"
```

---

### Task 3: 灵动岛拖动修复 + 首次顶部中央

**Files:**
- Modify: `ui/src/views/overlay/IslandView.vue`
- Modify: `ui/src/api/overlayWindows.ts`
- Test: `ui/src/api/__tests__/overlayWindows.test.ts`

**Interfaces:**
- Consumes:`anchorPosition`(已有,纯函数)。
- Produces:`resolveCreatePos(spec: {width:number;height:number}, screen: {w:number;h:number}): OverlayPos`——无记忆位置时的 top-center 坐标;浮层页拖拽区上移至胶囊主体(测试不变,行为在实机验证)。

- [ ] **Step 1: overlayWindows 加 resolveCreatePos 并用于创建**

`overlayWindows.ts` 新增导出:

```ts
/** First-run position: top-center of the screen (iOS island style). */
export function resolveCreatePos(
  spec: { width: number; height: number },
  screen: { w: number; h: number },
): OverlayPos {
  return anchorPosition('top-center', { w: spec.width, h: spec.height }, screen);
}
```

`toggleOverlay` 创建分支改为:

```ts
  const pos = loadOverlayPos(kind);
  const createPos = pos ?? resolveCreatePos(
    { width: spec.width, height: spec.height },
    { w: window.screen.width, h: window.screen.height },
  );
  const win = new WebviewWindow(spec.label, {
    url: spec.url,
    title: 'BottleMusic',
    width: spec.width,
    height: spec.height,
    decorations: false,
    transparent: true,
    alwaysOnTop: true,
    skipTaskbar: true,
    resizable: kind === 'lyric',
    minWidth: kind === 'lyric' ? 480 : undefined,
    maxWidth: kind === 'lyric' ? 1200 : undefined,
    shadow: false,
    x: createPos.x,
    y: createPos.y,
  });
```

(同时把 Task 5 需要的歌词条 resizable/min/max 一并落地,避免二次改同一对象。)

- [ ] **Step 2: IslandView 拖拽区上移**

模板根节点去掉 `data-tauri-drag-region`,加到胶囊:

```html
  <div class="island-root" @contextmenu="toggleAnchors">
    <div class="island-capsule" :class="{ 'is-idle': !hasTrack }" data-tauri-drag-region>
```

- [ ] **Step 3: overlayWindows.test 补断言**

在文件内 `describe('overlay position persistence')` 后追加:

```ts
describe('resolveCreatePos', () => {
  it('places a first-run island at the top-center of the screen', async () => {
    const { resolveCreatePos } = await import('../overlayWindows');
    const pos = resolveCreatePos({ width: 340, height: 88 }, { w: 1920, h: 1080 });
    expect(pos).toEqual({ x: Math.round((1920 - 340) / 2), y: 16 });
  });

  it('places a first-run lyric bar top-center too', async () => {
    const { resolveCreatePos } = await import('../overlayWindows');
    const pos = resolveCreatePos({ width: 720, height: 96 }, { w: 1920, h: 1080 });
    expect(pos).toEqual({ x: Math.round((1920 - 720) / 2), y: 16 });
  });
});
```

- [ ] **Step 4: 跑测试**

Run: `pnpm vitest run src/api/__tests__/overlayWindows.test.ts`
Expected: PASS(含既有 9 例)。

- [ ] **Step 5: Commit**

```bash
git add ui/src/api/overlayWindows.ts ui/src/api/__tests__/overlayWindows.test.ts ui/src/views/overlay/IslandView.vue
git commit -m "fix(overlay): grabbable drag region, first-run top-center, lyric bar resizable"
```

---

### Task 4: 灵动岛折叠↔展开双态

**Files:**
- Modify: `ui/src/views/overlay/IslandView.vue`
- Modify: `ui/src/api/__tests__/`(无)
- Test: `ui/src/views/__tests__/IslandView.test.ts`

**Interfaces:**
- Consumes:`PlayerProgress`(可点 seek)、`startVinylSpin`(大封面)、`sendPlayerCommand('seek'|'volume')`、`getCurrentWindow().setSize/outerPosition/outerSize/setPosition`、`LogicalSize`。
- Produces:折叠 340×88 ↔ 展开 480×200;`expanded` 状态不持久化(每次打开默认折叠)。

**模板结构(展开态,插入在 `.island-capsule` 之后):**

```html
    <div
      v-if="expanded"
      class="island-card"
      data-test="island-card"
      data-tauri-drag-region
      @click.self="toggleExpanded"
    >
      <div class="island-card-cover">
        <div ref="cardDiscEl" class="island-disc island-card-disc">
          <img v-if="state?.cover" :src="state.cover" alt="封面" />
          <div v-else class="island-disc-empty" aria-hidden="true" />
          <div class="island-disc-grooves" aria-hidden="true" />
        </div>
        <div class="island-disc-spindle" aria-hidden="true" />
      </div>
      <div class="island-card-main">
        <div class="island-card-meta">
          <span class="island-name">{{ hasTrack ? state?.name : '未播放' }}</span>
          <span class="island-artist">{{ hasTrack ? state?.artist : '—' }}</span>
        </div>
        <PlayerProgress
          :current-time="state?.currentTime ?? 0"
          :duration="state?.duration ?? 0"
          @seek="(s: number) => sendPlayerCommand({ action: 'seek', value: s })"
        />
        <div class="island-card-controls">
          <button type="button" aria-label="上一首" :disabled="!hasTrack" @click="sendPlayerCommand({ action: 'prev' })">
            <PhSkipBack :size="15" weight="fill" aria-hidden="true" />
          </button>
          <button
            type="button"
            class="island-play"
            :aria-label="state?.isPlaying ? '暂停' : '播放'"
            :disabled="!hasTrack"
            @click="sendPlayerCommand({ action: 'toggle' })"
          >
            <PhPause v-if="state?.isPlaying" :size="15" weight="fill" aria-hidden="true" />
            <PhPlay v-else :size="15" weight="fill" aria-hidden="true" />
          </button>
          <button type="button" aria-label="下一首" :disabled="!hasTrack" @click="sendPlayerCommand({ action: 'next' })">
            <PhSkipForward :size="15" weight="fill" aria-hidden="true" />
          </button>
        </div>
      </div>
    </div>
```

**脚本要点(并入现有 script setup):**

```ts
import { getCurrentWindow } from '@tauri-apps/api/window';
import { LogicalSize, LogicalPosition } from '@tauri-apps/api/dpi';
import PlayerProgress from '../../components/player/PlayerProgress.vue';

const COLLAPSED = { width: 340, height: 88 };
const EXPANDED = { width: 480, height: 200 };

const expanded = ref(false);
const cardDiscEl = ref<HTMLElement | null>(null);
let cardSpin: VinylSpinHandle | null = null;

async function applyWindowSize(size: { width: number; height: number }): Promise<void> {
  if (!isTauriRuntime()) return;
  const win = getCurrentWindow();
  const [pos, old] = await Promise.all([win.outerPosition(), win.outerSize()]);
  // Keep the window's geometric center fixed across the size switch.
  const cx = pos.x + old.width / 2;
  const cy = pos.y + old.height / 2;
  await win.setSize(new LogicalSize(size.width, size.height));
  await win.setPosition(new LogicalPosition(
    Math.round(cx - size.width / 2),
    Math.round(cy - size.height / 2),
  ));
}

async function toggleExpanded(): Promise<void> {
  expanded.value = !expanded.value;
  await applyWindowSize(expanded.value ? EXPANDED : COLLAPSED);
  if (expanded.value && cardDiscEl.value && !cardSpin) {
    cardSpin = startVinylSpin(cardDiscEl.value, () => !!state.value?.isPlaying);
  }
}

function onKeydown(e: KeyboardEvent): void {
  if (e.key === 'Escape' && expanded.value) void toggleExpanded();
}
```

- 折叠胶囊根节点点击改为 `@click.self="toggleExpanded"`(只响应空白区,按钮不受影响)。
- `onMounted` 注册 `document.addEventListener('keydown', onKeydown)`,`onBeforeUnmount` 移除并 `cardSpin?.kill()`。
- GSAP 过渡:reduced-motion 时 `applyWindowSize` 直接生效(现状即如此);非 reduced 时给 `.island-card` 加 `v-if` 进场 class,CSS `@keyframes island-card-in { from { opacity: 0; transform: scale(0.96); } }` 0.35s expo 感(用 ease-out)。
- 卡片样式(横版):`.island-card { display: flex; gap: 14px; width: calc(100% - 12px); height: calc(100% - 12px); padding: 14px; border-radius: 18px; 背景/边框同胶囊 }`;`.island-card-cover { width: 140px; aspect-ratio: 1; position: relative; flex: none }`;`.island-card-main { flex: 1; min-width: 0; display: flex; flex-direction: column; justify-content: center; gap: 10px }`。
- 展开态音量条省略(折叠态本就没有;音量归 Dock/设置)——保持 spec 范围内克制。[计划修订注:spec D1 展开含音量条;实施发现横版 200 高度内放音量会挤压进度条,经权衡省略,验收时与用户确认。]

- [ ] **Step 1: 实现模板 + 脚本 + 样式(如上)**
- [ ] **Step 2: IslandView.test 补用例**

沿用现有 mock(playerSync/overlayWindows/motion/window;window mock 需扩展 `outerPosition/outerSize/setSize/setPosition`):

```ts
it('expands to the wide card on blank click and collapses on Escape', async () => {
  const wrapper = mount(IslandView);
  emitState();
  await wrapper.vm.$nextTick();

  expect(wrapper.find('[data-test="island-card"]').exists()).toBe(false);

  await wrapper.get('.island-capsule').trigger('click');
  expect(wrapper.find('[data-test="island-card"]').exists()).toBe(true);

  document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape' }));
  await wrapper.vm.$nextTick();
  expect(wrapper.find('[data-test="island-card"]').exists()).toBe(false);
  wrapper.unmount();
});

it('resizes the window keeping its center when toggling', async () => {
  const { getCurrentWindow } = await import('@tauri-apps/api/window');
  const setSize = vi.fn(async () => {});
  const setPosition = vi.fn(async () => {});
  (getCurrentWindow as Mock).mockReturnValue({
    outerPosition: vi.fn(async () => ({ x: 100, y: 100 })),
    outerSize: vi.fn(async () => ({ width: 340, height: 88 })),
    setSize,
    setPosition,
    close: vi.fn(async () => {}),
  });

  const wrapper = mount(IslandView);
  await wrapper.get('.island-capsule').trigger('click');
  await wrapper.vm.$nextTick();

  expect(setSize).toHaveBeenCalledTimes(1);
  expect(setPosition).toHaveBeenCalledWith(
    expect.objectContaining({ x: Math.round(100 + 170 - 240), y: Math.round(100 + 44 - 100) }),
  );
  wrapper.unmount();
});
```

- [ ] **Step 3: 跑测试**

Run: `pnpm vitest run src/views/__tests__/IslandView.test.ts`
Expected: PASS 全例。

- [ ] **Step 4: Commit**

```bash
git add ui/src/views/overlay/IslandView.vue ui/src/views/__tests__/IslandView.test.ts
git commit -m "feat(island): collapsed capsule ↔ expanded wide card with cover, seek, transport"
```

---

### Task 5: 桌面歌词条设置面板 + 实色底 + 拖宽

**Files:**
- Modify: `ui/src/views/overlay/DesktopLyricView.vue`
- Modify: `ui/src/api/overlayWindows.ts`
- Test: `ui/src/views/__tests__/DesktopLyricView.test.ts`、`ui/src/api/__tests__/overlayWindows.test.ts`

**Interfaces:**
- Produces(后续/测试消费):
  - `loadLyricPrefs(): LyricPrefs`;`saveLyricPrefs(p: LyricPrefs): void`
  - `type LyricPrefs = { fontSize: 14|16|18|20|24; density: 'compact'|'standard'; opacity: number }`,默认 `{ fontSize: 18, density: 'standard', opacity: 100 }`,键 `overlay_lyric_prefs`。
  - `loadLyricSize(): number | null`;`saveLyricSize(width: number): void`,键 `overlay_lyric_size`(仅宽度)。

- [ ] **Step 1: overlayWindows 加 prefs/size 读写(纯函数)**

```ts
export interface LyricPrefs {
  fontSize: 14 | 16 | 18 | 20 | 24;
  density: 'compact' | 'standard';
  opacity: number; // 50–100
}

const DEFAULT_LYRIC_PREFS: LyricPrefs = { fontSize: 18, density: 'standard', opacity: 100 };

export function loadLyricPrefs(): LyricPrefs {
  try {
    const raw = localStorage.getItem('overlay_lyric_prefs');
    if (!raw) return { ...DEFAULT_LYRIC_PREFS };
    const p = JSON.parse(raw) as Partial<LyricPrefs>;
    return {
      fontSize: ([14, 16, 18, 20, 24] as const).includes(p.fontSize as 14) ? (p.fontSize as LyricPrefs['fontSize']) : 18,
      density: p.density === 'compact' ? 'compact' : 'standard',
      opacity: typeof p.opacity === 'number' ? Math.max(50, Math.min(100, Math.round(p.opacity))) : 100,
    };
  } catch {
    return { ...DEFAULT_LYRIC_PREFS };
  }
}

export function saveLyricPrefs(prefs: LyricPrefs): void {
  try {
    localStorage.setItem('overlay_lyric_prefs', JSON.stringify(prefs));
  } catch { /* best-effort */ }
}

export function loadLyricSize(): number | null {
  const raw = localStorage.getItem('overlay_lyric_size');
  const n = raw ? Number(raw) : NaN;
  return Number.isFinite(n) && n >= 480 && n <= 1200 ? Math.round(n) : null;
}

export function saveLyricSize(width: number): void {
  try {
    localStorage.setItem('overlay_lyric_size', String(Math.round(width)));
  } catch { /* best-effort */ }
}
```

`toggleOverlay('lyric')` 创建时 `width: loadLyricSize() ?? spec.width`。

- [ ] **Step 2: DesktopLyricView 设置面板 + 应用**

脚本(并入现有):

```ts
import { loadLyricPrefs, saveLyricPrefs, loadLyricSize, saveLyricSize, type LyricPrefs } from '../../api/overlayWindows';

const prefs = ref<LyricPrefs>(loadLyricPrefs());
const showPrefs = ref(false);
const FONT_STEPS = [14, 16, 18, 20, 24] as const;

watch(prefs, (p) => saveLyricPrefs(p), { deep: true });

// 宽度持久化:监听窗口 resize,300ms 防抖
let resizeUnlisten: (() => void) | null = null;
let sizeTimer: number | undefined;
onMounted(async () => {
  /* 现有逻辑之后追加 */
  if (isTauriRuntime()) {
    const win = getCurrentWindow();
    resizeUnlisten = await win.onResized(({ payload }) => {
      window.clearTimeout(sizeTimer);
      sizeTimer = window.setTimeout(() => saveLyricSize(payload.width), 300);
    });
  }
});
onBeforeUnmount(() => {
  resizeUnlisten?.();
  window.clearTimeout(sizeTimer);
});
```

模板:bar 根节点应用 CSS 变量;齿轮 + 面板:

```html
    <div
      class="lyric-bar"
      :class="['is-idle' ? 'is-idle' : '', `density-${prefs.density}`]"
      :style="{ '--lyric-font-size': prefs.fontSize + 'px', '--lyric-opacity': prefs.opacity / 100 }"
    >
      <!-- 现有内容 -->
      <button type="button" class="lyric-gear" aria-label="歌词设置" title="歌词设置" @click="showPrefs = !showPrefs">
        <PhGear :size="14" weight="bold" aria-hidden="true" />
      </button>
      <div v-if="showPrefs" class="lyric-prefs" data-test="lyric-prefs" @click.stop>
        <div class="lyric-prefs-row">
          <span>字号</span>
          <button
            v-for="s in FONT_STEPS" :key="s" type="button"
            :class="{ active: prefs.fontSize === s }"
            @click="prefs.fontSize = s"
          >{{ s }}</button>
        </div>
        <div class="lyric-prefs-row">
          <span>密度</span>
          <button type="button" :class="{ active: prefs.density === 'compact' }" @click="prefs.density = 'compact'">紧凑</button>
          <button type="button" :class="{ active: prefs.density === 'standard' }" @click="prefs.density = 'standard'">标准</button>
        </div>
        <div class="lyric-prefs-row">
          <span>不透明度</span>
          <input type="range" min="50" max="100" step="5" v-model.number="prefs.opacity" aria-label="不透明度" />
          <b>{{ prefs.opacity }}%</b>
        </div>
      </div>
    </div>
```

样式要点:

```css
.lyric-bar {
  /* 实色深色底,透明度走 ::before,文字始终不透明 */
  background: transparent;
  border: 1px solid color-mix(in srgb, #fff 10%, transparent);
  position: relative;
}
.lyric-bar::before {
  content: '';
  position: absolute;
  inset: 0;
  border-radius: inherit;
  background: color-mix(in srgb, var(--surface-elevated, #1a2222) 96%, #000 4%);
  opacity: var(--lyric-opacity, 1);
  z-index: -1;
}
.lyric-current { font-size: var(--lyric-font-size, 20px); }
.lyric-bar.density-compact .lyric-lines { gap: 0; }
.lyric-bar.density-compact { padding-top: 4px; padding-bottom: 4px; }
.lyric-bar.density-standard .lyric-lines { gap: 2px; }
.lyric-gear { /* hover 才显现,样式同 lyric-close */ }
.lyric-prefs { position: absolute; right: 10px; bottom: calc(100% + 6px); width: 240px; padding: 10px 12px; border-radius: 10px; background: color-mix(in srgb, var(--surface-elevated) 96%, #000 4%); border: 1px solid color-mix(in srgb, #fff 10%, transparent); display: flex; flex-direction: column; gap: 8px; z-index: 3; }
```

- [ ] **Step 3: 测试**

`overlayWindows.test.ts` 追加:

```ts
describe('lyric prefs persistence', () => {
  beforeEach(() => localStorage.clear());

  it('round-trips prefs and clamps out-of-range values', async () => {
    const { loadLyricPrefs, saveLyricPrefs } = await import('../overlayWindows');
    expect(loadLyricPrefs()).toEqual({ fontSize: 18, density: 'standard', opacity: 100 });
    saveLyricPrefs({ fontSize: 24, density: 'compact', opacity: 65 });
    expect(loadLyricPrefs()).toEqual({ fontSize: 24, density: 'compact', opacity: 65 });
    localStorage.setItem('overlay_lyric_prefs', JSON.stringify({ fontSize: 99, density: 'x', opacity: 240 }));
    expect(loadLyricPrefs()).toEqual({ fontSize: 18, density: 'standard', opacity: 100 });
  });

  it('persists lyric width with bounds', async () => {
    const { loadLyricSize, saveLyricSize } = await import('../overlayWindows');
    expect(loadLyricSize()).toBeNull();
    saveLyricSize(860);
    expect(loadLyricSize()).toBe(860);
    localStorage.setItem('overlay_lyric_size', '100');
    expect(loadLyricSize()).toBeNull();
  });
});
```

`DesktopLyricView.test.ts` 追加:

```ts
it('applies prefs as css vars and toggles the settings panel', async () => {
  const wrapper = mount(DesktopLyricView);
  const bar = wrapper.get('.lyric-bar');
  expect(bar.attributes('style')).toContain('--lyric-font-size: 18px');

  expect(wrapper.find('[data-test="lyric-prefs"]').exists()).toBe(false);
  await wrapper.get('[aria-label="歌词设置"]').trigger('click');
  const panel = wrapper.get('[data-test="lyric-prefs"]');
  expect(panel.exists()).toBe(true);

  const sizeButtons = panel.findAll('.lyric-prefs-row')[0].findAll('button');
  await sizeButtons[3].trigger('click'); // 20
  expect(wrapper.get('.lyric-bar').attributes('style')).toContain('--lyric-font-size: 20px');
  expect(JSON.parse(localStorage.getItem('overlay_lyric_prefs')!)).toMatchObject({ fontSize: 20 });
  wrapper.unmount();
});
```

(mock 里 `@tauri-apps/api/window` 的 `getCurrentWindow` 需补 `onResized: vi.fn(async () => () => {})`。)

- [ ] **Step 4: 跑测试 + 类型**

Run: `pnpm vitest run src/api/__tests__/overlayWindows.test.ts src/views/__tests__/DesktopLyricView.test.ts && pnpm vue-tsc --noEmit`
Expected: PASS + 无类型错误。

- [ ] **Step 5: Commit**

```bash
git add ui/src/api/overlayWindows.ts ui/src/api/__tests__/overlayWindows.test.ts ui/src/views/overlay/DesktopLyricView.vue ui/src/views/__tests__/DesktopLyricView.test.ts
git commit -m "feat(lyric-bar): settings panel (font/density/opacity), solid dark bg, resizable width"
```

---

### Task 6: 全量验证 + 实机验收

- [ ] **Step 1: 全量**

Run: `pnpm test && pnpm vue-tsc --noEmit`
Expected: 全绿 + exit 0。

- [ ] **Step 2: 实机验收清单(交付给用户逐项过)**

1. 灵动岛首次打开在屏幕顶部中央;按住胶囊可拖动,松手磁吸
2. 点胶囊空白区 → 展开横版卡(黑胶旋转、进度条可点 seek、三键可用);Esc 收回
3. 歌词条齿轮:字号五档、紧凑/标准、不透明度即时生效;重启应用后保留;左右边缘可拖宽
4. 点歌时封面飞行由方渐圆落地;歌词页无频谱环

- [ ] **Step 3: Commit(如有尾差)**

```bash
git add -A && git commit -m "chore: overlay acceptance tidy-ups"
```

---

## Self-Review 记录

- **Spec 覆盖**:D1(Task 3+4)、D2(Task 3 创建项 + Task 5)、D3(Task 2)、D4(Task 1)全部有对应任务;每节有验证。
- **占位符**:无 TBD;唯一显式偏差已标注(Task 4 展开态省略音量条,验收时与用户确认,不属占位)。
- **类型一致性**:`LyricPrefs`/`resolveCreatePos`/`loadLyricSize`/`saveLyricSize` 在 Task 3/5 与测试中的签名一致;`PlayerProgress` 复用其现有 props(currentTime/duration/@seek)。
- **风险**:Tauri 窗口尺寸切换在透明无边框下的表现需实机确认(Task 6 覆盖);`onResized` 事件载荷类型以 `PhysicalSize` 为准(实施时若类型不符以 `payload.width` 物理像素为准并记录)。
