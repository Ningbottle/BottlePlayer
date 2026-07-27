/**
 * overlayWindows.ts — 浮层窗口框架（灵动岛 / 桌面歌词）
 *
 * 负责：窗口创建/显隐切换、位置持久化（localStorage）、边缘吸附。
 * 浏览器与 vitest 环境（无 Tauri IPC）全部安全降级为 no-op；
 * 位置/吸附数学为纯函数，可单测。
 */
import { WebviewWindow } from '@tauri-apps/api/webviewWindow';
import { getCurrentWindow, currentMonitor } from '@tauri-apps/api/window';
import { LogicalPosition } from '@tauri-apps/api/dpi';

export type OverlayKind = 'island' | 'lyric';

export interface OverlayPos {
  x: number;
  y: number;
}

interface Size {
  w: number;
  h: number;
}

const OVERLAY_SPECS: Record<OverlayKind, { label: string; url: string; width: number; height: number }> = {
  island: { label: 'overlay-island', url: '/overlay/island', width: 340, height: 88 },
  lyric: { label: 'overlay-lyric', url: '/overlay/lyric', width: 720, height: 96 },
};

/** Pixels within a screen edge that trigger magnetic snapping. */
export const SNAP_MARGIN = 24;

export function isTauriRuntime(): boolean {
  return typeof window !== 'undefined' && ('__TAURI_INTERNALS__' in window || '__TAURI__' in window);
}

/** Pure: snap a window position to screen edges within SNAP_MARGIN. */
export function snapToEdges(pos: OverlayPos, win: Size, screen: Size): OverlayPos {
  let { x, y } = pos;
  if (Math.abs(x) <= SNAP_MARGIN) x = 0;
  else if (Math.abs(screen.w - win.w - x) <= SNAP_MARGIN) x = screen.w - win.w;
  if (Math.abs(y) <= SNAP_MARGIN) y = 0;
  else if (Math.abs(screen.h - win.h - y) <= SNAP_MARGIN) y = screen.h - win.h;
  return { x, y };
}

/** Pure: nine-grid anchor position (e.g. 'top-left', 'center', 'bottom-right'). */
export function anchorPosition(anchor: string, win: Size, screen: Size, margin = 16): OverlayPos {
  const xs: Record<string, number> = {
    left: margin,
    center: Math.round((screen.w - win.w) / 2),
    right: screen.w - win.w - margin,
  };
  const ys: Record<string, number> = {
    top: margin,
    center: Math.round((screen.h - win.h) / 2),
    bottom: screen.h - win.h - margin,
  };
  const [vy, hx] = anchor.split('-');
  return {
    x: xs[hx] ?? xs.center,
    y: ys[vy] ?? ys.center,
  };
}

export function loadOverlayPos(kind: OverlayKind): OverlayPos | null {
  try {
    const raw = localStorage.getItem(`overlay_${kind}_pos`);
    if (!raw) return null;
    const parsed = JSON.parse(raw) as Partial<OverlayPos>;
    if (typeof parsed.x === 'number' && typeof parsed.y === 'number') {
      return { x: parsed.x, y: parsed.y };
    }
  } catch {
    // corrupted entry → fall through to null
  }
  return null;
}

export function saveOverlayPos(kind: OverlayKind, pos: OverlayPos): void {
  try {
    localStorage.setItem(`overlay_${kind}_pos`, JSON.stringify(pos));
  } catch {
    // storage full/blocked — position just won't persist
  }
}

/** First-run position: top-center of the screen (iOS island style). */
export function resolveCreatePos(
  spec: { width: number; height: number },
  screen: { w: number; h: number },
): OverlayPos {
  return anchorPosition('top-center', { w: spec.width, h: spec.height }, screen);
}

export interface LyricPrefs {
  fontSize: 14 | 16 | 18 | 20 | 24;
  density: 'compact' | 'standard';
  opacity: number; // 50–100
}

const DEFAULT_LYRIC_PREFS: LyricPrefs = { fontSize: 18, density: 'standard', opacity: 100 };

