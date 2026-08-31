/**
 * Updater adapter — the only module allowed to import the Tauri updater,
 * process, and opener plugins. Views see neutral structures and never touch
 * the plugin packages directly.
 */

import { check } from '@tauri-apps/plugin-updater';
import { relaunch } from '@tauri-apps/plugin-process';
import { openUrl } from '@tauri-apps/plugin-opener';

export type UpdateDownloadEvent =
  | {
      event: 'Started';
      data: {
        contentLength?: number;
      };
    }
  | {
      event: 'Progress';
      data: {
        chunkLength: number;
      };
    }
  | {
      event: 'Finished';
    };

export interface AvailableUpdate {
  version: string;
  body?: string;
  downloadAndInstall(handler?: (event: UpdateDownloadEvent) => void): Promise<void>;
}

export async function checkForUpdate(): Promise<AvailableUpdate | null> {
  const update = await check();
  if (!update) return null;
  return {
    version: update.version,
    body: update.body,
    async downloadAndInstall(handler) {
      await update.downloadAndInstall(handler as never);
    },
  };
}

export async function relaunchApp(): Promise<void> {
  await relaunch();
}

export async function openExternalUrl(url: string): Promise<void> {
  await openUrl(url);
}
