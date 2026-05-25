// 前端 → C++ 后端的统一入口。
//
// 设计：
//   - Tauri 启动时拉起 EchoCompatServer sidecar 监听 127.0.0.1:6609。
//   - 这里直接 fetch loopback；不经 Rust 中转，少一跳。
//   - 若以后改端口/换 IPC 协议，只动 BASE 即可。

import { invoke } from '@tauri-apps/api/core';

let cachedBase: string | null = null;

async function base(): Promise<string> {
  if (cachedBase) return cachedBase;
  try {
    cachedBase = await invoke<string>('backend_base_url');
  } catch {
    cachedBase = 'http://127.0.0.1:6609';
  }
  return cachedBase;
}

export async function ping(): Promise<string> {
  return invoke<string>('ping');
}

/** 探测后端是否就绪（轮询用）。后端约定有 /healthz；若无则用 / 兜底。 */
export async function backendHealth(): Promise<{ ok: boolean; status: number; text?: string }> {
  const url = (await base()) + '/healthz';
  try {
    const r = await fetch(url, { method: 'GET' });
    return { ok: r.ok, status: r.status, text: await r.text().catch(() => undefined) };
  } catch (e) {
    return { ok: false, status: 0, text: String(e) };
  }
}

/** 通用 GET（返回 JSON）。 */
export async function apiGet<T = unknown>(path: string, query?: Record<string, string | number>): Promise<T> {
  const url = new URL((await base()) + path);
  if (query) {
    for (const [k, v] of Object.entries(query)) {
      url.searchParams.set(k, String(v));
    }
  }
  const r = await fetch(url.toString());
  if (!r.ok) throw new Error(`HTTP ${r.status} ${r.statusText}`);
  return (await r.json()) as T;
}