export function loadLyricPrefs(): LyricPrefs {
  try {
    const raw = localStorage.getItem('overlay_lyric_prefs');
    if (!raw) return { ...DEFAULT_LYRIC_PREFS };
    const p = JSON.parse(raw) as Partial<LyricPrefs>;
    return {
      fontSize: ([14, 16, 18, 20, 24] as const).includes(p.fontSize as 14) ? (p.fontSize as LyricPrefs['fontSize']) : 18,
      density: p.density === 'compact' ? 'compact' : 'standard',
      opacity: typeof p.opacity === 'number' ? Math.max(50, Math.min(100, Math.round(p.opacity))) : 100,
    };
  } catch {
    return { ...DEFAULT_LYRIC_PREFS };
  }
}

export function saveLyricPrefs(prefs: LyricPrefs): void {
  try {
    localStorage.setItem('overlay_lyric_prefs', JSON.stringify(prefs));
  } catch {
    // best-effort
  }
}

export function loadLyricSize(): number | null {
  const raw = localStorage.getItem('overlay_lyric_size');
  const n = raw ? Number(raw) : NaN;
  return Number.isFinite(n) && n >= 480 && n <= 1200 ? Math.round(n) : null;
}

export function saveLyricSize(width: number): void {
  try {
    localStorage.setItem('overlay_lyric_size', String(Math.round(width)));
  } catch {
    // best-effort
  }
}

/** Create the overlay window if absent; close it if present. Tauri-only. */
export async function toggleOverlay(kind: OverlayKind): Promise<boolean> {
  if (!isTauriRuntime()) return false;
  const spec = OVERLAY_SPECS[kind];
  try {
    const existing = await WebviewWindow.getByLabel(spec.label);
    if (existing) {
      await existing.close();
      return false;
    }
    const pos = loadOverlayPos(kind);
    const savedWidth = kind === 'lyric' ? loadLyricSize() : null;
    const createPos = pos ?? resolveCreatePos(
      { width: savedWidth ?? spec.width, height: spec.height },
      { w: window.screen.width, h: window.screen.height },
    );
    const win = new WebviewWindow(spec.label, {
      url: spec.url,
      title: 'BottleMusic',
      width: savedWidth ?? spec.width,
      height: spec.height,
      decorations: false,
      transparent: true,
      alwaysOnTop: true,
      skipTaskbar: true,
      resizable: kind === 'lyric',
      minWidth: kind === 'lyric' ? 480 : undefined,
      maxWidth: kind === 'lyric' ? 1200 : undefined,
      shadow: false,
      x: createPos.x,
      y: createPos.y,
    });
    // Creation is async — surface failures instead of vanishing silently.
    void win.once('tauri://error', (event) => {
      console.error(`[overlay] failed to create ${spec.label}:`, event);
    });
    void win.once('tauri://created', () => {
      console.log(`[overlay] created ${spec.label} → ${spec.url}`);
    });
    return true;
  } catch (err) {
    console.error(`[overlay] toggleOverlay(${kind}) failed:`, err);
    return false;
  }
}

/**
 * Called from the overlay page on drag release: read the window's current
 * position, magnetically snap it to nearby screen edges, persist.
 */
export async function settleCurrentOverlay(kind: OverlayKind): Promise<void> {
  if (!isTauriRuntime()) return;
  const win = getCurrentWindow();
  const [pos, size, monitor] = await Promise.all([
    win.outerPosition(),
    win.outerSize(),
    currentMonitor(),
  ]);
  if (!monitor) return;
  const snapped = snapToEdges(
    { x: pos.x, y: pos.y },
    { w: size.width, h: size.height },
    { w: monitor.size.width, h: monitor.size.height },
  );
  if (snapped.x !== pos.x || snapped.y !== pos.y) {
    await win.setPosition(new LogicalPosition(snapped.x, snapped.y));
  }
  saveOverlayPos(kind, snapped);
}

/** Jump the current overlay window to a nine-grid anchor. */
export async function moveCurrentOverlayTo(anchor: string, kind: OverlayKind): Promise<void> {
  if (!isTauriRuntime()) return;
  const win = getCurrentWindow();
  const [size, monitor] = await Promise.all([win.outerSize(), currentMonitor()]);
  if (!monitor) return;
  const pos = anchorPosition(
    anchor,
    { w: size.width, h: size.height },
    { w: monitor.size.width, h: monitor.size.height },
  );
  await win.setPosition(new LogicalPosition(pos.x, pos.y));
  saveOverlayPos(kind, pos);
}
