import { describe, it, expect, vi, beforeEach } from 'vitest';

vi.mock('@tauri-apps/plugin-updater', () => ({
  check: vi.fn(),
}));
vi.mock('@tauri-apps/plugin-process', () => ({
  relaunch: vi.fn(),
}));
vi.mock('@tauri-apps/plugin-opener', () => ({
  openUrl: vi.fn(),
}));

import { check } from '@tauri-apps/plugin-updater';
import { relaunch } from '@tauri-apps/plugin-process';
import { openUrl } from '@tauri-apps/plugin-opener';
import {
  checkForUpdate,
  relaunchApp,
  openExternalUrl,
  type AvailableUpdate,
  type UpdateDownloadEvent,
} from '../updater';

const checkMock = check as unknown as ReturnType<typeof vi.fn>;
const relaunchMock = relaunch as unknown as ReturnType<typeof vi.fn>;
const openUrlMock = openUrl as unknown as ReturnType<typeof vi.fn>;

// ── Compile-time event contract ─────────────────────────────────────────
// The plugin event is a discriminated union: only `Progress` requires
// `data.chunkLength`. These top-level satisfies checks make vue-tsc enforce
// that shape; the @ts-expect-error below is consumed by a real type error on
// the old loose interface (which wrongly accepted a data-less Progress).
const validStartedEvent = {
  event: 'Started',
  data: { contentLength: 1000 },
} satisfies UpdateDownloadEvent;

const validProgressEvent = {
  event: 'Progress',
  data: { chunkLength: 250 },
} satisfies UpdateDownloadEvent;

const validFinishedEvent = {
  event: 'Finished',
} satisfies UpdateDownloadEvent;

void validStartedEvent;
void validProgressEvent;
void validFinishedEvent;

const invalidProgressWithoutChunk = {
  event: 'Progress',
  // @ts-expect-error Progress events must carry chunkLength.
  data: {},
} satisfies UpdateDownloadEvent;

void invalidProgressWithoutChunk;

describe('platform/tauri updater adapter', () => {
  beforeEach(() => {
    checkMock.mockReset();
    relaunchMock.mockReset().mockResolvedValue(undefined);
    openUrlMock.mockReset().mockResolvedValue(undefined);
  });

  it('checkForUpdate maps a found update to a neutral AvailableUpdate', async () => {
    checkMock.mockResolvedValue({
      version: '1.2.3',
      body: 'fixes',
      downloadAndInstall: vi.fn().mockResolvedValue(undefined),
    });

    const update: AvailableUpdate | null = await checkForUpdate();

    expect(update).not.toBeNull();
    expect(update!.version).toBe('1.2.3');
    expect(update!.body).toBe('fixes');
    expect(typeof update!.downloadAndInstall).toBe('function');
  });

  it('checkForUpdate returns null when no update exists', async () => {
    checkMock.mockResolvedValue(null);
    expect(await checkForUpdate()).toBeNull();
  });

  it('downloadAndInstall forwards progress events in the plugin shape', async () => {
    const events: UpdateDownloadEvent[] = [];
    checkMock.mockResolvedValue({
      version: '2.0.0',
      downloadAndInstall: vi.fn(async (handler: (e: unknown) => void) => {
        handler({ event: 'Started', data: { contentLength: 1000 } });
        handler({ event: 'Progress', data: { chunkLength: 250 } });
        handler({ event: 'Finished' });
      }),
    });

    const update = await checkForUpdate();
    await update!.downloadAndInstall((e) => events.push(e));

    expect(events.map((e) => e.event)).toEqual(['Started', 'Progress', 'Finished']);
    expect(events[0]).toEqual({ event: 'Started', data: { contentLength: 1000 } });
    expect(events[1]).toEqual({ event: 'Progress', data: { chunkLength: 250 } });
  });

  it('checkForUpdate rejects when the plugin throws (caller keeps its UI error path)', async () => {
    checkMock.mockRejectedValue(new Error('network down'));
    await expect(checkForUpdate()).rejects.toThrow('network down');
  });

  it('relaunchApp delegates to the process plugin', async () => {
    await relaunchApp();
    expect(relaunchMock).toHaveBeenCalledTimes(1);
  });

  it('openExternalUrl delegates to the opener plugin with the URL unchanged', async () => {
    await openExternalUrl('https://m.kugou.com/');
    expect(openUrlMock).toHaveBeenCalledWith('https://m.kugou.com/');
  });
});
