import { invoke } from '@tauri-apps/api/core';

// 前端 → C++ 后端的统一入口。
//
// 设计：
//   - 后端已演进为直接通过 FFI 调用 C++ EchoCAPI.dll (native_request)。
//   - 接口保持原样，不搞乱原有代码逻辑。

export async function ping(): Promise<string> {
  return invoke<string>('ping');
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
  // Query 序列化为 string
  let queryStr: string | undefined = undefined;
  if (query) {
    const q: Record<string, string> = {};
    for (const [k, v] of Object.entries(query)) {
      q[k] = String(v);
    }
    queryStr = JSON.stringify(q);
  }

  const headersStr = headers ? JSON.stringify(headers) : undefined;
  
  const rawJson = await invoke<string>('native_request', {
    method,
    path,
    queryJson: queryStr,
    headersJson: headersStr,
    body
  });

  return JSON.parse(rawJson) as NativeResponse;
}

/** 探测后端是否就绪（轮询用）。后端约定有 /healthz；若无则用 / 兜底。 */
export async function backendHealth(): Promise<{ ok: boolean; status: number; text?: string }> {
  try {
    const r = await ipcRequest('GET', '/healthz');
    return { 
      ok: r.status >= 200 && r.status < 300, 
      status: r.status, 
      text: typeof r.body === 'string' ? r.body : JSON.stringify(r.body) 
    };
  } catch (e) {
    return { ok: false, status: 0, text: String(e) };
  }
}

/** 通用 GET（返回 JSON）。 */
export async function apiGet<T = unknown>(path: string, query?: Record<string, string | number>): Promise<T> {
  const r = await ipcRequest('GET', path, query);
  if (r.status < 200 || r.status >= 300) {
    throw new Error("HTTP " + r.status + " (via IPC)");
  }
  return r.body as T;
}
