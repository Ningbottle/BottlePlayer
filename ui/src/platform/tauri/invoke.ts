/**
 * Tauri command adapter — the only module allowed to import
 * @tauri-apps/api/core. Thin pass-through: no business command names, no
 * payload knowledge, no error translation.
 */

export type InvokeArgs = Record<string, unknown>;

import { invoke } from '@tauri-apps/api/core';

export function invokeTauri<T>(command: string, args?: InvokeArgs): Promise<T> {
  return invoke<T>(command, args);
}
