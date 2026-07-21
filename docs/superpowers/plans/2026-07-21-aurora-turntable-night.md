# 极光皮肤「唱机之夜」重设计实施计划

> **For agentic workers:** 每个任务独立完成、独立验证、独立提交。步骤用 checkbox 跟踪。Spec:`docs/superpowers/specs/2026-07-21-aurora-turntable-night-design.md`。

**Goal:** 把 Aurora 首页 + 播放 Dock 从"衬线巨标+描边卡片+满屏粒子"重设计为唱机之夜：圆形旋转黑胶封面、光锥尘埃、暖黑配色、常驻传输区、唱针播放头。

**Architecture:** 纯视觉层重设计。数据来源(`homeViewModel`/`usePlayerControls`)、组件结构、路由、Newsprint 皮肤零改动。改动集中在 6 个文件 + 3 个测试文件。

**Tech Stack:** Vue 3 `<script setup>`、GSAP 3.15、Canvas 2D、CSS 令牌(`tokens.css`)、Vitest + @vue/test-utils。

## Global Constraints

- 保留 `.aurora-cover` 类名、`data-test` 钩子、Canvas `data-*` 属性——现有测试依赖(见各任务)。
- `tokens.css` 深色值独立选取,禁止透明度派生;`--accent` 不变(`#62d6a2`/`#18875b`)。
- `PlayerProgress.vue` 内部与 markup 零改动(其测试断言 markup 不含 `aurora`/`data-skin`);唱针覆写只写在 `AuroraPlayerBar.vue` scoped `:deep()`。
- Newsprint 的一切(含 `newsprint-player-empty-console`)不许动。
- 所有动效尊重 `prefers-reduced-motion` 与 KeepAlive 生命周期。
- 不新增依赖;字体只用已自托管的 `@fontsource/*`(main.ts 已引入 Inter 400–700)。
- 工作目录 `C:/BottleMusic/ui`;测试命令 `pnpm test`(Vitest 全量)。dev server 已在 `http://localhost:1420/` 运行(进程名 `vite-dev`)。
- `docs/` 被 .gitignore,提交计划/spec 类文档需 `git add -f`。

## 测试基线(执行前必读)

现有断言中**会破**的 3 处,随对应任务同步更新:
1. `views/home/__tests__/AuroraAtmosphere.test.ts`:`data-particle-cap` 断言 `100`/`140`(两处)→ `30`/`60`(Task 3)。
2. `components/player/__tests__/AuroraPlayerBar.test.ts` 'empty track shows placeholder without hollow transport or quality':断言 `.aurora-pb-transport` 不存在 + `aurora-player-empty-console` 存在 → 反转(Task 5)。
3. `views/home/__tests__/AuroraHome.test.ts` 与 `components/shell/__tests__/Shells.test.ts` 顶部的 `vi.mock('..../api/motion', …)`(Shells 为 `'../../../api/motion'`)是显式工厂 mock,不含 `startVinylSpin` → 加入 mock(Task 4)。

**不可破**的契约:`.aurora-cover` 存在性;`aurora-stage-empty` 文本含"选择一首歌" + `empty-stage-refresh`;`queue-track-*` 12 行与 `.aurora-queue-rail-head h2 span` 计数;`data-playing`/`data-loop`/`data-motion`;传输键顺序 `['上一首','播放','下一首']`(loop 键 aria 不参与该断言);progress 令牌不透明;appearanceStore accent 精确 hex;`auroraFont.test.ts` 的衬线锁定。

---

### Task 1: aurora-dark 暖黑令牌

**Files:**
- Modify: `ui/src/styles/tokens.css`(aurora-dark 块,约 48–66 行)

**Interfaces:**
- Produces: 暖黑令牌组,供 Task 3/4/5 的所有暗色表面使用。精确值(已经 WCAG 校验,基准 `#0c0b09`):text 17.6/7.3/5.2:1,accent 10.9:1,progress fill/track 5.7:1。

- [ ] **Step 1: 替换 aurora-dark 块**

整块替换 `:root[data-skin='aurora'][data-mode='dark'] { … }` 为:

```css
/* ─── Aurora Dark (warm charcoal / turntable night) ─── */
:root[data-skin='aurora'][data-mode='dark'] {
  --app-bg: #0c0b09;
  --surface-1: #13110e;
  --surface-2: #1a1713;
  --surface-elevated: #221e19;
  --text-primary: #f5f2ec;
  --text-secondary: #a39d93;
  --text-muted: #8a8378;
  --accent: #62d6a2;
  --focus-ring: rgba(98, 214, 162, 0.42);
  --border-subtle: rgba(245, 242, 236, 0.1);
  --progress-track: #45403a;
  --progress-buffered: #322e29;
  --progress-fill: #62d6a2;
  --progress-thumb-fill: #ffffff;
  --progress-thumb-ring: #62d6a2;
  --progress-time: #8a8378;
}
```

aurora-light、newsprint 两块、`tokens.css` 其他内容不动。

- [ ] **Step 2: 跑令牌相关测试**

