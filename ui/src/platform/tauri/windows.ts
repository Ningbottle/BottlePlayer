/**
 * overlayWindows.ts — 浮层窗口框架（灵动岛 / 桌面歌词）
 *
 * 负责：窗口创建/显隐切换、位置持久化（localStorage）、边缘吸附。
 * 浏览器与 vitest 环境（无 Tauri IPC）全部安全降级为 no-op；
 * 位置/吸附数学为纯函数，可单测。
 */
import { WebviewWindow } from '@tauri-apps/api/webviewWindow';
import { getCurrentWindow, currentMonitor } from '@tauri-apps/api/window';
import { getCurrentWebview } from '@tauri-apps/api/webview';
import { PhysicalPosition, LogicalSize } from '@tauri-apps/api/dpi';

// ── Neutral window action adapters ──────────────────────────────────────
// Feature/App/View code consumes these; raw Tauri window/webview objects
// and dpi types never leave this module.

export type Unlisten = () => void;

export interface WindowPosition { x: number; y: number; }
export interface WindowSize { width: number; height: number; }
export interface WindowFrame {
  position: WindowPosition;
  size: WindowSize;
  scaleFactor: number;
}

/** Error semantics live with the caller (shell buttons warn, overlays swallow). */
export async function minimizeCurrentWindow(): Promise<void> {
  await getCurrentWindow().minimize();
}

export async function toggleMaximizeCurrentWindow(): Promise<void> {
  await getCurrentWindow().toggleMaximize();
}

export async function closeCurrentWindow(): Promise<void> {
  await getCurrentWindow().close();
}

/** Scale factor only — no position/size IPC. Overlay resize handlers use this. */
export async function getCurrentWindowScaleFactor(): Promise<number> {
  return getCurrentWindow().scaleFactor();
}

export async function readCurrentWindowFrame(): Promise<WindowFrame> {
  const win = getCurrentWindow();
  const [position, outer, scaleFactor] = await Promise.all([
    win.outerPosition(),
    win.outerSize(),
    win.scaleFactor(),
  ]);
  return {
    position: { x: position.x, y: position.y },
    size: { width: outer.width, height: outer.height },
    scaleFactor,
  };
}

export async function setCurrentWindowLogicalSize(width: number, height: number): Promise<void> {
  await getCurrentWindow().setSize(new LogicalSize(width, height));
}

export async function setCurrentWindowPhysicalPosition(x: number, y: number): Promise<void> {
  await getCurrentWindow().setPosition(new PhysicalPosition(x, y));
}

export async function makeCurrentOverlayTransparent(): Promise<void> {
  await Promise.allSettled([
    getCurrentWindow().setBackgroundColor([0, 0, 0, 0]),
    getCurrentWebview().setBackgroundColor([0, 0, 0, 0]),
  ]);
}

/**
 * Resized handler receives PHYSICAL pixels (as Tauri reports them). Callers
 * that need logical units read the scale factor via readCurrentWindowFrame()
 * or the current window's scaleFactor — no raw window handle is handed out.
 */
export async function onCurrentWindowResized(
  handler: (event: { payload: WindowSize }) => void,
): Promise<Unlisten> {
  return getCurrentWindow().onResized((event) => {
    handler({ payload: { width: event.payload.width, height: event.payload.height } });
  });
}

export type OverlayKind = 'island' | 'lyric';
export type OverlayToggleResult = 'opened' | 'closed' | 'failed';

export interface OverlayPos {
  x: number;
  y: number;
}

interface Size {
  w: number;
  h: number;
}

const OVERLAY_SPECS: Record<OverlayKind, { label: string; url: string; width: number; height: number }> = {
  island: { label: 'overlay-island', url: '/overlay/island', width: 236, height: 40 },
  lyric: { label: 'overlay-lyric', url: '/overlay/lyric', width: 560, height: 80 },
};

const TOP_INSET = 0;
const LYRIC_WIDTH_MIN = 420;
const LYRIC_WIDTH_MAX = 640;

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
    top: TOP_INSET,
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
      return clampToScreen(kind, { x: parsed.x, y: parsed.y });
    }
  } catch {
    // corrupted entry → fall through to null
  }
  return null;
}

/** Keep a remembered horizontal position inside the primary screen and dock it to the top. */
function clampToScreen(kind: OverlayKind, pos: OverlayPos): OverlayPos {
  const spec = OVERLAY_SPECS[kind];
  if (window.screen.width <= 0 || window.screen.height <= 0) return pos;
  const maxX = Math.max(0, window.screen.width - spec.width);
  return {
    x: Math.max(0, Math.min(maxX, pos.x)),
    y: TOP_INSET,
  };
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
  try {
    const raw = localStorage.getItem('overlay_lyric_size');
    const n = raw ? Number(raw) : NaN;
    return Number.isFinite(n) && n >= LYRIC_WIDTH_MIN && n <= LYRIC_WIDTH_MAX ? Math.round(n) : null;
  } catch {
    return null;
  }
}

export function saveLyricSize(width: number): void {
  try {
    localStorage.setItem('overlay_lyric_size', String(Math.round(width)));
  } catch {
    // best-effort
  }
}

/** Create the overlay window if absent; close it if present. Tauri-only. */
export async function toggleOverlay(kind: OverlayKind): Promise<OverlayToggleResult> {
  if (!isTauriRuntime()) return 'failed';
  const spec = OVERLAY_SPECS[kind];
  try {
    const existing = await WebviewWindow.getByLabel(spec.label);
    if (existing) {
      await existing.close();
      return 'closed';
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
      backgroundColor: { red: 0, green: 0, blue: 0, alpha: 0 },
      visible: false,
      alwaysOnTop: true,
      skipTaskbar: true,
      resizable: kind === 'lyric',
      minWidth: kind === 'lyric' ? LYRIC_WIDTH_MIN : undefined,
      maxWidth: kind === 'lyric' ? LYRIC_WIDTH_MAX : undefined,
      shadow: false,
      x: createPos.x,
      y: createPos.y,
    });
    // Creation is async — surface failures instead of vanishing silently.
    void win.once('tauri://error', (event) => {
      console.error(`[overlay] failed to create ${spec.label}:`, event);
    });
    void win.once('tauri://created', async () => {
      console.log(`[overlay] created ${spec.label} → ${spec.url}`);
      try {
        // The constructor can resolve before WebView2 has applied its content
        // background. Re-apply transparency after creation, then reveal the
        // window so no rectangular first frame leaks through.
        await win.setBackgroundColor({ red: 0, green: 0, blue: 0, alpha: 0 });
      } catch (error) {
        console.warn(`[overlay] could not re-apply transparency for ${spec.label}:`, error);
      } finally {
        await win.show().catch((error) => {
          console.error(`[overlay] could not show ${spec.label}:`, error);
        });
      }
    });
    return 'opened';
  } catch (err) {
    console.error(`[overlay] toggleOverlay(${kind}) failed:`, err);
    return 'failed';
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
  // Both compact overlays dock to the top edge — horizontal placement only.
  snapped.y = TOP_INSET;
  if (snapped.x !== pos.x || snapped.y !== pos.y) {
    await win.setPosition(new PhysicalPosition(snapped.x, snapped.y));
  }
  saveOverlayPos(kind, snapped);
}

/** Jump the current overlay window to a horizontal anchor along the top edge. */
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
  pos.y = TOP_INSET;
  await win.setPosition(new PhysicalPosition(pos.x, pos.y));
  saveOverlayPos(kind, pos);
}
