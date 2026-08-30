import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import type { PlayRecord } from '../playSessionTracker';

const mockInvoke = vi.fn();
vi.mock('@tauri-apps/api/core', () => ({
  invoke: (...args: unknown[]) => mockInvoke(...args),
}));

function mkRecord(): PlayRecord {
  return {
    song_hash: 'hash-1',
    song_name: 'Song',
    singer_name: 'A',
    album_id: 'album-1',
    album_name: 'Album',
    cover_url: 'http://img/cover',
    duration_seconds: 200,
    completed: false,
    listened_seconds: 12.5,
    quality: '320',
    played_at: 1735560000000,
  };
}

describe('playStatsGateway.recordPlay', () => {
  beforeEach(() => {
    vi.resetModules();
    mockInvoke.mockReset();
    mockInvoke.mockResolvedValue('');
  });

  afterEach(() => {
    vi.restoreAllMocks();
  });

  it('invokes stats_record_play with the record JSON in the expected shape', async () => {
    const { recordPlay } = await import('../playStatsGateway');
    const record = mkRecord();

    recordPlay(record);

    expect(mockInvoke).toHaveBeenCalledTimes(1);
    expect(mockInvoke).toHaveBeenCalledWith('stats_record_play', {
      json: JSON.stringify(record),
    });
  });

  it('is fire-and-forget: returns void synchronously', async () => {
    const { recordPlay } = await import('../playStatsGateway');

    const result = recordPlay(mkRecord());

    expect(result).toBeUndefined();
  });

  it('swallows an invoke Promise rejection without an unhandled rejection', async () => {
    mockInvoke.mockRejectedValue(new Error('ipc failed'));
    const { recordPlay } = await import('../playStatsGateway');

    const unhandled: unknown[] = [];
    const handler = (reason: unknown) => unhandled.push(reason);
    process.on('unhandledRejection', handler);
    try {
      expect(() => recordPlay(mkRecord())).not.toThrow();
      await new Promise((r) => setTimeout(r, 0));
      expect(unhandled).toHaveLength(0);
    } finally {
      process.off('unhandledRejection', handler);
    }
  });

  it('swallows a synchronous invoke throw without affecting the caller', async () => {
    mockInvoke.mockImplementation(() => {
      throw new Error('invoke not available');
    });
    const { recordPlay } = await import('../playStatsGateway');

    expect(() => recordPlay(mkRecord())).not.toThrow();
    expect(mockInvoke).toHaveBeenCalledTimes(1);
  });

  it('passes the record through unchanged (no field mutation, no re-serialization drift)', async () => {
    const { recordPlay } = await import('../playStatsGateway');
    const record = mkRecord();

    recordPlay(record);

    const passed = mockInvoke.mock.calls[0]?.[1]?.json as string;
    expect(JSON.parse(passed)).toEqual(record);
  });
});