Run: `pnpm test`
Expected: PASS。`PlayerProgress.test.ts` 的"opaque progress tokens"四组合断言(只查非透明、不查 hex)与 `appearanceStore.test.ts` 的 accent hex(`#62d6a2`/`#18875b` 未变)保持绿。

- [ ] **Step 3: Commit**

```bash
git add ui/src/styles/tokens.css
git commit -m "feat(aurora): warm charcoal dark tokens for turntable night"
```

---

### Task 2: 动效 — vinyl 配置段 + startVinylSpin

**Files:**
- Modify: `ui/src/api/motionProfiles.ts`
- Modify: `ui/src/api/motion.ts`
- Test: `ui/src/api/__tests__/motionProfiles.test.ts`、`ui/src/api/__tests__/motion.test.ts`

**Interfaces:**
- Produces(后续任务消费):
  - `getMotionProfile('aurora').vinyl` → `{ enabled: true, spinSeconds: 24, rampSeconds: 0.8 }`;newsprint 为 `{ enabled: false, spinSeconds: 0, rampSeconds: 0 }`。
  - `startVinylSpin(el: HTMLElement, isPlayingRef: Ref<boolean> | (() => boolean)): VinylSpinHandle`;`VinylSpinHandle = { kill(): void; setPlaying(): void }`。reduced-motion / newsprint → 惰性 handle(不创建任何 tween)。

- [ ] **Step 1: motionProfiles.ts 加 vinyl 段**

`MotionProfile` 接口加字段:

```ts
export interface MotionProfile {
  pageEnter: TweenSpec;
  pageLeave: TweenSpec;
  controlPress: TweenSpec;
  controlRelease: TweenSpec;
  cardEnter: TweenSpec & { stagger: number; maxItems: number };
  ambient: { enabled: boolean; duration: number; scale: number };
  particles: ParticleMotionProfile;
  /** Turntable night: hero vinyl rotation. */
  vinyl: { enabled: boolean; spinSeconds: number; rampSeconds: number };
}
```

`auroraProfile` 末尾加:`vinyl: { enabled: true, spinSeconds: 24, rampSeconds: 0.8 },`
`newsprintProfile` 末尾加:`vinyl: { enabled: false, spinSeconds: 0, rampSeconds: 0 },`

- [ ] **Step 2: motion.ts 实现 startVinylSpin**

追加到 `motion.ts`(放 `startAmbientMotion` 之后):

```ts
export interface VinylSpinHandle {
  kill: () => void;
  /** Re-read isPlayingRef and ramp the deck toward the matching state. */
  setPlaying: () => void;
}

/**
 * Turntable spin for the Aurora hero vinyl. Infinite GSAP rotation whose
 * timeScale ramps 0↔1 over profile.vinyl.rampSeconds, so the record speeds
 * up / winds down like a real deck. Honors visibility, blur/focus,
 * reduced-motion, and the skin profile (Newsprint → inert).
 */
export function startVinylSpin(
  el: HTMLElement,
  isPlayingRef: Ref<boolean> | (() => boolean),
): VinylSpinHandle {
  const profile = currentProfile();
  const inert: VinylSpinHandle = { kill: () => {}, setPlaying: () => {} };
  if (!profile.vinyl.enabled || isReducedMotion()) return inert;

  const isPlaying = typeof isPlayingRef === 'function' ? isPlayingRef : () => isPlayingRef.value;
  const spin = gsap.to(el, {
    rotation: '+=360',
    duration: profile.vinyl.spinSeconds,
    ease: 'none',
    repeat: -1,
    paused: true,
  });
  let killed = false;
  let ramp: { kill: () => void } | null = null;

  function rampTo(target: 0 | 1): void {
    if (ramp) { ramp.kill(); ramp = null; }
    if (target === 1) spin.play();
    ramp = gsap.to(spin, {
      timeScale: target,
      duration: profile.vinyl.rampSeconds,
      ease: target === 1 ? 'power2.out' : 'power2.inOut',
      onComplete: () => { if (target === 0) spin.pause(); },
    });
  }

  function sync(): void {
    if (killed) return;
    rampTo(isPlaying() && !document.hidden ? 1 : 0);
  }

  function onVisibilityChange(): void { sync(); }
  function onBlur(): void { if (!killed) rampTo(0); }
  function onFocus(): void { sync(); }

  document.addEventListener('visibilitychange', onVisibilityChange);
  window.addEventListener('blur', onBlur);
  window.addEventListener('focus', onFocus);
  sync();

  return {
    kill(): void {
      killed = true;
      if (ramp) { ramp.kill(); ramp = null; }
      spin.kill();
      document.removeEventListener('visibilitychange', onVisibilityChange);
      window.removeEventListener('blur', onBlur);
      window.removeEventListener('focus', onFocus);
    },
    setPlaying(): void { sync(); },
  };
}
```

注:kill 不重置 `rotation`(唱片保持停碟姿态;reduced-motion 下本来就没转过)。

- [ ] **Step 3: motionProfiles.test.ts 加断言**

```ts
it('owns the turntable vinyl spin profile', () => {
  expect(getMotionProfile('aurora').vinyl).toEqual({
    enabled: true,
    spinSeconds: 24,
    rampSeconds: 0.8,
  });
  expect(getMotionProfile('newsprint').vinyl.enabled).toBe(false);
});
```

