<script setup lang="ts">
/**
 * Aurora home atmosphere: Canvas 2D cone-lit dust + static wash.
 * Non-audio, non-WebGL. KeepAlive-safe rAF lifecycle.
 */
import {
  computed,
  onActivated,
  onBeforeUnmount,
  onDeactivated,
  onMounted,
  ref,
  watch,
} from 'vue';
import { isReducedMotion } from '../../api/motion';

const props = defineProps<{ isPlaying: boolean }>();

/** Turntable night: dust motes inside a static light cone. Fewer, smaller, calmer. */
const CAP_PAUSED = 30;
const CAP_PLAYING = 60;
const DPR_CAP = 2;

/** Cone apex (fraction of stage size) and axis. Opens down-left from the top-right. */
const CONE = {
  ax: 0.84,
  ay: -0.1,
  angle: Math.PI * (2 / 3), // 120°
  halfSpread: 0.2, // ~11.5°
};

const canvasRef = ref<HTMLCanvasElement | null>(null);
/** Target particle budget exposed for tests / QA. */
const particleCap = computed(() => (props.isPlaying ? CAP_PLAYING : CAP_PAUSED));
/** 'active' | 'static' | 'stopped' — loop intent for tests and diagnostics. */
const motionState = ref<'active' | 'static' | 'stopped'>('stopped');
const loopRunning = ref(false);

type Particle = {
  x: number;
  y: number;
  vx: number;
  vy: number;
  r: number;
  baseAlpha: number;
  phase: number;
  speed: number;
};

let frameId: number | null = null;
let resizeObserver: ResizeObserver | null = null;
/** False while KeepAlive-deactivated; gates startLoop. */
let treeActive = true;
let particles: Particle[] = [];
let cssW = 1;
let cssH = 1;
let lastTs = 0;
let cachedAccent: [number, number, number] = [94, 226, 165];
let accentCheckFrame = 0;

function readAccentRGB(): [number, number, number] {
  if (accentCheckFrame++ % 30 !== 0) return cachedAccent;
  const raw = getComputedStyle(document.documentElement).getPropertyValue('--accent').trim();
  if (raw.startsWith('#') && raw.length >= 7) {
    const r = parseInt(raw.slice(1, 3), 16);
    const g = parseInt(raw.slice(3, 5), 16);
    const b = parseInt(raw.slice(5, 7), 16);
    if (Number.isFinite(r) && Number.isFinite(g) && Number.isFinite(b)) {
      cachedAccent = [r, g, b];
    }
  }
  return cachedAccent;
}

function accentRGBA(a: number): string {
  const [r, g, b] = readAccentRGB();
  return `rgba(${r}, ${g}, ${b}, ${a})`;
}

function accentLightRGBA(a: number): string {
  const [r, g, b] = readAccentRGB();
  const lr = Math.min(255, r + 80);
  const lg = Math.min(255, g + 80);
  const lb = Math.min(255, b + 80);
  return `rgba(${lr}, ${lg}, ${lb}, ${a})`;
}

function cancelFrame(): void {
  if (frameId !== null) {
    cancelAnimationFrame(frameId);
    frameId = null;
  }
  loopRunning.value = false;
}

function stopLoop(): void {
  cancelFrame();
  if (motionState.value === 'active') {
    motionState.value = 'stopped';
  }
}

function syncCanvasSize(): void {
  const canvas = canvasRef.value;
  if (!canvas) return;
  const rect = canvas.getBoundingClientRect();
  cssW = Math.max(1, rect.width);
  cssH = Math.max(1, rect.height);
  const dpr = Math.min(window.devicePixelRatio || 1, DPR_CAP);
  const w = Math.max(1, Math.floor(cssW * dpr));
  const h = Math.max(1, Math.floor(cssH * dpr));
  if (canvas.width !== w || canvas.height !== h) {
    canvas.width = w;
    canvas.height = h;
  }
}

function seedParticles(count: number): void {
  particles = [];
  for (let i = 0; i < count; i++) {
    particles.push(makeParticle());
  }
}

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

function ensureParticleBudget(): void {
  const target = particleCap.value;
  if (particles.length < target) {
    while (particles.length < target) particles.push(makeParticle());
  } else if (particles.length > target) {
    particles.length = target;
  }
}

