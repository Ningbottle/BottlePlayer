import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import {
  apiGet,
  isCircuitOpen,
  pickBucket,
  describeBackendError,
  __resetCircuitBucketsForTests,
} from '../nativeClient';

const mockInvoke = vi.fn();
vi.mock('../invoke', () => ({
  invokeTauri: (...args: any[]) => mockInvoke(...args),
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
    vi.useFakeTimers();
    mockInvoke.mockImplementation(
      () => new Promise(resolve => setTimeout(resolve, 60_000))
    );
    const request = expect(apiGet('/healthz')).rejects.toThrow('request_timeout');
    await vi.advanceTimersByTimeAsync(14_000);
    await request;
    // Unique retry owner is C++ HttpClient — frontend does not re-invoke.
    expect(mockInvoke).toHaveBeenCalledTimes(1);
  });

  it('clears the timeout after a successful invoke', async () => {
    vi.useFakeTimers();
    mockInvoke.mockResolvedValueOnce(
      JSON.stringify({ status: 200, headers: {}, body: { ok: true } }),
    );

    await expect(apiGet('/healthz')).resolves.toEqual({ ok: true });

    expect(vi.getTimerCount()).toBe(0);
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

describe('backend error messages', () => {
  it('distinguishes an unavailable native runtime from a legacy sidecar error', () => {
    expect(describeBackendError(new Error('C API not loaded'), '请求失败')).toBe(
      '本地音乐服务未就绪，请重新打开应用',
    );
    expect(describeBackendError('C API not loaded', '请求失败')).toBe(
      '本地音乐服务未就绪，请重新打开应用',
    );
  });

  it('distinguishes timeouts and open circuits', () => {
    expect(describeBackendError(new Error('request_timeout'), '请求失败')).toBe(
      '请求超时，请稍后重试',
    );
    expect(describeBackendError(new Error('circuit_open'), '请求失败')).toBe(
      '服务暂时繁忙，请稍后重试',
    );
  });

  it('uses the caller fallback for unknown backend failures', () => {
    expect(describeBackendError(new Error('HTTP 503 (via IPC)'), '搜索失败，请稍后重试')).toBe(
      '搜索失败，请稍后重试',
    );
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