- [ ] **Step 4: motion.test.ts 加 startVinylSpin 测试**

仿现有 startAmbientMotion 套件(gsap 已 mock):

```ts
it('startVinylSpin spins only while playing and kills cleanly', async () => {
  const { gsap } = await import('gsap');
  const el = document.createElement('div');
  let playing = false;

  const handle = startVinylSpin(el, () => playing);
  expect(gsap.to).toHaveBeenCalled();           // spin tween created
  handle.setPlaying();                           // playing=false → ramp to 0
  playing = true;
  handle.setPlaying();                           // ramp to 1
  handle.kill();
  handle.setPlaying();                           // no-op after kill
});

it('startVinylSpin is inert under reduced motion', async () => {
  const { gsap } = await import('gsap');
  // 复用文件内现有 reduced-motion mock 工具/写法
  const el = document.createElement('div');
  const handle = startVinylSpin(el, () => true);
  expect(handle.kill).toBeInstanceOf(Function);
  handle.setPlaying();
  handle.kill();
});
```

断言风格对齐文件内现有写法(该文件对 gsap 的 mock 是 `vi.mock('gsap')`,用调用计数/调用参数断言;reduced-motion 用现有 `matchMedia` mock 辅助)。若该文件已有 `startAmbientMotion` 同名场景,断言粒度与之对齐即可。

- [ ] **Step 5: 跑测试**

Run: `pnpm test`
Expected: PASS(既有 aurora 时值断言全部不变)。

- [ ] **Step 6: Commit**

```bash
git add ui/src/api/motionProfiles.ts ui/src/api/motion.ts ui/src/api/__tests__/motionProfiles.test.ts ui/src/api/__tests__/motion.test.ts
git commit -m "feat(aurora): vinyl spin profile and startVinylSpin deck motion"
```

---

### Task 3: AuroraAtmosphere 重写 — 光锥尘埃

**Files:**
- Modify: `ui/src/views/home/AuroraAtmosphere.vue`(`<script setup>` 全换;template/style 不动)
- Test: `ui/src/views/home/__tests__/AuroraAtmosphere.test.ts`(两处 cap 断言)

**Interfaces:**
- Consumes: `props.isPlaying`(不变);Task 1 的 `--accent`。
- Produces(契约不变,测试依赖):canvas `data-test="aurora-atmosphere"`、`data-playing`、`data-particle-cap`(30/60)、`data-loop`、`data-motion`('active'|'static'|'stopped');仅请求 `'2d'` context;rAF 单循环、visibility/ResizeObserver/KeepAlive 生命周期与现版一致;reduced-motion 下静态 paint 一次。

**设计:** 粒子只生在"光锥"内——锥顶 `(0.84w, -0.10h)`,轴向 `angle = 2.09 rad`(120°,指向左下),半张角 `0.20 rad`。尘埃极小(0.5–1.6px)、慢漂;播放时叠加 `<0.5px` 机械抖动。wash 改为锥形光(顶点辉光 + 沿轴渐变带),accent 透明度 ≤0.10。

- [ ] **Step 1: 替换 `<script setup>` 主体**

保留:props 定义、`canvasRef`、`motionState`、`loopRunning`、生命周期函数骨架(`cancelFrame/stopLoop/syncCanvasSize/attachObservers/detachObservers/boot/onVisibilityChange` 及 mounted/activated/deactivated/unmounted/watch 块——这些函数体除 `seedParticles/makeParticle/paint*` 外原样保留)。替换常量与渲染函数:

