<script setup lang="ts">
/**
 * SpectrumRing — 环形频谱，环绕黑胶唱盘（歌词页 / 可视化融合）。
 * 与首页光锥尘埃共用一路 AnalyserNode 只读分接（audioLevelMonitor），
 * 无音频/不支持/reduced-motion 时退化为静态基线环。
 */
import { onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { playerStore } from '../../api/playerStore';
import { createAudioLevelMonitor, type AudioLevelMonitor } from '../../api/audioLevelMonitor';
import { isReducedMotion } from '../../api/motion';

const props = defineProps<{ isPlaying: boolean }>();

const BARS = 64;
const DPR_CAP = 2;

const canvasRef = ref<HTMLCanvasElement | null>(null);

let monitor: AudioLevelMonitor | null = null;
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
  const r0 = base * 0.43; // just outside the disc edge
  const maxLen = base * 0.09;
  const playing = props.isPlaying && !!analyser;

  ctx.beginPath();
  ctx.arc(cx, cy, r0, 0, Math.PI * 2);
  ctx.strokeStyle = accentRGBA(playing ? 0.2 : 0.1);
  ctx.lineWidth = 1;
  ctx.stroke();

  if (!playing && !freq) {
    // idle ticks — the ring reads as a physical dial, not dead space
    for (let i = 0; i < BARS; i++) {
      const a = (i / BARS) * Math.PI * 2 - Math.PI / 2;
      const cos = Math.cos(a);
      const sin = Math.sin(a);
      ctx.beginPath();
      ctx.moveTo(cx + cos * r0, cy + sin * r0);
      ctx.lineTo(cx + cos * (r0 + 3), cy + sin * (r0 + 3));
      ctx.strokeStyle = accentRGBA(0.14);
      ctx.lineWidth = 2;
      ctx.lineCap = 'round';
      ctx.stroke();
    }
    return;
  }

  for (let i = 0; i < BARS; i++) {
    const v = freq ? freq[Math.floor((i * freq.length * 0.72) / BARS)] / 255 : 0;
    const a = (i / BARS) * Math.PI * 2 - Math.PI / 2;
    const len = 2 + v * v * maxLen;
    const cos = Math.cos(a);
    const sin = Math.sin(a);
    ctx.beginPath();
    ctx.moveTo(cx + cos * r0, cy + sin * r0);
    ctx.lineTo(cx + cos * (r0 + len), cy + sin * (r0 + len));
    ctx.strokeStyle = accentRGBA(0.18 + v * 0.6);
    ctx.lineWidth = 2.5;
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
    paint();
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
  if (document.hidden) stopLoop();
  else startLoop();
}

function bootMonitor(): void {
  if (monitor || !playerStore.audio) return;
  monitor = createAudioLevelMonitor(playerStore.audio);
  monitor.start();
  paint();
}

watch(() => playerStore.audio, () => bootMonitor());
watch(() => props.isPlaying, () => {
  if (isReducedMotion()) paint();
});

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
  startLoop();
});

onBeforeUnmount(() => {
  stopLoop();
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
  <canvas
    ref="canvasRef"
    class="spectrum-ring"
    data-test="spectrum-ring"
    aria-hidden="true"
  />
</template>

<style scoped>
.spectrum-ring {
  position: absolute;
  inset: -11%;
  width: 122%;
  height: 122%;
  pointer-events: none;
  z-index: 0;
}
</style>
