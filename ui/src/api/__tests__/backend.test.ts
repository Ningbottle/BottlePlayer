import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { apiGet } from '../backend';

const mockInvoke = vi.fn();
vi.mock('@tauri-apps/api/core', () => ({
  invoke: (...args: any[]) => mockInvoke(...args),
}));

describe('backend resilience', () => {
  beforeEach(() => {
    mockInvoke.mockReset();
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  it('times out slow invoke and retry succeeds', { timeout: 30_000 }, async () => {
    mockInvoke
      .mockImplementationOnce(
        () => new Promise(resolve => setTimeout(resolve, 60_000))
      )
      .mockResolvedValueOnce(
        JSON.stringify({ status: 200, headers: {}, body: { ok: true } })
      );
    const start = Date.now();
    await expect(apiGet('/healthz')).resolves.toEqual({ ok: true });
    expect(Date.now() - start).toBeGreaterThanOrEqual(13_000);
    expect(mockInvoke).toHaveBeenCalledTimes(2);
  });

  it('retries idempotent GET once after rejection then succeeds', async () => {
    vi.useFakeTimers();
    mockInvoke
      .mockRejectedValueOnce(new Error('fail'))
      .mockResolvedValueOnce(JSON.stringify({ status: 200, headers: {}, body: { ok: true } }));
    const p = apiGet('/healthz');
    await vi.advanceTimersByTimeAsync(3_000);
    await expect(p).resolves.toEqual({ ok: true });
    expect(mockInvoke).toHaveBeenCalledTimes(2);
  });

  it('does not retry non-idempotent paths', async () => {
    mockInvoke.mockRejectedValue(new Error('fail'));
    await expect(apiGet('/login/qr/check', { key: 'x' })).rejects.toThrow('fail');
    expect(mockInvoke).toHaveBeenCalledTimes(1);
  });

  it('retries personal FM recommendation reads after transient native errors', async () => {
    vi.useFakeTimers();
    mockInvoke
      .mockRejectedValueOnce(new Error('WinHttpSendRequest/WinHttpReceiveResponse failed with Win32 error 12175'))
      .mockResolvedValueOnce(JSON.stringify({
        status: 200,
        headers: {},
        body: { status: 1, data: { song_list: [] } },
      }));

    const p = apiGet('/personal/fm', { hash: 'abc' });
    await vi.advanceTimersByTimeAsync(3_000);

    await expect(p).resolves.toEqual({ status: 1, data: { song_list: [] } });
    expect(mockInvoke).toHaveBeenCalledTimes(2);
  });
});