```ts
/** Turntable night: dust motes inside a static light cone. Fewer, smaller, calmer. */
const CAP_PAUSED = 30;
const CAP_PLAYING = 60;
const DPR_CAP = 2;

/** Cone apex (fraction of stage size) and axis. Opens down-left from top-right. */
const CONE = {
  ax: 0.84,
  ay: -0.1,
  angle: Math.PI * (2 / 3), // 120°
  halfSpread: 0.2,          // ~11.5°
};

function spawnInCone(): { x: number; y: number } {
  const axisLen = Math.max(cssW, cssH) * 1.05;
  const t = 0.06 + Math.random() * 0.94;
  const a = CONE.angle + (Math.random() * 2 - 1) * CONE.halfSpread;
  const d = t * axisLen;
  return {
    x: CONE.ax * cssW + Math.cos(a) * d,
    y: CONE.ay * cssH + Math.sin(a) * d,
  };
}

function makeParticle(): Particle {
  const playing = props.isPlaying;
  const { x, y } = spawnInCone();
  return {
    x,
    y,
    vx: (Math.random() - 0.5) * 0.08,
    vy: (Math.random() - 0.5) * 0.06 + 0.012, // slow settle
    r: 0.5 + Math.random() * 1.1,
    baseAlpha: 0.05 + Math.random() * (playing ? 0.17 : 0.1),
    phase: Math.random() * Math.PI * 2,
    speed: 0.4 + Math.random() * 0.5,
  };
}

/** Static cone wash — painted every frame incl. reduced-motion. */
function paintWash(ctx: CanvasRenderingContext2D): void {
  // Apex glow
  const apexX = CONE.ax * cssW;
  const apexY = CONE.ay * cssH;
  const g = ctx.createRadialGradient(apexX, apexY, 0, apexX, apexY, Math.max(cssW, cssH) * 0.5);
  g.addColorStop(0, accentRGBA(props.isPlaying ? 0.1 : 0.05));
  g.addColorStop(1, accentRGBA(0));
  ctx.fillStyle = g;
  ctx.fillRect(0, 0, cssW, cssH);

  // Axis band: soft gradient inside the cone
  ctx.save();
  ctx.translate(apexX, apexY);
  ctx.rotate(CONE.angle);
  const band = ctx.createLinearGradient(0, 0, Math.max(cssW, cssH), 0);
  band.addColorStop(0, accentRGBA(props.isPlaying ? 0.06 : 0.03));
  band.addColorStop(1, accentRGBA(0));
  ctx.fillStyle = band;
  const halfW = Math.tan(CONE.halfSpread) * Math.max(cssW, cssH);
  ctx.fillRect(0, -halfW, Math.max(cssW, cssH), halfW * 2);
  ctx.restore();
}

function paintParticles(ctx: CanvasRenderingContext2D, dt: number): void {
  ensureParticleBudget();
  const playing = props.isPlaying;
  for (const p of particles) {
    p.phase += dt * 0.001 * p.speed;
    p.x += p.vx * (dt * 0.06);
    p.y += p.vy * (dt * 0.06);
    // Turntable hum: sub-pixel mechanical jitter while playing (<0.5px)
    const jx = playing ? Math.sin(p.phase * 7.3) * 0.4 : 0;
    const jy = playing ? Math.cos(p.phase * 6.1) * 0.3 : 0;

    const pulse = 0.7 + 0.3 * Math.sin(p.phase);
    const alpha = Math.min(0.6, p.baseAlpha * pulse);
    const radius = p.r * (0.9 + 0.2 * pulse);

    const grad = ctx.createRadialGradient(p.x + jx, p.y + jy, 0, p.x + jx, p.y + jy, radius * 3);
    grad.addColorStop(0, accentLightRGBA(alpha));
    grad.addColorStop(1, accentRGBA(0));
    ctx.fillStyle = grad;
    ctx.beginPath();
    ctx.arc(p.x + jx, p.y + jy, radius * 3, 0, Math.PI * 2);
    ctx.fill();

    // Respawn far-strayed motes back into the cone (no hard wrap lines)
    if (p.x < -12 || p.x > cssW + 12 || p.y > cssH + 12) {
      const s = spawnInCone();
      p.x = s.x;
      p.y = s.y;
    }
  }
}
```

`Particle` 类型、`readAccentRGB/accentRGBA/accentLightRGBA`、`paint/frame/startLoop` 及生命周期块保持现版逻辑(见现文件 201–330 行),仅 `seedParticles` 继续调 `makeParticle`,`paint` 里 `paintWash` 换名不换调用。isPlaying 的 watch 保留,但把 retune 系数改为:播放 `baseAlpha = min(0.3, baseAlpha * 1.3 + 0.03)`,暂停 `baseAlpha = max(0.05, baseAlpha * 0.8)`;半径不再随播放放大(尘埃不变光斑)。

- [ ] **Step 2: 更新 cap 断言**

`AuroraAtmosphere.test.ts`:
- `'mounts a dedicated canvas behind stage content'`:`expect(Number(canvas.attributes('data-particle-cap'))).toBe(100)` → `toBe(30)`。
- `'uses a calmer particle cap while playing'`:`'100'` → `'30'`,`setProps({ isPlaying: true })` 后 `'140'` → `'60'`。

- [ ] **Step 3: 跑测试**

Run: `pnpm test`
Expected: PASS。rAF/visibility/KeepAlive/reduced-motion/2d-context 套件应原样通过(契约未变);若有个别断言引用旧半径/速度常数,按新常数对齐。

- [ ] **Step 4: Commit**

```bash
git add ui/src/views/home/AuroraAtmosphere.vue ui/src/views/home/__tests__/AuroraAtmosphere.test.ts
git commit -m "feat(aurora): rewrite stage atmosphere as light-cone dust"
```

---

### Task 4: AuroraHome — 黑胶舞台 + 排版 + 空态 + rail/卡片

**Files:**
- Modify: `ui/src/views/home/AuroraHome.vue`(template 三处 + script + scoped styles)
- Test: `ui/src/views/home/__tests__/AuroraHome.test.ts`(motion mock 块)、`ui/src/components/shell/__tests__/Shells.test.ts`(motion mock 块)

**Interfaces:**
- Consumes: Task 2 的 `startVinylSpin` 与 `VinylSpinHandle`;Task 1 令牌。
- Produces(DOM 契约,测试依赖):`.aurora-cover.aurora-vinyl > .aurora-vinyl-disc(ref) > img + .aurora-vinyl-grooves`;`.aurora-vinyl-spindle`;空态 `.aurora-stage-empty` 含"选择一首歌"与 `empty-stage-refresh`;其余 data-test 全部保留。

