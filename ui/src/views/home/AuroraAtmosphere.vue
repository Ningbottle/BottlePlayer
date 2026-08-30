<script setup lang="ts">
/**
 * Aurora home atmosphere: a static light cone breathing with loudness.
 * No particles — the mature version is pure light. Canvas 2D only.
 * KeepAlive-safe rAF lifecycle; reduced-motion paints a single static frame.
 */
import {
  onActivated,
  onBeforeUnmount,
  onDeactivated,
  onMounted,
  ref,
  watch,
} from 'vue';
import { isReducedMotion } from '../../api/motion';

const props = withDefaults(defineProps<{ isPlaying: boolean; level?: { value: number } | null; tint?: [number, number, number] | null }>(), {
  level: null,
  tint: null,
});

/** Live loudness 0..1 from the audio monitor; 0 when unwired. */
function energy(): number {
  const v = props.level?.value;
  return typeof v === 'number' && v > 0 ? Math.min(1, v) : 0;
}

const DPR_CAP = 2;

/** Cone apex (fraction of stage size) and axis. Opens down-left from the top-right. */
const CONE = {
  ax: 0.84,
  ay: -0.1,
  angle: Math.PI * (2 / 3), // 120°
  halfSpread: 0.2, // ~11.5°
};

const canvasRef = ref<HTMLCanvasElement | null>(null);
/** 'active' | 'static' | 'stopped' — loop intent for tests and diagnostics. */
const motionState = ref<'active' | 'static' | 'stopped'>('stopped');
const loopRunning = ref(false);

let frameId: number | null = null;
let resizeObserver: ResizeObserver | null = null;
/** False while KeepAlive-deactivated; gates startLoop. */
let treeActive = true;
let cssW = 1;
let cssH = 1;
let lastTs = 0;
let washPhase = 0;
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
  const [r, g, b] = props.tint ?? readAccentRGB();
  return `rgba(${r}, ${g}, ${b}, ${a})`;
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

function currentDpr(): number {
  return Math.min(window.devicePixelRatio || 1, DPR_CAP);
}

/**
 * Backing-store application from a known CSS size. The single place that
 * writes canvas.width/height: pure with respect to layout (no rect reads,
 * no paint, no loop control, no props).
 */
function applyCanvasSize(width: number, height: number, dpr: number): void {
  const canvas = canvasRef.value;
  if (!canvas) return;
  cssW = Math.max(1, width);
  cssH = Math.max(1, height);
  const w = Math.max(1, Math.floor(cssW * dpr));
  const h = Math.max(1, Math.floor(cssH * dpr));
  if (canvas.width !== w || canvas.height !== h) {
    canvas.width = w;
    canvas.height = h;
  }
}

/** Mount-time initial measurement — the ONLY getBoundingClientRect in this file. */
function measureInitialCanvasSize(): void {
  const canvas = canvasRef.value;
  if (!canvas) return;
  const rect = canvas.getBoundingClientRect();
  applyCanvasSize(rect.width, rect.height, currentDpr());
}

/** rAF-safe DPR sync from cached CSS size. Returns the DPR for setTransform. */
function syncDprOnly(): number {
  const dpr = currentDpr();
  applyCanvasSize(cssW, cssH, dpr);
  return dpr;
}

/** Cone wash — apex glow + axis band. Breaths: slow phase + live loudness. */
function paintWash(ctx: CanvasRenderingContext2D, dt: number): void {
  washPhase += dt * 0.0004;
  const breath = props.isPlaying ? 0.02 * (0.5 + 0.5 * Math.sin(washPhase * Math.PI * 2)) : 0;
  const e = energy();
  const apexX = CONE.ax * cssW;
  const apexY = CONE.ay * cssH;
  const maxDim = Math.max(cssW, cssH);

  const g = ctx.createRadialGradient(apexX, apexY, 0, apexX, apexY, maxDim * 0.5);
  g.addColorStop(0, accentRGBA((props.isPlaying ? 0.1 : 0.05) + breath + e * 0.06));
  g.addColorStop(1, accentRGBA(0));
  ctx.fillStyle = g;
  ctx.fillRect(0, 0, cssW, cssH);

  ctx.save();
  ctx.translate(apexX, apexY);
  ctx.rotate(CONE.angle);
  const band = ctx.createLinearGradient(0, 0, maxDim, 0);
  band.addColorStop(0, accentRGBA((props.isPlaying ? 0.06 : 0.03) + breath * 0.5 + e * 0.03));
  band.addColorStop(1, accentRGBA(0));
  ctx.fillStyle = band;
  const halfW = Math.tan(CONE.halfSpread) * maxDim;
  ctx.fillRect(0, -halfW, maxDim, halfW * 2);
  ctx.restore();
}

function paint(ts?: number): void {
  const canvas = canvasRef.value;
  const ctx = canvas?.getContext('2d');
  if (!canvas || !ctx) return;

  const dpr = syncDprOnly();
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, cssW, cssH);

  const now = ts ?? performance.now();
  const dt = lastTs ? Math.min(48, now - lastTs) : 16;
  lastTs = now;
  paintWash(ctx, isReducedMotion() ? 0 : dt);
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
    resizeObserver = new ResizeObserver((entries) => {
      // Size must be updated BEFORE any reduced/hidden/inactive early return,
      // from the entry's contentRect (never a layout read).
      const entry = entries[0];
      if (entry) {
        applyCanvasSize(
          entry.contentRect.width,
          entry.contentRect.height,
          currentDpr(),
        );
      } else {
        syncDprOnly();
      }

      if (isReducedMotion() || !treeActive || document.visibilityState === 'hidden') {
        paint();
        return;
      }
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
  measureInitialCanvasSize();
  attachObservers();
  // Always paint once so the canvas is never empty on first frame.
  // paint() is DPR-only now, so mount reads layout exactly once (above).
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
});

watch(
  () => props.isPlaying,
  () => {
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
