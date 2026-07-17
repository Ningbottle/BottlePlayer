/**
 * Frontend bridge for OsMediaSession (T1a).
 * Mirrors playerStore → Tauri commands; routes OS buttons to existing controls.
 */
import { watch, type WatchStopHandle } from 'vue';
import { invoke } from '@tauri-apps/api/core';
import { listen, type UnlistenFn } from '@tauri-apps/api/event';
import { playerStore, togglePlay, next, prev } from './playerStore';

export type OsMediaButton = 'Play' | 'Pause' | 'PlayPause' | 'Next' | 'Prev';

export type OsMediaBridgeDeps = {
  invoke: typeof invoke;
  listen: typeof listen;
  getTrack: () => {
    title: string;
    artist: string;
    album?: string;
    artworkUrl?: string;
  } | null;
  getIsPlaying: () => boolean;
  getHasQueuePrev: () => boolean;
  getHasQueueNext: () => boolean;
  togglePlay: () => void | Promise<unknown>;
  next: () => void | Promise<unknown>;
  prev: () => void | Promise<unknown>;
};

const defaultDeps = (): OsMediaBridgeDeps => ({
  invoke,
  listen,
  getTrack: () => {
    const t = playerStore.currentTrack;
    if (!t) return null;
    return {
      title: t.SongName || 'Unknown',
      artist: t.SingerName || 'Unknown',
      album: t.AlbumName,
      artworkUrl: t.Image,
    };
  },
  getIsPlaying: () => playerStore.isPlaying,
  getHasQueuePrev: () =>
    playerStore.queue.length > 0 && playerStore.currentIndex > 0,
  getHasQueueNext: () =>
    playerStore.queue.length > 0
    && playerStore.currentIndex >= 0
    && playerStore.currentIndex < playerStore.queue.length - 1,
  togglePlay,
  next,
  prev,
});

let stopWatch: WatchStopHandle | null = null;
let unlistenButton: UnlistenFn | null = null;
let bound = false;
let activeDeps: OsMediaBridgeDeps | null = null;

async function pushState(deps: OsMediaBridgeDeps) {
  const track = deps.getTrack();
  if (track) {
    await deps.invoke('os_media_set_now_playing', {
      nowPlaying: {
        title: track.title,
        artist: track.artist,
        album: track.album ?? null,
        artwork_url: track.artworkUrl ?? null,
      },
    });
  }
  await deps.invoke('os_media_set_playback_status', {
    status: deps.getIsPlaying() ? 'Playing' : track ? 'Paused' : 'Stopped',
  });
  await deps.invoke('os_media_set_enabled_controls', {
    controls: {
      play_pause: !!track,
      next: deps.getHasQueueNext(),
      prev: deps.getHasQueuePrev(),
    },
  });
}

export async function handleOsMediaButton(
  button: OsMediaButton,
  deps: Pick<
    OsMediaBridgeDeps,
    'togglePlay' | 'next' | 'prev' | 'getIsPlaying'
  > = defaultDeps(),
) {
  switch (button) {
    case 'Play':
      // playerStore exposes togglePlay only — resume when paused.
      if (!deps.getIsPlaying()) await deps.togglePlay();
      break;
    case 'Pause':
      if (deps.getIsPlaying()) await deps.togglePlay();
      break;
    case 'PlayPause':
      await deps.togglePlay();
      break;
    case 'Next':
      await deps.next();
      break;
    case 'Prev':
      await deps.prev();
      break;
    default:
      break;
  }
}

/** Bind OS media session and start mirroring playerStore. Idempotent. */
export async function bindOsMediaBridge(deps: OsMediaBridgeDeps = defaultDeps()) {
  if (bound) return;
  activeDeps = deps;
  try {
    await deps.invoke('os_media_bind');
    unlistenButton = await deps.listen<OsMediaButton>('os-media-button', (ev) => {
      void handleOsMediaButton(ev.payload, deps);
    });
  } catch {
    // Degrade: no Tauri / command missing / listen unavailable (unit tests).
    try {
      await deps.invoke('os_media_unbind');
    } catch {
      /* ignore */
    }
    activeDeps = null;
    bound = false;
    unlistenButton = null;
    return;
  }
  bound = true;
  stopWatch = watch(
    () => [
      playerStore.currentTrack?.FileHash,
      playerStore.currentTrack?.SongName,
      playerStore.isPlaying,
      playerStore.currentIndex,
      playerStore.queue.length,
    ],
    () => {
      void pushState(deps).catch(() => {});
    },
  );
  // Immediate push after bind (awaited so callers/tests observe full state).
  try {
    await pushState(deps);
  } catch {
    /* non-fatal: session stays bound, metadata may lag */
  }
}

export async function unbindOsMediaBridge(deps?: OsMediaBridgeDeps) {
  stopWatch?.();
  stopWatch = null;
  if (unlistenButton) {
    try {
      unlistenButton();
    } catch {
      /* ignore */
    }
    unlistenButton = null;
  }
  const d = deps ?? activeDeps;
  if (bound && d) {
    try {
      await d.invoke('os_media_unbind');
    } catch {
      /* ignore */
    }
  }
  bound = false;
  activeDeps = null;
}

/** Test-only: whether bridge considers itself bound. */
export function __osMediaBridgeIsBoundForTests() {
  return bound;
}

/** Test-only: reset without invoke (vitest isolation). */
export function __resetOsMediaBridgeForTests() {
  stopWatch?.();
  stopWatch = null;
  if (unlistenButton) {
    try {
      unlistenButton();
    } catch {
      /* ignore */
    }
    unlistenButton = null;
  }
  bound = false;
  activeDeps = null;
}
