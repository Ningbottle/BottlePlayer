/**
 * Platform/Tauri public API (C1 transitional facade).
 *
 * Explicit re-exports of the production adapters still living in ../../api/*
 * (they physically move here in Task C2). Callers are not yet migrated — this
 * file only establishes the future public surface. Test seams and circuit
 * breaker internals are deliberately NOT exported.
 */
export {
  apiGet,
  apiPost,
  ping,
  backendHealth,
  isCircuitOpen,
  describeBackendError,
} from './nativeClient';
export type { CircuitBucket } from './nativeClient';

export { prepareAudioSourceUrl } from './audioProxy';

export {
  SNAP_MARGIN,
  isTauriRuntime,
  snapToEdges,
  anchorPosition,
  loadOverlayPos,
  saveOverlayPos,
  resolveCreatePos,
  loadLyricPrefs,
  saveLyricPrefs,
  loadLyricSize,
  saveLyricSize,
  toggleOverlay,
  settleCurrentOverlay,
  moveCurrentOverlayTo,
} from '../../api/overlayWindows';
export type { OverlayKind, OverlayToggleResult, OverlayPos, LyricPrefs } from '../../api/overlayWindows';
