/**
 * Media Runtime — the single lifecycle owner of the playback <audio> element,
 * its PlayerBackend instance, and their event subscriptions.
 *
 * Owns: HTMLAudioElement creation/HMR reuse, backend instance, media metadata
 * listeners (durationchange/loadedmetadata/play), backend event subscription.
 * Must NOT own: pagehide policy, queue/Feature state, KuGou routes, UI schema,
 * persistence (see playbackModule responsibility table / plan Task B2).
 *
 * The window slot `__bottlemusic_media_runtime__` is the ONLY published
 * runtime owner — audio/backend owners are no longer published separately.
 * HMR reuse rebinds the SAME element: no pause, no src clear, no load(),
 * no currentTime change (locked by audioLifecycleOwnership tests).
 */
import type { PlayerBackend, PlaybackEvent } from './playerBackend';

export type MediaRuntimeShutdownReason = 'pagehide' | 'shutdown';

export interface MediaRuntimeDeps {
  initialVolume: () => number;
  createBackend: (audio: HTMLAudioElement, initialVolume: number) => PlayerBackend;
  onBackendEvent: (event: PlaybackEvent) => void;
  onDuration: (duration: number) => void;
  onFirstPlay: () => void;
  beforeHmrDetach: () => void;
}

export interface MediaRuntime {
  readonly audio: HTMLAudioElement;
  getBackend(): PlayerBackend | null;
  ensureBackend(): PlayerBackend;
  detachForHmr(): void;
  shutdown(reason: MediaRuntimeShutdownReason): Promise<void>;
}

type BottleMusicMediaGlobal = Window & {
  __bottlemusic_media_runtime__?: MediaRuntime;
};

function mediaGlobal(): BottleMusicMediaGlobal {
  return window as unknown as BottleMusicMediaGlobal;
}

function createMediaRuntime(audio: HTMLAudioElement, deps: MediaRuntimeDeps): MediaRuntime {
  let backend: PlayerBackend | null = null;
  let backendUnsub: (() => void) | null = null;
  let hmrDetached = false;

  const onDurationChange = () => {
    deps.onDuration(audio.duration);
  };
  const onLoadedMetadata = () => {
    deps.onDuration(audio.duration);
  };
  const onPlay = () => {
    deps.onFirstPlay();
  };

  audio.addEventListener('durationchange', onDurationChange);
  audio.addEventListener('loadedmetadata', onLoadedMetadata);
  audio.addEventListener('play', onPlay);

  const removeMediaListeners = () => {
    audio.removeEventListener('durationchange', onDurationChange);
    audio.removeEventListener('loadedmetadata', onLoadedMetadata);
    audio.removeEventListener('play', onPlay);
  };

  const dropBackendRef = () => {
    backendUnsub?.();
    backendUnsub = null;
    backend = null;
  };

  return {
    audio,
    getBackend: () => backend,
    // Coordinator commands read the backend on the same call stack
    // (playTrackCore → switchTrack), so creation must stay synchronous.
    ensureBackend: () => {
      if (!backend) {
        backend = deps.createBackend(audio, deps.initialVolume());
        backendUnsub = backend.onEvent(deps.onBackendEvent);
      }
      return backend;
    },
    detachForHmr: () => {
      if (hmrDetached) return;
      hmrDetached = true;
      // Teardown still owned by Store/composition (queue flush, coordinator
      // detach, FM/EQ/analyser dispose) is captured from the PREVIOUS
      // generation and must run exactly once. Runtime-owned resources (media
      // listeners, backend event subscription, backend ref) are dropped below
      // — the two sides never unsubscribe the same thing twice.
      try {
        deps.beforeHmrDetach();
      } catch {
        /* best-effort teardown */
      }
      removeMediaListeners();
      dropBackendRef();
      // HMR keeps the element audibly intact: no pause, no src removal,
      // no audio.load(), no currentTime change.
    },
    shutdown: async (_reason: MediaRuntimeShutdownReason) => {
      removeMediaListeners();
      const retiring = backend;
      dropBackendRef();
      if (retiring) {
        try {
          await retiring.shutdown();
        } catch {
          /* shutdown best-effort */
        }
      }
    },
  };
}

/**
 * Create (first call) or rebind (HMR) the single global MediaRuntime.
 *
 * Reuse path: detaches the previous runtime — running its captured
 * beforeHmrDetach exactly once and dropping its backend ref WITHOUT pausing
 * the element — then creates a fresh runtime around the SAME audio and
 * replaces the global owner. Callers that never invoke this function
 * (orphan modules) cannot take over the global runtime.
 */
export function getOrCreateMediaRuntime(deps: MediaRuntimeDeps): MediaRuntime {
  const g = mediaGlobal();
  const existing = g.__bottlemusic_media_runtime__;
  if (existing) {
    existing.detachForHmr();
    const runtime = createMediaRuntime(existing.audio, deps);
    g.__bottlemusic_media_runtime__ = runtime;
    return runtime;
  }
  const runtime = createMediaRuntime(new Audio(), deps);
  g.__bottlemusic_media_runtime__ = runtime;
  return runtime;
}

export function getMediaRuntime(): MediaRuntime | null {
  return mediaGlobal().__bottlemusic_media_runtime__ ?? null;
}
