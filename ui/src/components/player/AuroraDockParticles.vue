<script setup lang="ts">
/**
 * Dock particles linked to playback progress (no audio analyser).
 * - progress 0–1 moves the bright band and phase along the bar
 * - isPlaying densifies / speeds motes; paused is calm
 */
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { isReducedMotion } from '../../api/motion';
import { getMotionProfile } from '../../api/motionProfiles';

const props = withDefaults(
  defineProps<{
    isPlaying: boolean;
    /** 0–1 song progress; drives wave position */
    progress?: number;
  }>(),
  { progress: 0 },
);

const CAP_PAUSED = 32;
const CAP_PLAYING = 44;
const DPR_CAP = 2;
const dockMotion = getMotionProfile('aurora').particles.dock;

const canvasRef = ref<HTMLCanvasElement | null>(null);
const particleCap = computed(() => (props.isPlaying ? CAP_PLAYING : CAP_PAUSED));

type Particle = {
  x: number;
  y: number;
  vx: number;
  vy: number;
  r: number;
  baseAlpha: number;
  phase: number;
  speed: number;
  /** Prefer clustering near this relative X (0–1) when seeded */
  anchor: number;
};

let frameId: number | null = null;
let resizeObserver: ResizeObserver | null = null;
let particles: Particle[] = [];
let cssW = 1;
let cssH = 1;
let lastTs = 0;
let alive = true;
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
  return `rgba(${Math.min(255, r + 80)}, ${Math.min(255, g + 80)}, ${Math.min(255, b + 80)}, ${a})`;
}

function clamp01(n: number): number {
  if (!Number.isFinite(n)) return 0;
  return Math.max(0, Math.min(1, n));
}

function cancelFrame(): void {
  if (frameId !== null) {
    cancelAnimationFrame(frameId);
    frameId = null;
  }
}

function makeParticle(nearProgress = false): Particle {
  const playing = props.isPlaying;
  const p = clamp01(props.progress);
  const velocity = playing ? dockMotion.velocity.playing : dockMotion.velocity.paused;
  const verticalVelocity = playing
    ? dockMotion.verticalVelocity.playing
    : dockMotion.verticalVelocity.paused;
  const speedRange = playing ? dockMotion.speed.playing : dockMotion.speed.paused;
  // Cluster some motes around the playhead when a track is active
  const anchor = nearProgress
    ? Math.max(0, Math.min(1, p + (Math.random() - 0.5) * 0.22))
    : Math.random();
  return {
    x: anchor * Math.max(cssW, 1),
    y: Math.random() * cssH,
    vx: (Math.random() - 0.5) * velocity,
    vy: (Math.random() - 0.5) * verticalVelocity - verticalVelocity * 0.35,
    r: 0.55 + Math.random() * (playing ? 2.4 : 1.5),
    baseAlpha: playing ? 0.24 + Math.random() * 0.38 : 0.08 + Math.random() * 0.16,
    phase: Math.random() * Math.PI * 2,
    speed: dockMotion.speedBase + Math.random() * speedRange,
    anchor,
  };
}

function seed(count: number): void {
  particles = [];
  for (let i = 0; i < count; i++) {
    particles.push(makeParticle(i % 3 !== 0));
  }
}

function ensureBudget(): void {
  const target = particleCap.value;
  while (particles.length < target) particles.push(makeParticle(true));
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

  const prog = clamp01(props.progress);
  const playheadX = prog * cssW;
  const a = props.isPlaying ? 0.16 : 0.05;

  // Progress-linked wash: glow rides the playhead
  const g = ctx.createRadialGradient(
    playheadX,
    cssH * 0.55,
    0,
    playheadX,
    cssH * 0.55,
    Math.max(cssW * 0.28, 80),
  );
  g.addColorStop(0, accentRGBA(a));
  g.addColorStop(0.45, accentRGBA(a * 0.4));
  g.addColorStop(1, accentRGBA(0));
  ctx.fillStyle = g;
  ctx.fillRect(0, 0, cssW, cssH);

  // Soft floor strip intensity scales with progress (song further = wider breath)
  const band = ctx.createLinearGradient(0, 0, cssW, 0);
  const edge = Math.max(0.02, prog);
  band.addColorStop(0, accentRGBA(0));
  band.addColorStop(Math.max(0, edge - 0.08), accentRGBA(0));
  band.addColorStop(edge, accentRGBA(props.isPlaying ? 0.1 : 0.04));
  band.addColorStop(Math.min(1, edge + 0.06), accentRGBA(0));
  band.addColorStop(1, accentRGBA(0));
  ctx.fillStyle = band;
  ctx.fillRect(0, cssH * 0.35, cssW, cssH * 0.45);

  ensureBudget();
  const now = ts ?? performance.now();
  const dt = lastTs ? Math.min(48, now - lastTs) : 16;
  lastTs = now;

  // Progress advances global phase so motes “breathe” with the song clock
  const progressPhase = prog * Math.PI * 4;
  const boost = props.isPlaying ? dockMotion.boost.playing : dockMotion.boost.paused;
  const pull = props.isPlaying ? dockMotion.pull.playing : dockMotion.pull.paused;
  const phaseRate = dockMotion.phaseRate;
  const progressPhaseRate = dockMotion.progressPhaseRate;
  const radiusScale = props.isPlaying ? dockMotion.radiusScale.playing : dockMotion.radiusScale.paused;

  for (const p of particles) {
    if (!isReducedMotion()) {
      // Soft attraction toward current playhead (progress-linked)
      const targetX = p.anchor * 0.35 * cssW + playheadX * 0.65;
      p.vx += (targetX - p.x) * pull * (dt * 0.06);
      p.vx *= 0.98;
      p.phase += dt * phaseRate * p.speed + progressPhase * progressPhaseRate;
      p.x += p.vx * p.speed * (dt * 0.07);
      p.y += p.vy * p.speed * (dt * 0.07);
      if (p.x < -8) p.x = cssW + 8;
      if (p.x > cssW + 8) p.x = -8;
      if (p.y < -4) p.y = cssH + 4;
      if (p.y > cssH + 4) p.y = -4;
    }

    const pulse = 0.55 + 0.45 * Math.sin(p.phase + progressPhase);
    // Brighter near playhead
    const dist = Math.abs(p.x - playheadX) / Math.max(cssW * 0.25, 40);
    const near = Math.max(0, 1 - dist);
    const alpha = Math.min(0.92, p.baseAlpha * pulse * boost * (0.55 + near * 0.65));
    const radius = p.r * radiusScale * (0.85 + 0.3 * pulse) * (0.85 + near * 0.35);

    const grad = ctx.createRadialGradient(p.x, p.y, 0, p.x, p.y, radius * 3.4);
    grad.addColorStop(0, accentLightRGBA(alpha));
    grad.addColorStop(0.4, accentRGBA(alpha * 0.55));
    grad.addColorStop(1, accentRGBA(0));
    ctx.fillStyle = grad;
    ctx.beginPath();
    ctx.arc(p.x, p.y, radius * 3.4, 0, Math.PI * 2);
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
  () => [props.isPlaying, props.progress] as const,
  () => {
    ensureBudget();
    // Keep loop alive so progress updates repaint even when paused (slow drift)
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
    :data-particle-cap="particleCap"
    :data-progress="Number(progress ?? 0).toFixed(3)"
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