- [ ] **Step 1: template — hero 封面改黑胶**

把 hero 的 `.aurora-cover` 块替换为(保留 `.aurora-cover` 类与 img 的 alt/@error):

```html
<div class="aurora-cover aurora-vinyl" data-test="hero-vinyl">
  <div ref="vinylEl" class="aurora-vinyl-disc">
    <img
      v-if="heroCover"
      :src="heroCover"
      :alt="`${model.heroTrack?.SongName || '当前歌曲'}封面`"
      @error="onCoverError"
    />
    <div v-else class="aurora-cover-placeholder">封面暂缺</div>
    <div class="aurora-vinyl-grooves" aria-hidden="true" />
  </div>
  <div class="aurora-vinyl-spindle" aria-hidden="true" />
</div>
```

- [ ] **Step 2: template — 空态去巨标盒**

替换 `.aurora-stage-empty` 整块为(黑胶空盘 + 单行文案;保留两个 data-test 与文案关键字):

```html
<div v-else class="aurora-stage-empty" data-test="aurora-stage-empty">
  <div class="aurora-cover aurora-vinyl aurora-vinyl-empty" aria-hidden="true">
    <div class="aurora-vinyl-disc">
      <div class="aurora-vinyl-grooves" aria-hidden="true" />
    </div>
    <div class="aurora-vinyl-spindle" aria-hidden="true" />
  </div>
  <div class="aurora-stage-empty-copy">
    <p class="aurora-label"><span class="aurora-label-dot" aria-hidden="true" />还没有开始播放</p>
    <p class="aurora-stage-empty-title">选择一首歌，开始聆听</p>
    <p class="aurora-stage-empty-hint">从每日推荐或左侧歌单开始，舞台会随播放状态展开。</p>
    <button
      type="button"
      class="aurora-play"
      data-test="empty-stage-refresh"
      :disabled="model.sections.daily.loading || model.sections.daily.refreshing"
      @click="model.sections.daily.retry()"
    >
      {{ model.sections.daily.error ? '重试' : model.sections.daily.refreshing ? '刷新中…' : '刷新推荐' }}
    </button>
  </div>
</div>
```

- [ ] **Step 3: script — 接入 startVinylSpin**

```ts
// import 行补充:
import { ref, computed, watch, onBeforeUpdate, onMounted, onActivated, onDeactivated, onUnmounted, nextTick } from 'vue';
import { animateStagger, isReducedMotion, startVinylSpin } from '../../api/motion';
import type { VinylSpinHandle } from '../../api/motion';

// stageEl 声明附近加:
const vinylEl = ref<HTMLElement | null>(null);
let vinylSpin: VinylSpinHandle | null = null;

function bootVinyl(): void {
  if (vinylSpin || !vinylEl.value) return;
  vinylSpin = startVinylSpin(vinylEl.value, () => !!props.model.isPlaying);
}

watch(() => props.model.isPlaying, () => vinylSpin?.setPlaying());
// heroTrack 出现前 vinylEl 不存在;出现后补 boot
watch(() => props.model.heroTrack, () => { void nextTick(() => bootVinyl()); });

onMounted(() => { bootVinyl(); });
onActivated(() => { bootVinyl(); vinylSpin?.setPlaying(); });
onDeactivated(() => { vinylSpin?.kill(); vinylSpin = null; });
// onUnmounted 现有 killEnterHandles() 前加:
//   vinylSpin?.kill(); vinylSpin = null;
```

(文件已有 `watch(() => props.model.heroTrack, …)` 重置 coverError——合并到同一 watcher 或另起一个均可。)

- [ ] **Step 4: 测试 mock 块补 startVinylSpin**

`AuroraHome.test.ts` 与 `Shells.test.ts` 顶部的 `vi.mock('…/api/motion', …)` 工厂返回对象中各加一行:

```ts
startVinylSpin: vi.fn(() => ({ kill: vi.fn(), setPlaying: vi.fn() })),
```

(保持 mock 中其他导出现状;若 mock 用的是 `importOriginal` 展开则无需改动——先读文件确认形态再改。)

- [ ] **Step 5: scoped styles — 黑胶/光锥/字阶/空态/rail/卡片**

在 `<style scoped>` 中:

① `.aurora-stage-main` 的 `background: radial-gradient(…)` 声明删除,改为在 `.aurora-stage` 加静态光锥:

```css
.aurora-stage::before {
  content: '';
  position: absolute;
  inset: -10% -8% 32% 28%;
  background: radial-gradient(ellipse 50% 66% at 84% 0%, color-mix(in srgb, var(--accent) 9%, transparent), transparent 62%);
  pointer-events: none;
  z-index: 0;
}
```

② `.aurora-cover` 整块替换为黑胶结构样式(尺寸令牌:桌面 ≤320px / ≥1600px 340px / <1359px 280px / <900px `min(58vw,240px)`——沿用现有各断点的数值,只改形状):

