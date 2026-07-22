<script setup lang="ts">
/**
 * VisualizerView — 光谱地平线 (Spectral Horizon)
 *
 * Full-stage ring spectrum around the turntable disc. Audio data comes from
 * the same always-on analyser tap as the home cone dust (audioLevelMonitor) —
 * analysis only, never in the playback path. Paused / no audio / unsupported →
 * calm idle frame. Reduced motion → static frame, no loop.
 */
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { playerStore } from '../api/playerStore';
import { createAudioLevelMonitor, type AudioLevelMonitor } from '../api/audioLevelMonitor';
import { isReducedMotion, startVinylSpin } from '../api/motion';
import type { VinylSpinHandle } from '../api/motion';

const BARS = 64;
const DPR_CAP = 2;

const canvasRef = ref<HTMLCanvasElement | null>(null);
const discEl = ref<HTMLElement | null>(null);

const currentTrack = computed(() => playerStore.currentTrack);
const coverUrl = computed(() => currentTrack.value?.Image || '');

let monitor: AudioLevelMonitor | null = null;
let vinylSpin: VinylSpinHandle | null = null;
let frameId: number | null = null;
let resizeObserver: ResizeObserver | null = null;
let cssW = 1;
let cssH = 1;
let freq: Uint8Array | null = null;
let cachedAccent: [number, number, number] = [98, 214, 162];
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

function syncCanvasSize(): void {
  const canvas = canvasRef.value;
  if (!canvas) return;
  const rect = canvas.getBoundingClientRect();
  cssW = Math.max(1, rect.width);
  cssH = Math.max(1, rect.height);
  const dpr = Math.min(window.devicePixelRatio || 1, DPR_CAP);
  const w = Math.max(1, Math.floor(cssW * dpr));
  const h = Math.max(1, Math.floor(cssH * dpr));
  if (canvas.width !== w) canvas.width = w;
  if (canvas.height !== h) canvas.height = h;
}

function paint(): void {
  const canvas = canvasRef.value;
  const ctx = canvas?.getContext('2d');
  if (!canvas || !ctx) return;

  syncCanvasSize();
  const dpr = Math.min(window.devicePixelRatio || 1, DPR_CAP);
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, cssW, cssH);

  const analyser = monitor?.getAnalyser() ?? null;
  if (analyser) {
    if (!freq || freq.length !== analyser.frequencyBinCount) {
      freq = new Uint8Array(analyser.frequencyBinCount);
    }
    analyser.getByteFrequencyData(freq);
  }

  const cx = cssW / 2;
  const cy = cssH / 2;
  const base = Math.min(cssW, cssH);
  const r0 = base * 0.24; // just outside the disc
  const maxLen = base * 0.17;
  const playing = playerStore.isPlaying && !!analyser;

  // Baseline ring — the horizon the spectrum rises from
  ctx.beginPath();
  ctx.arc(cx, cy, r0, 0, Math.PI * 2);
  ctx.strokeStyle = accentRGBA(playing ? 0.22 : 0.12);
  ctx.lineWidth = 1;
  ctx.stroke();

  for (let i = 0; i < BARS; i++) {
    const v = freq ? freq[Math.floor((i * freq.length * 0.72) / BARS)] / 255 : 0;
    const a = (i / BARS) * Math.PI * 2 - Math.PI / 2;
    const len = 2 + v * v * maxLen;
    const cos = Math.cos(a);
    const sin = Math.sin(a);
    ctx.beginPath();
    ctx.moveTo(cx + cos * r0, cy + sin * r0);
    ctx.lineTo(cx + cos * (r0 + len), cy + sin * (r0 + len));
    ctx.strokeStyle = accentRGBA(0.2 + v * 0.65);
    ctx.lineWidth = 3;
    ctx.lineCap = 'round';
    ctx.stroke();
  }
}

function frame(): void {
  frameId = null;
  if (document.hidden) return;
  paint();
  frameId = requestAnimationFrame(frame);
}

function startLoop(): void {
  if (isReducedMotion()) {
    paint(); // static frame only
    return;
  }
  if (frameId === null && !document.hidden) {
    frameId = requestAnimationFrame(frame);
  }
}

function stopLoop(): void {
  if (frameId !== null) {
    cancelAnimationFrame(frameId);
    frameId = null;
  }
}

function onVisibilityChange(): void {
  if (document.hidden) {
    stopLoop();
  } else {
    startLoop();
  }
}

function bootMonitor(): void {
  if (monitor || !playerStore.audio) return;
  monitor = createAudioLevelMonitor(playerStore.audio);
  monitor.start();
  paint(); // repaint once the analyser exists
}

