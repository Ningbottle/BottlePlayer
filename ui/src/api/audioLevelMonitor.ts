/**
 * audioLevelMonitor.ts — always-on loudness tap for audio-reactive motion.
 *
 * Mineradio's dj-analyzer idea, in-house: instead of decoding offline, we tap
 * the live <audio> element via captureStream → AnalyserNode (analysis only,
 * never connected to destination, so the playback path is untouched). The EQ
 * AudioWorklet graph (webAudioEq.ts) uses its own stream/context and is
 * unaffected.
 *
 * Fallbacks: no captureStream / no WebAudio (jsdom, old WebView2) → inert
 * monitor whose level stays 0. Reduced motion → level pinned to 0.
 */
import { ref, type Ref } from 'vue';
import { isReducedMotion } from './motion';

export interface AudioLevelMonitor {
  /** Smoothed loudness 0..1 (RMS with attack/decay easing). */
  readonly level: Ref<number>;
  /** The live analyser for spectrum/waveform consumers (null when unavailable). */
  getAnalyser: () => AnalyserNode | null;
  /** Idempotent. Starts the rAF sampling loop when possible. */
  start: () => void;
  /** Stops sampling and releases the audio context. */
  stop: () => void;
}

interface CapturableAudio extends HTMLAudioElement {
  captureStream?: () => MediaStream;
  mozCaptureStream?: () => MediaStream;
}

/** Pure: normalized RMS of byte time-domain samples (128 = silence) → 0..1. */
export function computeRms(samples: Uint8Array): number {
  if (samples.length === 0) return 0;
  let sum = 0;
  for (let i = 0; i < samples.length; i++) {
    const v = (samples[i] - 128) / 128;
    sum += v * v;
  }
  return Math.min(1, Math.sqrt(sum / samples.length) * 2.2);
}

export function createAudioLevelMonitor(audio: CapturableAudio): AudioLevelMonitor {
  const level = ref(0);

  let ctx: AudioContext | null = null;
  let analyser: AnalyserNode | null = null;
  let samples: Uint8Array | null = null;
  let frameId: number | null = null;
  let started = false;
  let smoothed = 0;

  function tick(): void {
    frameId = null;
    if (!analyser || !samples) return;

    if (isReducedMotion() || audio.paused || document.hidden) {
      smoothed = 0;
    } else {
      analyser.getByteTimeDomainData(samples);
      const rms = computeRms(samples);
      // Fast attack, slow decay — dust should flare on beats, settle gently.
      smoothed += (rms - smoothed) * (rms > smoothed ? 0.3 : 0.08);
    }
    level.value = smoothed;

    frameId = requestAnimationFrame(tick);
  }

  function start(): void {
    if (started) return;
    started = true;

    const capture = audio.captureStream ?? audio.mozCaptureStream;
    if (typeof capture !== 'function' || typeof AudioContext === 'undefined') {
      return; // inert fallback — level stays 0
    }

    try {
      ctx = new AudioContext();
      const source = ctx.createMediaStreamSource(capture.call(audio));
      analyser = ctx.createAnalyser();
      analyser.fftSize = 256;
      analyser.smoothingTimeConstant = 0.6;
      source.connect(analyser); // analysis only — never to destination
      samples = new Uint8Array(analyser.fftSize);
    } catch {
      ctx = null;
      analyser = null;
      samples = null;
      return;
    }

    if (frameId === null) {
      frameId = requestAnimationFrame(tick);
    }
  }

  function stop(): void {
    started = false;
    if (frameId !== null) {
      cancelAnimationFrame(frameId);
      frameId = null;
    }
    level.value = 0;
    smoothed = 0;
    analyser = null;
    samples = null;
    if (ctx) {
      void ctx.close().catch(() => {});
      ctx = null;
    }
  }

  return {
    level,
    getAnalyser: () => analyser,
    start,
    stop,
  };
}