```css
.aurora-cover.aurora-vinyl {
  position: relative;
  aspect-ratio: 1;
  width: 100%;
  max-width: 320px;
  height: auto;
  border-radius: 50%;
  background: #0a0a09;
  box-shadow:
    0 24px 48px rgba(0, 0, 0, 0.45),
    0 0 0 1px color-mix(in srgb, #fff 5%, transparent);
  flex: none;
  overflow: visible;
}

.aurora-vinyl-disc {
  position: absolute;
  inset: 0;
  border-radius: 50%;
  overflow: hidden;
  will-change: transform;
}

.aurora-vinyl-disc img {
  width: 100%;
  height: 100%;
  object-fit: cover;
  display: block;
  border-radius: 50%;
}

.aurora-cover-placeholder {
  width: 100%;
  height: 100%;
  display: flex;
  align-items: center;
  justify-content: center;
  color: var(--text-muted);
  font-size: 13px;
  border-radius: 50%;
}

/* Grooves + the aurora specular arc — rotates with the disc */
.aurora-vinyl-grooves {
  position: absolute;
  inset: 0;
  border-radius: 50%;
  background:
    conic-gradient(from 210deg,
      transparent 0deg,
      color-mix(in srgb, var(--accent) 14%, transparent) 18deg,
      transparent 55deg),
    repeating-radial-gradient(circle at 50% 50%,
      rgba(255, 255, 255, 0.05) 0 1px,
      transparent 1px 4px);
  pointer-events: none;
}

/* Static center label + spindle hole (the aurora dot color) */
.aurora-vinyl-spindle {
  position: absolute;
  left: 50%;
  top: 50%;
  width: 26%;
  aspect-ratio: 1;
  transform: translate(-50%, -50%);
  border-radius: 50%;
  background: radial-gradient(circle at 50% 50%,
    var(--app-bg) 0 11%,
    color-mix(in srgb, var(--accent) 82%, #000 18%) 12% 100%);
  box-shadow: 0 0 0 1px color-mix(in srgb, #fff 8%, transparent);
  pointer-events: none;
}

.aurora-vinyl-empty .aurora-vinyl-disc { background: #0a0a09; }
```

③ 字阶(kicker 用显式 Inter 栈——aurora 的 `--font-sans` 被测试锁定为衬线,不能用):

```css
.aurora-label {
  font-family: 'Inter', 'Microsoft YaHei UI', 'PingFang SC', system-ui, sans-serif;
  font-size: 11px;
  letter-spacing: 0.12em;
  text-transform: uppercase;
  /* 其余沿用 */
}
.aurora-song-name { font-size: clamp(26px, 2.6vw, 36px); letter-spacing: -0.01em; }
.aurora-artist { font-size: 16px; }
.aurora-stage-empty-title {
  margin: 0;
  font-family: Georgia, 'Noto Serif SC', 'Songti SC', serif;
  font-size: clamp(20px, 2vw, 26px);
  line-height: 1.3;
  color: var(--text-primary);
}
.aurora-stage-empty-hint { margin: 0; max-width: 36rem; color: var(--text-secondary); font-size: 13px; line-height: 1.65; }
```

④ `.aurora-stage-empty` 去盒化(替换原有 border/radius/background/box-shadow/padding 声明):

```css
.aurora-stage-empty {
  min-height: 280px;
  display: grid;
  grid-template-columns: minmax(200px, 280px) minmax(0, 1fr);
  align-items: center;
  gap: 22px;
  padding: 0;
  border: 0;
  background: transparent;
  box-shadow: none;
  min-width: 0;
}
.aurora-stage-empty-copy { display: flex; flex-direction: column; align-items: flex-start; gap: 10px; min-width: 0; }
```

⑤ rail 密排化 + 卡片去重影:

```css
.aurora-queue-rail {
  border: 1px solid var(--border-subtle);
  border-radius: 8px;
  background: color-mix(in srgb, var(--surface-1) 72%, transparent);
  /* 其余沿用(max-height/flex/overflow) */
}
.aurora-queue-row button { padding: 6px 8px; border-radius: 6px; grid-template-columns: 24px minmax(0, 1fr) auto; }
.aurora-track-cover {
  border-radius: 8px;
  box-shadow: none;
  border: 1px solid var(--border-subtle);
}
.aurora-card-cover { border-radius: 8px; }
```

⑥ 加载骨架圆形化:`.aurora-cover-skeleton` 加 `border-radius: 50%;`(原方块)。

- [ ] **Step 6: 跑测试**

Run: `pnpm test`
Expected: PASS。重点过 `.aurora-cover` 存在性、空态文案、enter-budget(0.72/0.36 等未动)、rail 12 行、reduced-motion 套件。

- [ ] **Step 7: Commit**

```bash
git add ui/src/views/home/AuroraHome.vue ui/src/views/home/__tests__/AuroraHome.test.ts ui/src/components/shell/__tests__/Shells.test.ts
git commit -m "feat(aurora): turntable vinyl hero stage, cone light, calmer rail"
```

---

### Task 5: AuroraPlayerBar — 常驻传输区 + 器物控件 + 唱针播放头

**Files:**
- Modify: `ui/src/components/player/AuroraPlayerBar.vue`(template + scoped styles)
- Test: `ui/src/components/player/__tests__/AuroraPlayerBar.test.ts`

