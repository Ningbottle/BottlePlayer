import { invoke } from '@tauri-apps/api/core';
import { CircuitBreaker } from './circuitBreaker';

// 前端 → C++ 后端的统一入口。
//
// 设计：
//   - 后端已演进为直接通过 FFI 调用 C++ EchoCAPI.dll (native_request)。
//   - 接口保持原样，不搞乱原有代码逻辑。
//   - S1 增加：前端 timeout、幂等 GET 重试、熔断器。

const FRONTEND_TIMEOUT_MS = 14_000;

const circuitBreaker = new CircuitBreaker({
  failureThreshold: 5,
  openDurationMs: 30_000,
});

const IDEMPOTENT_GETS =
  /^\/(healthz|song\/url|search|playlist|rank|top|album|artist|images\/audio|user\/history)/;

const RETRY_DELAYS_MS = [500, 2_000];

function sleep(ms: number): Promise<void> {
  return new Promise(resolve => setTimeout(resolve, ms));
}

async function withTimeout<T>(promise: Promise<T>, ms: number): Promise<T> {
  return Promise.race([
    promise,
    new Promise<never>((_, reject) =>
      setTimeout(() => reject(new Error('request_timeout')), ms)
    ),
  ]);
}

/** IPC 响应的统一结构 (对应 C_API.cpp 的输出) */
interface NativeResponse {
  status: number;
  headers: Record<string, string>;
  body: any;
}

/** 通用底层 IPC 调用 */
async function ipcRequest(
  method: string,
  path: string,
  query?: Record<string, string | number>,
  headers?: Record<string, string>,
  body?: string
): Promise<NativeResponse> {
  let queryStr: string | undefined = undefined;
  if (query) {
    const q: Record<string, string> = {};
    for (const [k, v] of Object.entries(query)) {
      q[k] = String(v);
    }
    queryStr = JSON.stringify(q);
  }

  const headersStr = headers ? JSON.stringify(headers) : undefined;

  const rawJson = await withTimeout(
    invoke<string>('native_request', {
      method,
      path,
      queryJson: queryStr,
      headersJson: headersStr,
      body,
    }),
    FRONTEND_TIMEOUT_MS
  );

  return JSON.parse(rawJson) as NativeResponse;
}

async function apiGetOnce<T = unknown>(
  path: string,
  query?: Record<string, string | number>
): Promise<T> {
  const r = await ipcRequest('GET', path, query);
  if (r.status < 200 || r.status >= 300) {
    throw new Error('HTTP ' + r.status + ' (via IPC)');
  }
  return r.body as T;
}

async function apiGetWithRetry<T = unknown>(
  path: string,
  query?: Record<string, string | number>
): Promise<T> {
  if (!circuitBreaker.isClosed()) {
    throw new Error('circuit_open');
  }

  const canRetry = IDEMPOTENT_GETS.test(path);
  const attempts = canRetry ? 1 + RETRY_DELAYS_MS.length : 1;

  for (let attempt = 0; attempt < attempts; attempt++) {
    try {
      const result = await apiGetOnce<T>(path, query);
      circuitBreaker.recordSuccess();
      return result;
    } catch (e) {
      circuitBreaker.recordFailure();
      if (attempt === attempts - 1) throw e;
      await sleep(RETRY_DELAYS_MS[attempt]);
    }
  }

  throw new Error('unreachable');
}

export async function ping(): Promise<string> {
  return invoke<string>('ping');
}

export function isCircuitOpen(): boolean {
  return !circuitBreaker.isClosed();
}

/** 探测后端是否就绪（轮询用）。后端约定有 /healthz；若无则用 / 兜底。 */
export async function backendHealth(): Promise<{ ok: boolean; status: number; text?: string }> {
  try {
    const r = await ipcRequest('GET', '/healthz');
    return {
      ok: r.status >= 200 && r.status < 300,
      status: r.status,
      text: typeof r.body === 'string' ? r.body : JSON.stringify(r.body),
    };
  } catch (e) {
    return { ok: false, status: 0, text: String(e) };
  }
}

/** 通用 GET（返回 JSON）。 */
export async function apiGet<T = unknown>(
  path: string,
  query?: Record<string, string | number>
): Promise<T> {
  return apiGetWithRetry<T>(path, query);
}

/** 通用 POST（返回 JSON）。 */
export async function apiPost<T = unknown>(
  path: string,
  body?: string,
  query?: Record<string, string | number>
): Promise<T> {
  const r = await ipcRequest('POST', path, query, undefined, body);
  if (r.status < 200 || r.status >= 300) {
    throw new Error('HTTP ' + r.status + ' (via IPC)');
  }
  return r.body as T;
}
