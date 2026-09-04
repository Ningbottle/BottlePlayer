/**
 * Platform/Tauri public API.
 *
 * nativeClient, audioProxy and windows are owned here in platform/tauri/;
 * this facade explicitly re-exports the production API that Features may use.
 * Feature callers finish migrating onto this typed gateway surface in Task
 * C7. Test seams and circuit breaker internals are deliberately NOT exported.
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