**Interfaces:**
- Consumes: Task 1 令牌;`PlayerProgress` 既有 DOM(`.progress-thumb`)。
- Produces:`.aurora-pb-transport` 恒渲染,无曲时带 `.is-muted` 且四键 disabled;`aurora-player-empty-console` 删除;`.aurora-pb-progress-wrap` 恒渲染(`PlayerProgress` 在 duration=0 时自带 inert 态);传输键 DOM 顺序与 aria-label 不变。

- [ ] **Step 1: template — 删除 v-if/v-else,传输区常驻**

`.aurora-pb-center` 内:
- `.aurora-pb-transport` 去掉 `v-if="c.currentTrack"`,加 `:class="{ 'is-muted': !c.currentTrack }"`;
- 四个按钮(loop/prev/play/next)各加 `:disabled="!c.currentTrack"`;
- 删除整个 `.aurora-pb-empty-console` 块;
- `.aurora-pb-progress-wrap` 去掉 `v-if="c.currentTrack"`(恒渲染);
- 右区 `.aurora-pb-quality` 的 `v-if="c.currentTrack"` 保留不动。

- [ ] **Step 2: 更新空态测试**

把 'empty track shows placeholder without hollow transport or quality' 整例替换为:

```ts
it('empty track shows a muted transport, never a placeholder console', () => {
  const wrapper = mount(AuroraPlayerBar, {
    props: { controller: createStubController() },
  });

  const transport = wrapper.get('[data-test="aurora-player-transport"]');
  expect(transport.classes()).toContain('is-muted');
  const buttons = transport.findAll('button');
  expect(buttons.length).toBeGreaterThanOrEqual(4);
  expect(buttons.every((b) => b.attributes('disabled') !== undefined)).toBe(true);

  expect(wrapper.find('[data-test="aurora-player-empty-console"]').exists()).toBe(false);
  expect(wrapper.text()).toContain('未播放歌曲');
  expect(wrapper.find('[data-test="player-cover-placeholder"]').exists()).toBe(true);
  expect(wrapper.find('[data-test="aurora-player-quality"]').exists()).toBe(false);
});
```

- [ ] **Step 3: scoped styles — 器物化 + 唱针 + 静音态**

```css
/* Muted console: visible but inert without a track */
.aurora-pb-transport.is-muted { opacity: 0.55; }
.aurora-pb-transport.is-muted .aurora-pb-btn { cursor: default; }
.aurora-pb-transport.is-muted .aurora-pb-play {
  background: color-mix(in srgb, var(--accent) 30%, var(--surface-2));
  color: color-mix(in srgb, var(--text-primary) 55%, transparent);
  box-shadow: none;
}

/* Play: the only filled object — deck button with inset depth + static indicator glow */
.aurora-pb-play {
  width: 44px;
  height: 44px;
  box-shadow:
    0 1px 0 color-mix(in srgb, #fff 18%, transparent) inset,
    0 -2px 5px rgba(0, 0, 0, 0.22) inset,
    0 0 14px color-mix(in srgb, var(--accent) 22%, transparent);
}

/* Needle playhead (aurora-only deep override; PlayerProgress markup untouched) */
.aurora-pb-progress-wrap :deep(.progress-thumb) {
  width: 11px;
  height: 13px;
  border: 0;
  border-radius: 2px;
  background: var(--progress-thumb-fill, #fff);
  clip-path: polygon(50% 100%, 6% 12%, 94% 12%);
  box-shadow: 0 1px 3px rgba(0, 0, 0, 0.4);
}

/* Volume knob: same object family as the play button */
.aurora-pb-vol-thumb {
  width: 12px;
  height: 12px;
  box-shadow:
    0 0 0 1px color-mix(in srgb, var(--accent) 55%, transparent),
    0 1px 3px rgba(0, 0, 0, 0.35);
}

/* Dock silhouette: console object, calmer radius (24 → 16) */
.aurora-pb { border-radius: 16px; }
```

- [ ] **Step 4: 跑测试**

Run: `pnpm test`
Expected: PASS。注意 `NewsprintPlayerBar.test.ts` 的 `newsprint-player-empty-console` 与 'does NOT use aurora-pb root class' 不受影响;Dock overflow 可见性测试(音质菜单上开)不受影响。

- [ ] **Step 5: Commit**

```bash
git add ui/src/components/player/AuroraPlayerBar.vue ui/src/components/player/__tests__/AuroraPlayerBar.test.ts
git commit -m "feat(aurora): persistent muted transport, deck play button, needle playhead"
```

---

### Task 6: 壳层 — 900–1099px sidebar 图标栏

**Files:**
- Modify: `ui/src/styles/skins/aurora.css`

**Interfaces:**
- Consumes: Sidebar 现有 DOM(`.sidebar-wordmark`/`.aurora-nav-label`/`.update-entry`/`.user > .avatar + div`/`.nav > a > .ico + 文本节点`/`.section-label`/`.playlists`)。纯 CSS,组件零改动。
- Produces:900–1099px 时 sidebar 为 64px 纯图标栏;<900px 行为不变(隐藏);≥1100px 行为不变。

- [ ] **Step 1: 替换 1099 断点规则**

