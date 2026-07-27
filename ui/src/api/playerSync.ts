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
}

export type PlayerCommand =
  | { action: 'toggle' | 'next' | 'prev' }
  | { action: 'seek'; value: number }
  | { action: 'volume'; value: number };

const STATE_EVENT = 'bottle://player-state';
const CMD_EVENT = 'bottle://player-cmd';

function snapshot(): PlayerSyncState {
  const t = playerStore.currentTrack;
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
  };
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
    () => [playerStore.currentTrack?.FileHash, playerStore.isPlaying, playerStore.loopMode, playerStore.volume],
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

  broadcast(); // seed late-opening overlays immediately

  let unlisten: UnlistenFn | null = null;
  void unlistenPromise.then((fn) => {
    unlisten = fn;
  });

  return () => {
    stopWatch();
    clearInterval(tick);
    unlisten?.();
    void unlistenPromise.then((fn) => fn());
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
  return unlisten;
}
