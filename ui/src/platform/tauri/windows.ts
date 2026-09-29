/**
 * windows.ts — Neutral window action adapters for the app shell.
 *
 * Feature/App/View code consumes these; raw Tauri window objects never leave
 * this module.
 */
import { getCurrentWindow } from '@tauri-apps/api/window';

export async function minimizeCurrentWindow(): Promise<void> {
  await getCurrentWindow().minimize();
}

export async function toggleMaximizeCurrentWindow(): Promise<void> {
  await getCurrentWindow().toggleMaximize();
}

export async function closeCurrentWindow(): Promise<void> {
  await getCurrentWindow().close();
}
