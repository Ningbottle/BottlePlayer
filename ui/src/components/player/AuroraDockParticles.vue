<script setup lang="ts">
/**
 * Compact Canvas 2D motes for the Aurora player dock.
 * Non-audio; denser when playing; cleaned on unmount / reduced-motion.
 */
import { onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { isReducedMotion } from '../../api/motion';

const props = defineProps<{ isPlaying: boolean }>();

const CAP_PAUSED = 28;
const CAP_PLAYING = 48;
const DPR_CAP = 2;

const canvasRef = ref<HTMLCanvasElement | null>(null);

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
let particles: Particle[] = [];
let cssW = 1;
let cssH = 1;
let lastTs = 0;
let alive = true;

function cancelFrame(): void {
  if (frameId !== null) {
    cancelAnimationFrame(frameId);
    frameId = null;
  }
}

function makeParticle(): Particle {
  const playing = props.isPlaying;
  return {
    x: Math.random() * cssW,
    y: Math.random() * cssH,
    vx: (Math.random() - 0.5) * (playing ? 0.28 : 0.1),
    vy: (Math.random() - 0.5) * (playing ? 0.16 : 0.06) - (playing ? 0.05 : 0.02),
    r: 0.5 + Math.random() * (playing ? 2.2 : 1.4),
    baseAlpha: playing ? 0.22 + Math.random() * 0.35 : 0.08 + Math.random() * 0.16,
    phase: Math.random() * Math.PI * 2,
    speed: 0.45 + Math.random() * (playing ? 1.0 : 0.6),
  };
}

function seed(count: number): void {
  particles = Array.from({ length: count }, () => makeParticle());
}

function ensureBudget(): void {
  const target = props.isPlaying ? CAP_PLAYING : CAP_PAUSED;
  while (particles.length < target) particles.push(makeParticle());
  if (particles.length > target) particles.length = target;
}

function syncSize(): void {
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

function paint(ts?: number): void {
  const canvas = canvasRef.value;
  const ctx = canvas?.getContext('2d');
  if (!canvas || !ctx) return;
  syncSize();
  const dpr = Math.min(window.devicePixelRatio || 1, DPR_CAP);
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, cssW, cssH);

  // Soft accent wash along the dock midline
  const a = props.isPlaying ? 0.14 : 0.06;
  const g = ctx.createLinearGradient(0, 0, cssW, 0);
  g.addColorStop(0, 'rgba(94, 226, 165, 0)');
  g.addColorStop(0.35, `rgba(94, 226, 165, ${a * 0.55})`);
  g.addColorStop(0.5, `rgba(120, 200, 255, ${a * 0.35})`);
  g.addColorStop(0.65, `rgba(94, 226, 165, ${a * 0.55})`);
  g.addColorStop(1, 'rgba(94, 226, 165, 0)');
  ctx.fillStyle = g;
  ctx.fillRect(0, 0, cssW, cssH);

  if (isReducedMotion()) return;

  ensureBudget();
  const now = ts ?? performance.now();
  const dt = lastTs ? Math.min(48, now - lastTs) : 16;
  lastTs = now;
  const boost = props.isPlaying ? 1.45 : 1;

  for (const p of particles) {
    p.phase += dt * 0.0015 * p.speed;
    p.x += p.vx * p.speed * (dt * 0.07);
    p.y += p.vy * p.speed * (dt * 0.07);
    if (p.x < -6) p.x = cssW + 6;
    if (p.x > cssW + 6) p.x = -6;
    if (p.y < -4) p.y = cssH + 4;
    if (p.y > cssH + 4) p.y = -4;

    const pulse = 0.55 + 0.45 * Math.sin(p.phase);
    const alpha = Math.min(0.9, p.baseAlpha * pulse * boost);
    const radius = p.r * (props.isPlaying ? 1.15 : 1) * (0.85 + 0.3 * pulse);
    const grad = ctx.createRadialGradient(p.x, p.y, 0, p.x, p.y, radius * 3.2);
    grad.addColorStop(0, `rgba(200, 255, 230, ${alpha})`);
    grad.addColorStop(0.4, `rgba(94, 226, 165, ${alpha * 0.5})`);
    grad.addColorStop(1, 'rgba(94, 226, 165, 0)');
    ctx.fillStyle = grad;
    ctx.beginPath();
    ctx.arc(p.x, p.y, radius * 3.2, 0, Math.PI * 2);
    ctx.fill();
  }
}

function frame(ts: number): void {
  frameId = null;
  if (!alive || document.visibilityState === 'hidden' || isReducedMotion()) return;
  paint(ts);
  frameId = requestAnimationFrame(frame);
}

function startLoop(): void {
  if (!alive) return;
  if (isReducedMotion()) {
    cancelFrame();
    paint();
    return;
  }
  if (document.visibilityState === 'hidden') {
    cancelFrame();
    return;
  }
  if (frameId !== null) return;
  lastTs = 0;
  frameId = requestAnimationFrame(frame);
}

function stopLoop(): void {
  cancelFrame();
}

function onVisibility(): void {
  if (document.visibilityState === 'hidden') stopLoop();
  else startLoop();
}

onMounted(() => {
  alive = true;
  syncSize();
  seed(props.isPlaying ? CAP_PLAYING : CAP_PAUSED);
  paint();
  startLoop();
  if (typeof ResizeObserver !== 'undefined' && canvasRef.value) {
    resizeObserver = new ResizeObserver(() => {
      syncSize();
      if (frameId === null) paint();
    });
    resizeObserver.observe(canvasRef.value);
  }
  document.addEventListener('visibilitychange', onVisibility);
});

onBeforeUnmount(() => {
  alive = false;
  stopLoop();
  document.removeEventListener('visibilitychange', onVisibility);
  if (resizeObserver) {
    resizeObserver.disconnect();
    resizeObserver = null;
  }
  particles = [];
});

watch(
  () => props.isPlaying,
  () => {
    ensureBudget();
    for (const p of particles) {
      if (props.isPlaying) {
        p.baseAlpha = Math.min(0.65, p.baseAlpha * 1.3 + 0.05);
        p.vx *= 1.2;
        p.vy *= 1.15;
      } else {
        p.baseAlpha = Math.max(0.06, p.baseAlpha * 0.82);
        p.vx *= 0.88;
        p.vy *= 0.88;
      }
    }
    startLoop();
  },
);
</script>

<template>
  <canvas
    ref="canvasRef"
    class="aurora-dock-particles"
    data-test="aurora-dock-particles"
    :data-playing="isPlaying"
    aria-hidden="true"
  />
</template>

<style scoped>
.aurora-dock-particles {
  position: absolute;
  inset: 0;
  width: 100%;
  height: 100%;
  border-radius: inherit;
  pointer-events: none;
  z-index: 0;
  opacity: 0.95;
}
</style>
