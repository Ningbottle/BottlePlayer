import { describe, it, expect, vi, beforeEach } from 'vitest';

vi.mock('@tauri-apps/api/event', () => ({
  emit: vi.fn().mockResolvedValue(undefined),
  listen: vi.fn().mockResolvedValue(vi.fn()),
}));

import { emit, listen } from '@tauri-apps/api/event';
import {
  emitEvent,
  listenEvent,
  type PlatformEvent,
  type Unlisten,
} from '../events';

const emitMock = emit as unknown as ReturnType<typeof vi.fn>;
const listenMock = listen as unknown as ReturnType<typeof vi.fn>;

describe('platform/tauri events adapter', () => {
  beforeEach(() => {
    emitMock.mockClear().mockResolvedValue(undefined);
    listenMock.mockClear().mockResolvedValue(vi.fn());
  });

  it('emitEvent forwards name and payload to the Tauri emit', async () => {
    await emitEvent('bottle://player-state', { isPlaying: true });

    expect(emitMock).toHaveBeenCalledWith('bottle://player-state', { isPlaying: true });
  });

  it('emitEvent works without a payload', async () => {
    await emitEvent('bottle://sync-hello');

    expect(emitMock).toHaveBeenCalledWith('bottle://sync-hello', undefined);
  });

  it('listenEvent wraps the raw event as PlatformEvent<T>', async () => {
    const handler = vi.fn();
    listenMock.mockResolvedValue(vi.fn());

    await listenEvent<string>('os-media-button', handler);

    expect(listenMock).toHaveBeenCalledTimes(1);
    const [, registered] = listenMock.mock.calls[0];
    expect(registered).toBeTypeOf('function');

    const rawEvent = { payload: 'PlayPause', id: 1, event: 'os-media-button' };
    registered(rawEvent);
    expect(handler).toHaveBeenCalledWith({ payload: 'PlayPause' } as PlatformEvent<string>);
  });

  it('listenEvent returns the unlisten function as Unlisten', async () => {
    const unlisten = vi.fn() as Unlisten;
    listenMock.mockResolvedValue(unlisten);

    const returned = await listenEvent('e', vi.fn());

    expect(returned).toBe(unlisten);
    returned();
    expect(unlisten).toHaveBeenCalledTimes(1);
  });

  it('does not re-export raw Tauri types or package objects', async () => {
    const mod = await import('../events');
    const exported = Object.keys(mod);
    expect(exported.sort()).toEqual(['emitEvent', 'listenEvent'].sort());
  });
});
