/**
 * audioLevelMonitor.ts — always-on loudness tap for audio-reactive motion.
 *
 * Mineradio's dj-analyzer idea, in-house: instead of decoding offline, we tap
 * the live <audio> element via captureStream → AnalyserNode (analysis only,
 * never connected to destination, so the playback path is untouched). The EQ
 * AudioWorklet graph (webAudioEq.ts) uses its own stream/context and is
 * unaffected.
 *
 * The WebAudio graph is a MODULE-LEVEL SINGLETON: creating or closing an
 * AudioContext can audibly blip the output device, so the graph is built once
 * per audio element and NEVER closed. start/stop only toggles the rAF
 * sampling loop — navigating between pages must not touch the device.
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
  /** Stops sampling. The shared graph stays alive (no device churn). */
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

// ── Shared graph (built once per audio element, never closed) ──
let sharedCtx: AudioContext | null = null;
let sharedAnalyser: AnalyserNode | null = null;
let sharedSource: MediaStreamAudioSourceNode | null = null;
let sharedAudio: CapturableAudio | null = null;
let sharedSamples: Uint8Array | null = null;

function ensureGraph(audio: CapturableAudio): boolean {
  if (sharedAnalyser && sharedAudio === audio) return true;

  const capture = audio.captureStream ?? audio.mozCaptureStream;
  if (typeof capture !== 'function' || typeof AudioContext === 'undefined') {
    return false;
  }

  try {
    if (!sharedCtx) {
      sharedCtx = new AudioContext();
    }
    if (sharedSource) {
      sharedSource.disconnect();
      sharedSource = null;
    }
    sharedSource = sharedCtx.createMediaStreamSource(capture.call(audio));
    if (!sharedAnalyser) {
      sharedAnalyser = sharedCtx.createAnalyser();
      sharedAnalyser.fftSize = 256;
      sharedAnalyser.smoothingTimeConstant = 0.6;
    }
    sharedSource.connect(sharedAnalyser); // analysis only — never to destination
    sharedSamples = new Uint8Array(sharedAnalyser.fftSize);
    sharedAudio = audio;
    return true;
  } catch {
    sharedSource = null;
    sharedAnalyser = null;
    sharedSamples = null;
    sharedAudio = null;
    return false;
  }
}

export function createAudioLevelMonitor(audio: CapturableAudio): AudioLevelMonitor {
  const level = ref(0);
  let frameId: number | null = null;
  let started = false;
  let smoothed = 0;

  function tick(): void {
    frameId = null;
    if (!sharedAnalyser || !sharedSamples) return;

    if (isReducedMotion() || audio.paused || document.hidden) {
      smoothed = 0;
    } else {
      sharedAnalyser.getByteTimeDomainData(sharedSamples);
      const rms = computeRms(sharedSamples);
      // Fast attack, slow decay — dust should flare on beats, settle gently.
      smoothed += (rms - smoothed) * (rms > smoothed ? 0.3 : 0.08);
    }
    level.value = smoothed;

    frameId = requestAnimationFrame(tick);
  }

  function start(): void {
    if (started) return;
    started = true;
    if (!ensureGraph(audio)) return;
    if (sharedCtx && sharedCtx.state === 'suspended') {
      void sharedCtx.resume().catch(() => {});
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
    smoothed = 0;
    level.value = 0;
    // NB: the shared context/analyser stay alive by design — closing them
    // would blip the output device on every page navigation.
  }

  return {
    level,
    getAnalyser: () => (sharedAudio === audio ? sharedAnalyser : null),
    start,
    stop,
  };
}
