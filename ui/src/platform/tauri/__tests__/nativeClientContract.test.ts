/**
 * Cross-layer shape contract for the native_request envelope (audit item B4).
 *
 * Fixtures replicate what the C++ backend really serializes:
 * - the envelope wrapper {"status","headers":{"Content-Type"},"body"} built
 *   by native/core/C_API.cpp SerializeResponse,
 * - the deterministic 404 body {"status":0,"error_code":404,
 *   "error":"Unknown route"} that native/core/CompatApi.cpp returns on a
 *   route-table miss (no network involved),
 * - the local /health body from native/core/compat_routes/
 *   DiagnosticsRoutes.cpp (no upstream call).
 *
 * They drive the real parsing code in platform/tauri/nativeClient.ts
 * (ipcRequest -> apiGet) and cover both branches of the Tauri
 * Result<String, String>: the Ok JSON string and the Err error string
 * (e.g. "C API not loaded" from src/backend_api.rs, "request_deadline"
 * from src/lib.rs).
 */
import { describe, it, expect, vi, beforeEach } from 'vitest';
import {
  apiGet,
  isCircuitOpen,
  describeBackendError,
  __resetCircuitBucketsForTests,
} from '../nativeClient';

const mockInvoke = vi.fn();
vi.mock('../invoke', () => ({
  invokeTauri: (...args: any[]) => mockInvoke(...args),
}));

/** Envelope exactly as SerializeResponse (native/core/C_API.cpp) emits it.
 * Content-Type is the CompatResponse default (CompatApi.h):
 * "application/json; charset=utf-8". */
function cppEnvelope(status: number, body: unknown): string {
  return JSON.stringify({
    status,
    headers: { 'Content-Type': 'application/json; charset=utf-8' },
    body,
  });
}

/** Body exactly as CompatApi.cpp builds it for a route-table miss. */
const UNKNOWN_ROUTE_BODY = { status: 0, error_code: 404, error: 'Unknown route' };

/** Body fields as DiagnosticsRoutes.cpp HandleHealth emits them. */
const HEALTH_BODY = { status: 1, data: { service: 'EchoCompatServer', state: 'ok' } };

describe('cross-layer contract: C++ envelope shapes -> nativeClient', () => {
  beforeEach(() => {
    mockInvoke.mockReset();
    __resetCircuitBucketsForTests();
  });

  it('apiGet returns the body of a real 200 envelope and keeps the IPC argument contract', async () => {
    mockInvoke.mockResolvedValueOnce(cppEnvelope(200, HEALTH_BODY));

    await expect(apiGet('/health')).resolves.toEqual(HEALTH_BODY);

    expect(mockInvoke).toHaveBeenCalledTimes(1);
    expect(mockInvoke).toHaveBeenCalledWith('native_request', {
      method: 'GET',
      path: '/health',
      queryJson: undefined,
      headersJson: undefined,
      body: undefined,
    });
  });

  it('apiGet rejects with the HTTP status carried by the real 404 Unknown-route envelope', async () => {
    mockInvoke.mockResolvedValueOnce(cppEnvelope(404, UNKNOWN_ROUTE_BODY));

    await expect(apiGet('/contract-test/nonexistent-route')).rejects.toThrow(
      'HTTP 404 (via IPC)',
    );
    expect(mockInvoke).toHaveBeenCalledTimes(1);
  });

  it('an envelope-level 404 counts toward the circuit breaker like a transport failure', async () => {
    mockInvoke.mockResolvedValue(cppEnvelope(404, UNKNOWN_ROUTE_BODY));

    for (let i = 0; i < 5; i++) {
      await expect(apiGet('/search')).rejects.toThrow('HTTP 404 (via IPC)');
    }

    expect(isCircuitOpen('search')).toBe(true);
    expect(isCircuitOpen('playback')).toBe(false);
  });

  it('propagates the Rust Err string "C API not loaded" untouched and maps it for users', async () => {
    // Tauri Result<String, String>: the Err variant surfaces as an invoke
    // rejection carrying the Rust error string verbatim.
    mockInvoke.mockRejectedValue('C API not loaded');

    await expect(apiGet('/health')).rejects.toBe('C API not loaded');
    expect(describeBackendError('C API not loaded', '请求失败')).toBe(
      '本地音乐服务未就绪，请重新打开应用',
    );
  });

  it('propagates the Rust Err string "request_deadline" and keeps the caller fallback', async () => {
    // "request_deadline" is the outer-watchdog error string from src/lib.rs
    // native_request. describeBackendError currently has no dedicated branch
    // for it and must fall back to the caller-provided message.
    mockInvoke.mockRejectedValue('request_deadline');

    await expect(apiGet('/health')).rejects.toBe('request_deadline');
    expect(describeBackendError(new Error('request_deadline'), '请求失败')).toBe('请求失败');
  });
});
