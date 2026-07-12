<script setup lang="ts">
/**
 * Aurora home atmosphere: Canvas 2D soft particles + wash.
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

/** Higher caps = more visible motes when playing (still capped for cost). */
const CAP_PAUSED = 100;
const CAP_PLAYING = 180;
const DPR_CAP = 2;

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

function makeParticle(): Particle {
  const playing = props.isPlaying;
  return {
    x: Math.random() * cssW,
    y: Math.random() * cssH,
    vx: (Math.random() - 0.5) * (playing ? 0.32 : 0.14),
    vy: (Math.random() - 0.5) * (playing ? 0.26 : 0.1) - (playing ? 0.07 : 0.025),
    r: 0.7 + Math.random() * (playing ? 3.2 : 2.0),
    baseAlpha: playing ? 0.28 + Math.random() * 0.42 : 0.12 + Math.random() * 0.26,
    phase: Math.random() * Math.PI * 2,
    speed: 0.5 + Math.random() * (playing ? 1.15 : 0.85),
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

function paintWash(ctx: CanvasRenderingContext2D): void {
  const a = props.isPlaying ? 0.24 : 0.1;
  const g = ctx.createRadialGradient(
    cssW * 0.72,
    cssH * 0.22,
    0,
    cssW * 0.72,
    cssH * 0.22,
    Math.max(cssW, cssH) * 0.62,
  );
  g.addColorStop(0, `rgba(94, 226, 165, ${a})`);
  g.addColorStop(0.55, `rgba(72, 180, 200, ${a * 0.4})`);
  g.addColorStop(1, 'rgba(94, 226, 165, 0)');
  ctx.fillStyle = g;
  ctx.fillRect(0, 0, cssW, cssH);

  // Secondary cool wash (lower left) for depth without frost overlay on text.
  const g2 = ctx.createRadialGradient(
    cssW * 0.18,
    cssH * 0.75,
    0,
    cssW * 0.18,
    cssH * 0.75,
    Math.max(cssW, cssH) * 0.48,
  );
  const a2 = props.isPlaying ? 0.12 : 0.05;
  g2.addColorStop(0, `rgba(120, 160, 255, ${a2})`);
  g2.addColorStop(1, 'rgba(120, 160, 255, 0)');
  ctx.fillStyle = g2;
  ctx.fillRect(0, 0, cssW, cssH);
}

function paintParticles(ctx: CanvasRenderingContext2D, dt: number): void {
  ensureParticleBudget();
  const alphaBoost = props.isPlaying ? 1.55 : 1;
  const sizeBoost = props.isPlaying ? 1.2 : 1;
  for (const p of particles) {
    p.phase += dt * 0.0014 * p.speed;
    p.x += p.vx * p.speed * (dt * 0.07);
    p.y += p.vy * p.speed * (dt * 0.07);
    // Soft wrap
    if (p.x < -8) p.x = cssW + 8;
    if (p.x > cssW + 8) p.x = -8;
    if (p.y < -8) p.y = cssH + 8;
    if (p.y > cssH + 8) p.y = -8;

    const pulse = 0.6 + 0.4 * Math.sin(p.phase);
    const alpha = Math.min(0.95, p.baseAlpha * pulse * alphaBoost);
    const radius = p.r * sizeBoost * (0.9 + 0.3 * pulse);

    const grad = ctx.createRadialGradient(p.x, p.y, 0, p.x, p.y, radius * 3.6);
    grad.addColorStop(0, `rgba(200, 255, 230, ${alpha})`);
    grad.addColorStop(0.35, `rgba(94, 226, 165, ${alpha * 0.6})`);
    grad.addColorStop(1, 'rgba(94, 226, 165, 0)');
    ctx.fillStyle = grad;
    ctx.beginPath();
    ctx.arc(p.x, p.y, radius * 3.6, 0, Math.PI * 2);
    ctx.fill();
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
    // Softly retune velocities/alphas for existing motes without multi-loop.
    for (const p of particles) {
      if (props.isPlaying) {
        p.baseAlpha = Math.min(0.72, p.baseAlpha * 1.35 + 0.06);
        p.r = Math.min(4.2, p.r * 1.12);
        p.vx *= 1.25;
        p.vy *= 1.18;
      } else {
        p.baseAlpha = Math.max(0.08, p.baseAlpha * 0.8);
        p.vx *= 0.88;
        p.vy *= 0.88;
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
