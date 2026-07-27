/**
 * playerSync.ts — 主窗口与浮层窗口之间的播放状态同步。
 *
 * 主窗口（host）：节流广播 playerStore 快照，接收浮层命令并执行。
 * 浮层（client）：订阅状态快照，发送传输命令。
 * 全部经 Tauri event bus；非 Tauri 环境安全降级为 no-op。
 */
import { emit, listen, type UnlistenFn } from '@tauri-apps/api/event';
import { watch } from 'vue';
import { playerStore, togglePlay, next, prev, seek, setVolume } from './playerStore';
import { useThemeStore } from './themeStore';
import { useAppearanceStore } from './appearanceStore';
import { isTauriRuntime } from './overlayWindows';

export interface PlayerSyncState {
  hash: string;
  name: string;
  artist: string;
  cover: string;
  isPlaying: boolean;
  currentTime: number;
  duration: number;
  volume: number;
  loopMode: string;
  /** Theme snapshot — overlay windows must mirror the main window's look. */
  skin?: string;
  mode?: string;
  accent?: string;
}

export type PlayerCommand =
  | { action: 'toggle' | 'next' | 'prev' }
  | { action: 'seek'; value: number }
  | { action: 'volume'; value: number };

const STATE_EVENT = 'bottle://player-state';
const CMD_EVENT = 'bottle://player-cmd';
const HELLO_EVENT = 'bottle://sync-hello';

function snapshot(): PlayerSyncState {
  const t = playerStore.currentTrack;
  const theme = useThemeStore();
  return {
    hash: t?.FileHash ?? '',
    name: t?.SongName ?? '',
    artist: t?.SingerName ?? '',
    cover: t?.Image ?? '',
    isPlaying: playerStore.isPlaying,
    currentTime: playerStore.currentTime,
    duration: playerStore.duration,
    volume: playerStore.volume,
    loopMode: playerStore.loopMode,
    skin: theme.skinId.value,
    mode: theme.mode.value,
    accent: useAppearanceStore().accent.value,
  };
}

/**
 * Overlay side: mirror the main window's theme onto this document.
 * Overlay localStorage is not guaranteed to share the main window's keys,
 * so theme must travel through the sync channel, not storage.
 */
export function applySyncedTheme(state: PlayerSyncState): void {
  if (state.skin) document.documentElement.dataset.skin = state.skin;
  if (state.mode) document.documentElement.dataset.mode = state.mode;
  if (state.accent) {
    document.documentElement.style.setProperty('--accent', state.accent);
  } else {
    document.documentElement.style.removeProperty('--accent');
  }
}

/**
 * Main-window side: broadcast state (throttled ticks + immediate on track /
 * playstate change) and execute overlay transport commands.
 * Returns a teardown function.
 */
export function startPlayerSyncHost(): () => void {
  if (!isTauriRuntime()) return () => {};

  const broadcast = (): void => {
    void emit(STATE_EVENT, snapshot()).catch(() => {});
  };

  // Immediate on structural changes; light throttle for progress ticks.
  const stopWatch = watch(
    () => [
      playerStore.currentTrack?.FileHash,
      playerStore.isPlaying,
      playerStore.loopMode,
      playerStore.volume,
      useThemeStore().skinId.value,
      useThemeStore().mode.value,
      useAppearanceStore().accent.value,
    ],
    broadcast,
  );
  const tick = setInterval(() => {
    if (playerStore.isPlaying) broadcast();
  }, 500);

  const unlistenPromise = listen<PlayerCommand>(CMD_EVENT, (event) => {
    const cmd = event.payload;
    if (cmd.action === 'toggle') togglePlay();
    else if (cmd.action === 'next') next();
    else if (cmd.action === 'prev') prev();
    else if (cmd.action === 'seek') seek(cmd.value);
    else if (cmd.action === 'volume') setVolume(cmd.value);
  });

  // Overlays that subscribe after the seed broadcast would otherwise wait
  // forever while idle — they say hello, we answer with an immediate state.
  const helloPromise = listen(HELLO_EVENT, broadcast);

  broadcast(); // seed late-opening overlays immediately

  let unlisten: UnlistenFn | null = null;
  let unhello: UnlistenFn | null = null;
  void unlistenPromise.then((fn) => {
    unlisten = fn;
  });
  void helloPromise.then((fn) => {
    unhello = fn;
  });

  return () => {
    stopWatch();
    clearInterval(tick);
    unlisten?.();
    unhello?.();
    void unlistenPromise.then((fn) => fn());
    void helloPromise.then((fn) => fn());
  };
}

/** Overlay side: send a transport command to the main window. */
export async function sendPlayerCommand(cmd: PlayerCommand): Promise<void> {
  if (!isTauriRuntime()) return;
  await emit(CMD_EVENT, cmd).catch(() => {});
}

/** Overlay side: subscribe to state snapshots. Returns unlisten. */
export async function onPlayerState(
  cb: (state: PlayerSyncState) => void,
): Promise<() => void> {
  if (!isTauriRuntime()) return () => {};
  const unlisten = await listen<PlayerSyncState>(STATE_EVENT, (event) => {
    cb(event.payload);
  });
  // Handshake: ask the host to rebroadcast so late joiners get theme + state now.
  await emit(HELLO_EVENT, null).catch(() => {});
  return unlisten;
}
