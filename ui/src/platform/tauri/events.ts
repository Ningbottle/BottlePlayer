/**
 * Tauri event adapter — the only module allowed to import
 * @tauri-apps/api/event. Callers see neutral names and a wrapped payload;
 * raw Tauri Event/UnlistenFn types and the package object stay here.
 */

import { emit, listen } from '@tauri-apps/api/event';

export type Unlisten = () => void;

export interface PlatformEvent<T> {
  payload: T;
}

export function emitEvent<T>(name: string, payload?: T): Promise<void> {
  return emit(name, payload);
}

export function listenEvent<T>(
  name: string,
  handler: (event: PlatformEvent<T>) => void,
): Promise<Unlisten> {
  return listen<T>(name, (event) => {
    handler({ payload: event.payload });
  }) as Promise<Unlisten>;
}