watch(() => playerStore.audio, () => bootMonitor());
watch(() => playerStore.currentTrack?.FileHash, () => paint());

onMounted(() => {
  syncCanvasSize();
  const canvas = canvasRef.value;
  if (canvas && typeof ResizeObserver !== 'undefined') {
    resizeObserver = new ResizeObserver(() => {
      syncCanvasSize();
      if (isReducedMotion() || frameId === null) paint();
    });
    resizeObserver.observe(canvas);
  }
  document.addEventListener('visibilitychange', onVisibilityChange);

  bootMonitor();
  if (discEl.value) {
    vinylSpin = startVinylSpin(discEl.value, () => !!playerStore.isPlaying);
  }
  startLoop();
});

watch(() => playerStore.isPlaying, () => vinylSpin?.setPlaying());

onBeforeUnmount(() => {
  stopLoop();
  vinylSpin?.kill();
  vinylSpin = null;
  monitor?.stop();
  monitor = null;
  if (resizeObserver) {
    resizeObserver.disconnect();
    resizeObserver = null;
  }
  document.removeEventListener('visibilitychange', onVisibilityChange);
});
</script>

<template>
  <div class="visualizer-page" data-test="visualizer-page">
    <canvas
      ref="canvasRef"
      class="visualizer-canvas"
      data-test="visualizer-canvas"
      aria-hidden="true"
    />

    <div class="visualizer-stage">
      <div class="visualizer-disc" data-test="visualizer-disc">
        <div ref="discEl" class="visualizer-disc-spin">
          <img v-if="coverUrl" :src="coverUrl" alt="封面" />
          <div v-else class="visualizer-disc-empty" aria-hidden="true" />
          <div class="visualizer-disc-grooves" aria-hidden="true" />
        </div>
        <div class="visualizer-disc-spindle" aria-hidden="true" />
      </div>

      <div class="visualizer-meta">
        <span class="visualizer-kicker">SPECTRUM · 可视化</span>
        <template v-if="currentTrack">
          <span class="visualizer-title">{{ currentTrack.SongName }}</span>
          <span class="visualizer-artist">{{ currentTrack.SingerName }}</span>
        </template>
        <span v-else class="visualizer-idle" data-test="visualizer-idle">
          播放一首歌，点亮光谱
        </span>
      </div>
    </div>
  </div>
</template>

<style scoped>
.visualizer-page {
  position: relative;
  width: 100%;
  height: 100%;
  overflow: hidden;
  background: var(--app-bg);
}

.visualizer-canvas {
  position: absolute;
  inset: 0;
  width: 100%;
  height: 100%;
}

.visualizer-stage {
  position: absolute;
  inset: 0;
  display: grid;
  place-items: center;
  pointer-events: none;
}

.visualizer-disc {
  position: relative;
  width: min(30vmin, 260px);
  aspect-ratio: 1;
  border-radius: 50%;
  background: #0a0a09;
  box-shadow:
    0 24px 48px rgba(0, 0, 0, 0.5),
    0 0 0 1px color-mix(in srgb, #fff 5%, transparent);
}

.visualizer-disc-spin {
  position: absolute;
  inset: 0;
  border-radius: 50%;
  overflow: hidden;
  will-change: transform;
}

.visualizer-disc-spin img {
  width: 100%;
  height: 100%;
  object-fit: cover;
  border-radius: 50%;
}

.visualizer-disc-empty {
  width: 100%;
  height: 100%;
  background: #0a0a09;
}

.visualizer-disc-grooves {
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

.visualizer-disc-spindle {
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
}

.visualizer-meta {
  position: absolute;
  left: 50%;
  transform: translateX(-50%);
  bottom: calc(50% - min(30vmin, 260px) / 2 - 76px);
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 4px;
  text-align: center;
  max-width: min(80%, 480px);
}

.visualizer-kicker {
  font-family: 'Inter', 'Microsoft YaHei UI', 'PingFang SC', system-ui, sans-serif;
  font-size: 11px;
  font-weight: 500;
  letter-spacing: 0.12em;
  text-transform: uppercase;
  color: var(--accent);
}

.visualizer-title {
  font-family: Georgia, 'Noto Serif SC', 'Songti SC', serif;
  font-size: 22px;
  font-weight: 700;
  color: var(--text-primary);
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  max-width: 100%;
}

.visualizer-artist {
  font-size: 13px;
  color: var(--text-secondary);
}

.visualizer-idle {
  font-size: 13px;
  color: var(--text-muted);
}
</style>
