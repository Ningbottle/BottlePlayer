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
