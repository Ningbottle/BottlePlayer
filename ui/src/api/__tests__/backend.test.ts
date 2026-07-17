import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import {
  apiGet,
  isCircuitOpen,
  pickBucket,
  __resetCircuitBucketsForTests,
} from '../backend';

const mockInvoke = vi.fn();
vi.mock('@tauri-apps/api/core', () => ({
  invoke: (...args: any[]) => mockInvoke(...args),
}));

describe('backend resilience', () => {
  beforeEach(() => {
    mockInvoke.mockReset();
    __resetCircuitBucketsForTests();
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  it('times out slow invoke without frontend retry', { timeout: 30_000 }, async () => {
    mockInvoke.mockImplementation(
      () => new Promise(resolve => setTimeout(resolve, 60_000))
    );
    const start = Date.now();
    await expect(apiGet('/healthz')).rejects.toThrow('request_timeout');
    expect(Date.now() - start).toBeGreaterThanOrEqual(13_000);
    // Unique retry owner is C++ HttpClient — frontend does not re-invoke.
    expect(mockInvoke).toHaveBeenCalledTimes(1);
  });

  it('does not retry after a single rejection (records circuit failure)', async () => {
    mockInvoke.mockRejectedValue(new Error('fail'));
    await expect(apiGet('/healthz')).rejects.toThrow('fail');
    expect(mockInvoke).toHaveBeenCalledTimes(1);
  });

  it('does not retry personal FM either', async () => {
    mockInvoke.mockRejectedValue(new Error('WinHttpSendRequest failed'));
    await expect(apiGet('/personal/fm', { hash: 'abc' })).rejects.toThrow(
      'WinHttpSendRequest failed',
    );
    expect(mockInvoke).toHaveBeenCalledTimes(1);
  });

  it('succeeds on first response without extra invokes', async () => {
    mockInvoke.mockResolvedValueOnce(
      JSON.stringify({ status: 200, headers: {}, body: { ok: true } }),
    );
    await expect(apiGet('/healthz')).resolves.toEqual({ ok: true });
    expect(mockInvoke).toHaveBeenCalledTimes(1);
  });
});

describe('circuit buckets', () => {
  beforeEach(() => {
    mockInvoke.mockReset();
    __resetCircuitBucketsForTests();
  });

  it('pickBucket classifies paths by longest-prefix rules', () => {
    expect(pickBucket('/song/url')).toBe('playback');
    expect(pickBucket('/personal/fm')).toBe('playback');
    expect(pickBucket('/lyric')).toBe('lyric');
    expect(pickBucket('/search/lyric')).toBe('lyric');
    expect(pickBucket('/search')).toBe('search');
    expect(pickBucket('/search/hot')).toBe('search');
    expect(pickBucket('/playhistory/upload')).toBe('generic');
    expect(pickBucket('/healthz')).toBe('generic');
  });

  it('search failures do not open the playback bucket', async () => {
    mockInvoke.mockRejectedValue(new Error('fail'));
    for (let i = 0; i < 5; i++) {
      await expect(apiGet('/search')).rejects.toThrow('fail');
    }
    expect(isCircuitOpen('search')).toBe(true);
    expect(isCircuitOpen('playback')).toBe(false);
    expect(isCircuitOpen()).toBe(false); // default = playback

    mockInvoke.mockResolvedValueOnce(
      JSON.stringify({ status: 200, headers: {}, body: { ok: true } }),
    );
    await expect(apiGet('/song/url')).resolves.toEqual({ ok: true });
  });

  it('playback failures open only the playback bucket', async () => {
    mockInvoke.mockRejectedValue(new Error('fail'));
    for (let i = 0; i < 5; i++) {
      await expect(apiGet('/song/url')).rejects.toThrow('fail');
    }
    expect(isCircuitOpen('playback')).toBe(true);
    expect(isCircuitOpen('search')).toBe(false);
    expect(isCircuitOpen('lyric')).toBe(false);

    mockInvoke.mockResolvedValueOnce(
      JSON.stringify({ status: 200, headers: {}, body: { hits: [] } }),
    );
    await expect(apiGet('/search')).resolves.toEqual({ hits: [] });
  });
});