/** Static cone wash — painted every frame incl. reduced-motion. */
function paintWash(ctx: CanvasRenderingContext2D): void {
  const apexX = CONE.ax * cssW;
  const apexY = CONE.ay * cssH;
  const maxDim = Math.max(cssW, cssH);

  // Apex glow
  const g = ctx.createRadialGradient(apexX, apexY, 0, apexX, apexY, maxDim * 0.5);
  g.addColorStop(0, accentRGBA(props.isPlaying ? 0.1 : 0.05));
  g.addColorStop(1, accentRGBA(0));
  ctx.fillStyle = g;
  ctx.fillRect(0, 0, cssW, cssH);

  // Axis band: soft gradient along the cone
  ctx.save();
  ctx.translate(apexX, apexY);
  ctx.rotate(CONE.angle);
  const band = ctx.createLinearGradient(0, 0, maxDim, 0);
  band.addColorStop(0, accentRGBA(props.isPlaying ? 0.06 : 0.03));
  band.addColorStop(1, accentRGBA(0));
  ctx.fillStyle = band;
  const halfW = Math.tan(CONE.halfSpread) * maxDim;
  ctx.fillRect(0, -halfW, maxDim, halfW * 2);
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

function paint(ts?: number): void {
  const canvas = canvasRef.value;
  const ctx = canvas?.getContext('2d');
  if (!canvas || !ctx) return;

  syncCanvasSize();
  const dpr = Math.min(window.devicePixelRatio || 1, DPR_CAP);
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, cssW, cssH);
  paintWash(ctx);

  if (isReducedMotion()) {
    return;
  }

  const now = ts ?? performance.now();
  const dt = lastTs ? Math.min(48, now - lastTs) : 16;
  lastTs = now;
  paintParticles(ctx, dt);
}

function frame(ts: number): void {
  frameId = null;
  if (!treeActive || document.visibilityState === 'hidden' || isReducedMotion()) {
    loopRunning.value = false;
    return;
  }
  paint(ts);
  frameId = requestAnimationFrame(frame);
  loopRunning.value = true;
  motionState.value = 'active';
}

function startLoop(): void {
  if (!treeActive) return;
  if (isReducedMotion()) {
    cancelFrame();
    motionState.value = 'static';
    lastTs = 0;
    paint();
    return;
  }
  if (document.visibilityState === 'hidden') {
    cancelFrame();
    motionState.value = 'stopped';
    return;
  }
  // Idempotent: never stack concurrent rAF loops.
  if (frameId !== null) return;
  motionState.value = 'active';
  lastTs = 0;
  frameId = requestAnimationFrame(frame);
  loopRunning.value = true;
}

function onVisibilityChange(): void {
  if (document.visibilityState === 'hidden') {
    stopLoop();
    return;
  }
  if (treeActive) {
    startLoop();
  }
}

function attachObservers(): void {
  const canvas = canvasRef.value;
  if (!canvas) return;
  if (!resizeObserver && typeof ResizeObserver !== 'undefined') {
    resizeObserver = new ResizeObserver(() => {
      // Resize only; do not start extra loops.
      if (isReducedMotion() || !treeActive || document.visibilityState === 'hidden') {
        paint();
        return;
      }
      // Keep size in sync; running loop will paint next frame.
      syncCanvasSize();
      if (frameId === null) {
        startLoop();
      }
    });
    resizeObserver.observe(canvas);
  }
  document.addEventListener('visibilitychange', onVisibilityChange);
}

function detachObservers(): void {
  if (resizeObserver) {
    resizeObserver.disconnect();
    resizeObserver = null;
  }
  document.removeEventListener('visibilitychange', onVisibilityChange);
}

function boot(): void {
  treeActive = true;
  syncCanvasSize();
  seedParticles(particleCap.value);
  attachObservers();
  // Always paint once so the canvas is never empty on first frame.
  paint();
  startLoop();
}

// Reflect reduced-motion before first paint (onMounted runs after initial render).
if (isReducedMotion()) {
  motionState.value = 'static';
}

onMounted(() => {
  boot();
});

onActivated(() => {
  treeActive = true;
  ensureParticleBudget();
  startLoop();
});

onDeactivated(() => {
  treeActive = false;
  stopLoop();
});

onBeforeUnmount(() => {
  treeActive = false;
  stopLoop();
  detachObservers();
  particles = [];
});

watch(
  () => props.isPlaying,
  () => {
    ensureParticleBudget();
    // Softly retune alphas for existing motes without multi-loop.
    for (const p of particles) {
      if (props.isPlaying) {
        p.baseAlpha = Math.min(0.3, p.baseAlpha * 1.3 + 0.03);
      } else {
        p.baseAlpha = Math.max(0.05, p.baseAlpha * 0.8);
      }
    }
    if (treeActive && !isReducedMotion() && document.visibilityState !== 'hidden') {
      startLoop();
    } else if (isReducedMotion()) {
      paint();
    }
  },
);
</script>

<template>
  <canvas
    ref="canvasRef"
    class="aurora-atmosphere"
    data-test="aurora-atmosphere"
    :data-playing="isPlaying"
    :data-particle-cap="particleCap"
    :data-loop="loopRunning ? '1' : '0'"
    :data-motion="motionState"
    aria-hidden="true"
  />
</template>

<style scoped>
.aurora-atmosphere {
  position: absolute;
  inset: 0;
  width: 100%;
  height: 100%;
  pointer-events: none;
  z-index: 0;
  opacity: 1;
  mix-blend-mode: normal;
}
</style>