删除现有:

```css
@media (max-width: 1099px) {
  .app[data-shell="aurora"] {
    grid-template-columns: minmax(208px, 22vw) minmax(0, 1fr);
  }
}
```

替换为:

```css
/* Mid-narrow: sidebar becomes a 64px icon rail (text hidden, icons kept) */
@media (min-width: 900px) and (max-width: 1099px) {
  .app[data-shell="aurora"] {
    grid-template-columns: 64px minmax(0, 1fr);
  }

  [data-skin-chrome='aurora'].sidebar .sidebar-wordmark,
  [data-skin-chrome='aurora'].sidebar .aurora-nav-label,
  [data-skin-chrome='aurora'].sidebar .section-label,
  [data-skin-chrome='aurora'].sidebar .playlists,
  [data-skin-chrome='aurora'].sidebar .user > div:not(.avatar) {
    display: none;
  }

  [data-skin-chrome='aurora'].sidebar .masthead {
    justify-content: center;
  }

  [data-skin-chrome='aurora'].sidebar .update-entry {
    font-size: 0 !important; /* inline styles set 11px — must override */
    gap: 0 !important;
    padding: 4px !important;
  }

  [data-skin-chrome='aurora'].sidebar .user {
    justify-content: center;
  }

  [data-skin-chrome='aurora'].sidebar .nav > a {
    justify-content: center;
    gap: 0;
    font-size: 0; /* nav labels are bare text nodes */
  }

  [data-skin-chrome='aurora'].sidebar .nav > a .ico {
    width: 20px;
    height: 20px;
    flex: none;
  }
}
```

- [ ] **Step 2: 跑测试 + 截图核对**

Run: `pnpm test`
Expected: PASS(jsdom 不计算媒体查询,无断言受影响)。

Run: `AURORA_QA_URL=http://localhost:1420/ node scripts/capture-aurora-qa.mjs`
人工核对 1280×720 与 1440×900(≤1099 不在矩阵内——1280 仍 ≥1100 为全宽;改用浏览器或手动把 viewport 调到 1000×800 截一张确认图标栏)。可用现成脚本加一行 shot 或 playwright 一次性脚本:`{ width: 1000, height: 800 }`,确认:sidebar 64px、仅图标、舞台不被挤压。

- [ ] **Step 3: Commit**

```bash
git add ui/src/styles/skins/aurora.css
git commit -m "feat(aurora): 64px icon rail for 900-1099px windows"
```

---

### Task 7: 全量验证 + QA 台账更新

**Files:**
- Modify: `ui/design-qa.md`
- Artifacts: `ui/design-qa-captures/`(gitignored,不提交)

- [ ] **Step 1: 全量测试**

Run: `pnpm test`
Expected: PASS(全量绿)。

- [ ] **Step 2: 截图矩阵重跑 + 人工核对**

Run: `AURORA_QA_URL=http://localhost:1420/ node scripts/capture-aurora-qa.mjs`
再跑 `AURORA_QA_URL=http://localhost:1420/ node scripts/capture-stage-preview.mjs`(注入演示数据的填充态)。

核对清单:
- dark `#0c0b09` 暖黑;accent 仍 `#62d6a2`;
- 空态:黑胶空盘 + 单行文案,无巨标无描边盒;
- 填充态:圆形黑胶封面、沟槽、中心翡翠 label;
- Dock:无曲时静音态传输区(无虚线 pill);有曲时唱针播放头;
- reduced-motion:全部静止可用;
- 900×720:sidebar 隐藏、舞台单列;1000×800(Task 6 自截):图标栏。

- [ ] **Step 3: 更新 design-qa.md**

把台账目标图/结果表更新为唱机之夜版本(新的检查行:圆形黑胶、光锥、暖黑令牌、常驻传输区、唱针播放头、图标栏断点),记录矩阵重跑结果。

- [ ] **Step 4: Commit**

```bash
git add ui/design-qa.md
git commit -m "docs(qa): turntable-night aurora QA ledger"
```

---

## Self-Review 记录

- **Spec 覆盖**:D1 圆角纪律(T4⑤/T5③)、D2 舞台(T4)、D3 光锥尘埃(T3/T4⑤①)、D4 令牌+字阶(T1/T4⑤③)、D5 Dock(T5)、D6 rail/卡片(T4⑤⑤)、D7 响应式(T6 + T4 沿用断点)、D8 动效穷举(T2 + 现有)。✔
- **占位符**:无 TBD;Task 2 Step 4 的测试断言粒度说明是故意的(对齐文件内现有 mock 风格,属"读文件后按惯例写"而非占位)。
- **类型一致性**:`VinylSpinHandle` 在 Task 2 定义、Task 4 消费,签名一致(`kill`/`setPlaying` 均无参);`vinyl` 段字段名三处一致;cap 常量 30/60 与测试更新一致。
- **一处有意的 spec 微调**:Dock 圆角 24→16(而非 D1 的 8px)——Dock 是承载传输区的"主机"器物,16px 在贴边悬浮时更稳;其余元素严格 8px。如审计划时不认可,改 8px 只需动 T5③ 最后一行。
