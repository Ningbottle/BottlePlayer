use std::{
    collections::HashMap,
    net::TcpListener as StdTcpListener,
    sync::{
        atomic::{AtomicU64, Ordering},
        Arc, Mutex, OnceLock,
    },
    time::Instant,
};

use futures_util::StreamExt;
use reqwest::{
    header::{
        ACCEPT, ACCEPT_RANGES, CONTENT_LENGTH, CONTENT_RANGE, CONTENT_TYPE, ETAG, IF_RANGE,
        LAST_MODIFIED, RANGE, USER_AGENT,
    },
    redirect::Policy,
    Client, Url,
};
use tauri::State;
use tokio::{
    io::{AsyncReadExt, AsyncWriteExt},
    net::{TcpListener, TcpStream},
};

#[derive(Clone)]
pub struct AudioProxyState {
    inner: Arc<AudioProxyInner>,
}

struct AudioProxyInner {
    port: u16,
    routes: Mutex<HashMap<String, RouteEntry>>,
    // Monotonic logical clock for LRU tie-breaking. Instant::now() can return
    // identical stamps for touches inside the same tick, and min_by_key over
    // equal keys would pick an arbitrary victim; every register/resolve stamps
    // a strictly increasing seq so eviction order stays deterministic.
    touch_seq: AtomicU64,
}

struct RouteEntry {
    url: String,
    // Recency marker for capacity eviction: refreshed on every resolve, so a
    // route an audio element is actively fetching is never the LRU victim.
    last_used: Instant,
    // Tie-breaker for equal last_used stamps (see AudioProxyInner::touch_seq):
    // lower seq means touched earlier, so it loses the tie.
    seq: u64,
}

const MAX_ROUTES: usize = 128;
const BODY_RETRY_LIMIT: usize = 2;
const MAX_AUDIO_REDIRECTS: usize = 5;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum RedirectDecision {
    Follow,
    Reject,
}

impl AudioProxyState {
    pub fn new(port: u16) -> Self {
        Self {
            inner: Arc::new(AudioProxyInner {
                port,
                routes: Mutex::new(HashMap::new()),
                touch_seq: AtomicU64::new(0),
            }),
        }
    }

    pub fn disabled() -> Self {
        Self::new(0)
    }

    fn port(&self) -> u16 {
        self.inner.port
    }

    fn register(&self, url: String) -> Result<String, String> {
        if self.port() == 0 {
            return Err("audio_proxy_unavailable".to_string());
        }
        if !is_supported_audio_url(&url) {
            return Err("audio_proxy_requires_http_url".to_string());
        }

        let mut routes = self
            .inner
            .routes
            .lock()
            .map_err(|_| "audio_proxy_routes_poisoned".to_string())?;
        while routes.len() >= MAX_ROUTES {
            if let Some(lru_id) = routes
                .iter()
                .min_by_key(|(_, route)| (route.last_used, route.seq))
                .map(|(id, _)| id.clone())
            {
                routes.remove(&lru_id);
            } else {
                break;
            }
        }

        let id = loop {
            let candidate = random_route_id()?;
            if !routes.contains_key(&candidate) {
                break candidate;
            }
        };
        routes.insert(
            id.clone(),
            RouteEntry {
                url,
                last_used: Instant::now(),
                seq: self.inner.touch_seq.fetch_add(1, Ordering::Relaxed),
            },
        );
        Ok(format!("http://127.0.0.1:{}/audio/{}", self.port(), id))
    }

    fn resolve(&self, id: &str) -> Option<String> {
        let mut routes = self.inner.routes.lock().ok()?;
        routes.get_mut(id).map(|route| {
            route.last_used = Instant::now();
            route.seq = self.inner.touch_seq.fetch_add(1, Ordering::Relaxed);
            route.url.clone()
        })
    }
}

pub fn bind_listener() -> Result<(StdTcpListener, u16), String> {
    let listener = StdTcpListener::bind(("127.0.0.1", 0)).map_err(|e| e.to_string())?;
    listener.set_nonblocking(true).map_err(|e| e.to_string())?;
    let port = listener.local_addr().map_err(|e| e.to_string())?.port();
    Ok((listener, port))
}

pub async fn serve(listener: StdTcpListener, state: AudioProxyState) {
    let listener = match TcpListener::from_std(listener) {
        Ok(listener) => listener,
        Err(e) => {
            eprintln!("[AudioProxy ERR] Failed to start listener: {}", e);
            return;
        }
    };

    loop {
        match listener.accept().await {
            Ok((stream, _)) => {
                let state = state.clone();
                tauri::async_runtime::spawn(async move {
                    if let Err(e) = handle_client(stream, state).await {
                        if is_client_disconnect(&e) {
                            eprintln!("[AudioProxy DEBUG] client disconnected: {}", e);
                        } else {
                            eprintln!("[AudioProxy WARN] request failed: {}", e);
                        }
                    }
                });
            }
            Err(e) => {
                eprintln!("[AudioProxy WARN] accept failed: {}", e);
                break;
            }
        }
    }
}

fn is_client_disconnect(error: &str) -> bool {
    error.contains("client write failed")
}

#[tauri::command]
pub fn audio_proxy_url(url: String, state: State<'_, AudioProxyState>) -> Result<String, String> {
    state.register(url)
}

async fn handle_client(mut stream: TcpStream, state: AudioProxyState) -> Result<(), String> {
    // R02: a client that opens a connection but never finishes its request
    // headers used to park a task (and a socket) forever. Header bytes are
    // tiny; 10s is orders of magnitude above any real client, and it cannot
    // truncate audio — it only bounds the PRE-body phase.
    let request = tokio::time::timeout(client_header_timeout(), read_http_request(&mut stream))
        .await
        .map_err(|_| "client_header_timeout".to_string())??;
    let origin = header_value(&request, "origin");
    let mut lines = request.lines();
    let request_line = lines.next().ok_or_else(|| "empty_request".to_string())?;
    let mut parts = request_line.split_whitespace();
    let method = parts.next().unwrap_or_default();
    let path = parts.next().unwrap_or_default();

    if method == "OPTIONS" {
        write_empty_response(&mut stream, 204, origin.as_deref()).await?;
        return Ok(());
    }

    if method != "GET" {
        write_text_response(&mut stream, 405, "method not allowed", origin.as_deref()).await?;
        return Ok(());
    }

    let Some(id) = path.strip_prefix("/audio/") else {
        write_text_response(&mut stream, 404, "not found", origin.as_deref()).await?;
        return Ok(());
    };

    let Some(upstream_url) = state.resolve(id) else {
        write_text_response(&mut stream, 404, "audio route expired", origin.as_deref()).await?;
        return Ok(());
    };

    let route_id = id.to_string();
    let upstream_host = Url::parse(&upstream_url)
        .ok()
        .and_then(|parsed| parsed.host_str().map(str::to_string));

    let range = header_value(&request, "range");

    // P1-H: share a process-wide Client so CDN keep-alive is reused across
    // Range seeks (was: build_audio_proxy_client() per connection).
    let client = shared_audio_proxy_client().map_err(|error| {
        proxy_error(
            &route_id,
            upstream_host.as_deref(),
            None,
            "upstream_client",
            0,
            error,
        )
    })?;
    let mut req = client
        .get(upstream_url.clone())
        .header(USER_AGENT, "BottleMusic/1.0 audio proxy")
        .header(ACCEPT, "audio/*,*/*");
    if let Some(ref range) = range {
        req = req.header(RANGE, range);
    }

    let upstream = tokio::time::timeout(upstream_response_timeout(), req.send())
        .await
        .map_err(|_| {
            proxy_error(
                &route_id,
                upstream_host.as_deref(),
                None,
                "upstream_response_headers_timeout",
                0,
                format!(
                    "upstream did not return response headers within {}s",
                    UPSTREAM_RESPONSE_TIMEOUT_SECS.load(Ordering::Acquire)
                ),
            )
        })?
        .map_err(|e| {
            proxy_error(
                &route_id,
                upstream_host.as_deref(),
                None,
                "upstream_request",
                0,
                format!("upstream request failed: {e}"),
            )
        })?;
    let status = upstream.status();
    let upstream_status = status.as_u16();
    let headers = upstream.headers().clone();

    let mut response = format!(
        "HTTP/1.1 {} {}\r\n",
        status.as_u16(),
        status_reason(status.as_u16())
    );
    append_cors_headers(&mut response, origin.as_deref());
    response.push_str("Connection: close\r\n");
    if let Some(value) = headers.get(CONTENT_LENGTH).and_then(|v| v.to_str().ok()) {
        response.push_str(&format!("Content-Length: {}\r\n", value));
    }

    if let Some(value) = headers.get(CONTENT_TYPE).and_then(|v| v.to_str().ok()) {
        response.push_str(&format!("Content-Type: {}\r\n", value));
    } else {
        response.push_str("Content-Type: audio/mpeg\r\n");
    }
    if let Some(value) = headers.get(CONTENT_RANGE).and_then(|v| v.to_str().ok()) {
        response.push_str(&format!("Content-Range: {}\r\n", value));
    }
    if let Some(value) = headers.get(ACCEPT_RANGES).and_then(|v| v.to_str().ok()) {
        response.push_str(&format!("Accept-Ranges: {}\r\n", value));
    } else {
        response.push_str("Accept-Ranges: bytes\r\n");
    }

    response.push_str("\r\n");
    tokio::time::timeout(
        client_write_timeout(),
        stream.write_all(response.as_bytes()),
    )
    .await
    .map_err(|_| {
        proxy_error(
            &route_id,
            upstream_host.as_deref(),
            Some(upstream_status),
            "client_headers_stalled",
            0,
            format!(
                "client stopped accepting response headers for {}s",
                CLIENT_WRITE_TIMEOUT_SECS.load(Ordering::Acquire)
            ),
        )
    })?
    .map_err(|e| {
        proxy_error(
            &route_id,
            upstream_host.as_deref(),
            Some(upstream_status),
            "response_headers",
            0,
            format!("client write failed (response headers): {e}"),
        )
    })?;
    let resume_plan = ResumePlan::from_headers(upstream_status, &headers);
    // Resource-change guard for resume requests: when the upstream declared a
    // validator, the retry carries If-Range — a changed resource then answers
    // 200 (not 206) and validate_retry_response fails loudly instead of
    // splicing two different files together.
    let if_range_validator = headers
        .get(ETAG)
        .or_else(|| headers.get(LAST_MODIFIED))
        .and_then(|v| v.to_str().ok())
        .map(str::to_string);
    let mut body = upstream.bytes_stream();
    let mut forwarded_bytes = 0u64;
    let mut retry_count = 0usize;
    'streaming: loop {
        while let Some(chunk) =
            match tokio::time::timeout(upstream_idle_timeout(), body.next()).await {
                Ok(next) => next,
                Err(_) => {
                    return Err(proxy_error(
                        &route_id,
                        upstream_host.as_deref(),
                        Some(upstream_status),
                        "upstream_body_idle",
                        forwarded_bytes,
                        format!(
                            "upstream sent no bytes for {}s; connection reaped",
                            UPSTREAM_IDLE_TIMEOUT_SECS.load(std::sync::atomic::Ordering::Acquire)
                        ),
                    ));
                }
            }
        {
            let chunk = match chunk {
                Ok(chunk) => chunk,
                Err(e) => {
                    let Some(plan) = resume_plan else {
                        return Err(proxy_error(
                            &route_id,
                            upstream_host.as_deref(),
                            Some(upstream_status),
                            "upstream_body",
                            forwarded_bytes,
                            format!("upstream body read failed: {e}"),
                        ));
                    };

                    let Some(retry_range) = plan.retry_range(forwarded_bytes) else {
                        return Err(proxy_error(
                            &route_id,
                            upstream_host.as_deref(),
                            Some(upstream_status),
                            "upstream_body",
                            forwarded_bytes,
                            format!("upstream body read failed after complete body: {e}"),
                        ));
                    };

                    if retry_count >= BODY_RETRY_LIMIT {
                        return Err(proxy_error(
                            &route_id,
                            upstream_host.as_deref(),
                            Some(upstream_status),
                            "upstream_body",
                            forwarded_bytes,
                            format!("upstream body read failed after retries: {e}"),
                        ));
                    }

                    retry_count += 1;
                    let retry_start = plan.body_start + forwarded_bytes;
                    let retry_request = client
                        .get(upstream_url.clone())
                        .header(USER_AGENT, "BottleMusic/1.0 audio proxy")
                        .header(ACCEPT, "audio/*,*/*")
                        .header(RANGE, retry_range);
                    let retry_request = match &if_range_validator {
                        Some(tag) => retry_request.header(IF_RANGE, tag),
                        None => retry_request,
                    };
                    let retry =
                        tokio::time::timeout(upstream_response_timeout(), retry_request.send())
                            .await
                            .map_err(|_| {
                                proxy_error(
                                    &route_id,
                                    upstream_host.as_deref(),
                                    None,
                                    "upstream_retry_headers_timeout",
                                    forwarded_bytes,
                                    format!(
                                        "upstream did not return retry response headers within {}s",
                                        UPSTREAM_RESPONSE_TIMEOUT_SECS.load(Ordering::Acquire)
                                    ),
                                )
                            })?
                            .map_err(|retry_err| {
                                proxy_error(
                                    &route_id,
                                    upstream_host.as_deref(),
                                    None,
                                    "upstream_retry_request",
                                    forwarded_bytes,
                                    format!(
                                "upstream body read failed: {e}; retry request failed: {retry_err}"
                            ),
                                )
                            })?;
                    validate_retry_response(&retry, retry_start, Some(plan.body_end)).map_err(
                        |retry_err| {
                            proxy_error(
                                &route_id,
                                upstream_host.as_deref(),
                                Some(retry.status().as_u16()),
                                "upstream_retry_response",
                                forwarded_bytes,
                                retry_err,
                            )
                        },
                    )?;
                    body = retry.bytes_stream();
                    continue 'streaming;
                }
            };
            tokio::time::timeout(client_write_timeout(), stream.write_all(&chunk))
                .await
                .map_err(|_| {
                    proxy_error(
                        &route_id,
                        upstream_host.as_deref(),
                        Some(upstream_status),
                        "client_body_stalled",
                        forwarded_bytes,
                        format!(
                            "client stopped accepting audio data for {}s",
                            CLIENT_WRITE_TIMEOUT_SECS.load(Ordering::Acquire)
                        ),
                    )
                })?
                .map_err(|e| {
                    proxy_error(
                        &route_id,
                        upstream_host.as_deref(),
                        Some(upstream_status),
                        "client_body",
                        forwarded_bytes,
                        format!("client write failed (body chunk): {e}"),
                    )
                })?;
            forwarded_bytes += chunk.len() as u64;
        }
        break;
    }
    Ok(())
}

#[derive(Clone, Copy)]
struct ResumePlan {
    body_start: u64,
    body_end: u64,
    expected_len: u64,
}

impl ResumePlan {
    // B05: the resume plan is derived from what the upstream ACTUALLY sent —
    // response status plus a valid Content-Range — never from the client's
    // request Range. The old fallback reused the request Range start for any
    // 200/206 missing Content-Range, so when an upstream ignored the Range
    // header and answered 200 with the full body, a mid-body failure resumed
    // at client_start + forwarded and silently skipped bytes.
    fn from_headers(status: u16, headers: &reqwest::header::HeaderMap) -> Option<Self> {
        let parse_len = |headers: &reqwest::header::HeaderMap| -> Option<u64> {
            headers
                .get(CONTENT_LENGTH)
                .and_then(|v| v.to_str().ok())
                .and_then(|v| v.parse::<u64>().ok())
        };
        if status == 200 {
            // A 200 is the complete representation: the body starts at 0 no
            // matter what the client asked for. (A Content-Range on a 200 is
            // malformed upstream behavior; it must not be trusted here.)
            let expected_len = parse_len(headers)?;
            if expected_len == 0 {
                return None;
            }
            return Some(Self {
                body_start: 0,
                body_end: expected_len - 1,
                expected_len,
            });
        }
        if status != 206 {
            return None;
        }
        // A 206 is only splicable with a valid Content-Range; a malformed one
        // leaves the true body start unknowable, so resume is disabled (the
        // response still streams to the client untouched — an upstream body
        // error then fails loudly instead of guessing an offset).
        let (start, end) = headers
            .get(CONTENT_RANGE)
            .and_then(|v| v.to_str().ok())
            .and_then(parse_content_range_bounds)?;
        let expected_len = parse_len(headers)?;
        if expected_len == 0 {
            return None;
        }
        // Contradictory 206 (declared window length != Content-Length) must
        // not be blind-spliced either.
        if end.checked_sub(start)? + 1 != expected_len {
            return None;
        }
        Some(Self {
            body_start: start,
            body_end: end,
            expected_len,
        })
    }

    fn retry_range(&self, forwarded_bytes: u64) -> Option<String> {
        if forwarded_bytes >= self.expected_len {
            return None;
        }
        let start = self.body_start.checked_add(forwarded_bytes)?;
        if start > self.body_end {
            return None;
        }
        Some(format!("bytes={}-{}", start, self.body_end))
    }
}

fn validate_retry_response(
    response: &reqwest::Response,
    expected_start: u64,
    expected_total: Option<u64>,
) -> Result<(), String> {
    if response.status().as_u16() != 206 {
        return Err(format!(
            "retry returned non-partial status {}",
            response.status().as_u16()
        ));
    }
    let Some((actual_start, actual_end)) = response
        .headers()
        .get(CONTENT_RANGE)
        .and_then(|v| v.to_str().ok())
        .and_then(parse_content_range_bounds)
    else {
        return Err("retry response missing valid Content-Range".to_string());
    };
    if actual_start != expected_start {
        return Err(format!(
            "retry Content-Range started at {actual_start}, expected {expected_start}"
        ));
    }
    // Resource-change guard: the retry window's end must line up with the
    // original response's window end. An upstream serving a DIFFERENT
    // resource after the failure (new encoding, trimmed file) would declare
    // a different end here.
    if let Some(expected_end_total) = expected_total {
        if actual_end != expected_end_total {
            return Err(format!(
                "retry Content-Range ends at {actual_end}, expected {expected_end_total} (upstream resource may have changed)"
            ));
        }
    }
    Ok(())
}

async fn read_http_request(stream: &mut TcpStream) -> Result<String, String> {
    let mut data = Vec::new();
    let mut buf = [0u8; 1024];
    loop {
        let n = stream.read(&mut buf).await.map_err(|e| e.to_string())?;
        if n == 0 {
            break;
        }
        data.extend_from_slice(&buf[..n]);
        if data.windows(4).any(|w| w == b"\r\n\r\n") {
            break;
        }
        if data.len() > 16 * 1024 {
            return Err("request_headers_too_large".to_string());
        }
    }

    String::from_utf8(data).map_err(|e| e.to_string())
}

async fn write_empty_response(
    stream: &mut TcpStream,
    status: u16,
    origin: Option<&str>,
) -> Result<(), String> {
    let mut response = format!("HTTP/1.1 {} {}\r\n", status, status_reason(status));
    append_cors_headers(&mut response, origin);
    response.push_str("Content-Length: 0\r\nConnection: close\r\n\r\n");
    stream
        .write_all(response.as_bytes())
        .await
        .map_err(|e| e.to_string())
}

async fn write_text_response(
    stream: &mut TcpStream,
    status: u16,
    body: &str,
    origin: Option<&str>,
) -> Result<(), String> {
    let mut response = format!("HTTP/1.1 {} {}\r\n", status, status_reason(status));
    append_cors_headers(&mut response, origin);
    response.push_str("Content-Type: text/plain; charset=utf-8\r\n");
    response.push_str(&format!("Content-Length: {}\r\n", body.len()));
    response.push_str("Connection: close\r\n\r\n");
    response.push_str(body);
    stream
        .write_all(response.as_bytes())
        .await
        .map_err(|e| e.to_string())
}

fn append_cors_headers(response: &mut String, origin: Option<&str>) {
    if let Some(origin) = origin.filter(|origin| is_allowed_origin(origin)) {
        response.push_str(&format!("Access-Control-Allow-Origin: {}\r\n", origin));
        response.push_str("Vary: Origin\r\n");
    }
    response.push_str("Access-Control-Allow-Methods: GET, OPTIONS\r\n");
    response.push_str("Access-Control-Allow-Headers: Range\r\n");
    response.push_str(
        "Access-Control-Expose-Headers: Content-Length, Content-Range, Accept-Ranges\r\n",
    );
}

fn is_supported_audio_url(url: &str) -> bool {
    let Ok(parsed) = Url::parse(url) else {
        return false;
    };
    if parsed.scheme() != "http" && parsed.scheme() != "https" {
        return false;
    }
    let Some(host) = parsed.host_str().map(|host| host.to_ascii_lowercase()) else {
        return false;
    };
    is_allowed_kugou_cdn_host(&host)
}

fn audio_redirect_decision(target: &Url, previous_hops: usize) -> RedirectDecision {
    if previous_hops >= MAX_AUDIO_REDIRECTS || !is_supported_audio_url(target.as_str()) {
        RedirectDecision::Reject
    } else {
        RedirectDecision::Follow
    }
}

/// R02: bounds the PRE-body phase (client request headers). Body streaming
/// is never bounded by this — long audio with inter-chunk pauses is normal.
/// Generous default (real clients send headers in milliseconds); tests may
/// override it with set_client_header_timeout_for_test.
static CLIENT_HEADER_TIMEOUT_SECS: std::sync::atomic::AtomicU64 =
    std::sync::atomic::AtomicU64::new(10);

fn client_header_timeout() -> std::time::Duration {
    std::time::Duration::from_secs(
        CLIENT_HEADER_TIMEOUT_SECS.load(std::sync::atomic::Ordering::Acquire),
    )
}

/// R02: bounds the upstream IDLE phase — the longest allowed gap between two
/// body bytes. This is deliberately NOT a total-duration timeout: a healthy
/// long stream keeps producing bytes and is never truncated, while a truly
/// stalled upstream (connection held open, zero bytes) is reaped. Tests may
/// shorten it with set_upstream_idle_timeout_for_test.
static UPSTREAM_IDLE_TIMEOUT_SECS: std::sync::atomic::AtomicU64 =
    std::sync::atomic::AtomicU64::new(30);

/// Bounds DNS/connect/request-header stalls before the upstream body stream
/// exists. The body itself uses the idle timeout above so long songs remain
/// unlimited in total duration.
static UPSTREAM_RESPONSE_TIMEOUT_SECS: std::sync::atomic::AtomicU64 =
    std::sync::atomic::AtomicU64::new(20);

/// A client that stops reading can block `write_all` and prevent the upstream
/// idle timer from running. Bound each downstream write to release both sides.
static CLIENT_WRITE_TIMEOUT_SECS: std::sync::atomic::AtomicU64 =
    std::sync::atomic::AtomicU64::new(30);

fn upstream_idle_timeout() -> std::time::Duration {
    std::time::Duration::from_secs(
        UPSTREAM_IDLE_TIMEOUT_SECS.load(std::sync::atomic::Ordering::Acquire),
    )
}

fn upstream_response_timeout() -> std::time::Duration {
    std::time::Duration::from_secs(
        UPSTREAM_RESPONSE_TIMEOUT_SECS.load(std::sync::atomic::Ordering::Acquire),
    )
}

fn client_write_timeout() -> std::time::Duration {
    std::time::Duration::from_secs(CLIENT_WRITE_TIMEOUT_SECS.load(Ordering::Acquire))
}

#[cfg(test)]
fn set_upstream_idle_timeout_for_test(secs: u64) {
    UPSTREAM_IDLE_TIMEOUT_SECS.store(secs, std::sync::atomic::Ordering::Release);
}

#[cfg(test)]
fn set_client_header_timeout_for_test(secs: u64) {
    CLIENT_HEADER_TIMEOUT_SECS.store(secs, std::sync::atomic::Ordering::Release);
}

#[cfg(test)]
fn set_upstream_response_timeout_for_test(secs: u64) {
    UPSTREAM_RESPONSE_TIMEOUT_SECS.store(secs, Ordering::Release);
}

#[cfg(test)]
fn set_client_write_timeout_for_test(secs: u64) {
    CLIENT_WRITE_TIMEOUT_SECS.store(secs, Ordering::Release);
}

fn build_audio_proxy_client() -> Result<Client, String> {
    Client::builder()
        .redirect(Policy::custom(|attempt| {
            match audio_redirect_decision(attempt.url(), attempt.previous().len()) {
                RedirectDecision::Follow => attempt.follow(),
                // Stop without surfacing the redirect target or its query string.
                RedirectDecision::Reject => attempt.stop(),
            }
        }))
        .build()
        .map_err(|error| format!("audio_proxy_client_build_failed: {error}"))
}

fn shared_audio_proxy_client() -> Result<&'static Client, String> {
    static CLIENT: OnceLock<Client> = OnceLock::new();
    if let Some(client) = CLIENT.get() {
        return Ok(client);
    }
    let client = build_audio_proxy_client()?;
    let _ = CLIENT.set(client);
    CLIENT
        .get()
        .ok_or_else(|| "audio_proxy_client_init_failed".to_string())
}

fn is_allowed_kugou_cdn_host(host: &str) -> bool {
    if host == "imge.kugou.com" {
        return true;
    }
    let Some(rest) = host.strip_prefix("fs.") else {
        return false;
    };
    let Some(label) = rest.strip_suffix(".kugou.com") else {
        return false;
    };
    !label.is_empty() && label.chars().all(|c| c.is_ascii_alphanumeric())
}

fn is_allowed_origin(origin: &str) -> bool {
    matches!(
        origin,
        "tauri://localhost"
            | "http://tauri.localhost"
            | "https://tauri.localhost"
            | "http://localhost:1420"
    )
}

fn header_value(request: &str, header: &str) -> Option<String> {
    request.lines().find_map(|line| {
        let (name, value) = line.split_once(':')?;
        if name.eq_ignore_ascii_case(header) {
            Some(value.trim().to_string())
        } else {
            None
        }
    })
}

fn parse_content_range_bounds(content_range: &str) -> Option<(u64, u64)> {
    let rest = content_range.trim().strip_prefix("bytes ")?;
    let (range, _) = rest.split_once('/')?;
    let (start, end) = range.split_once('-')?;
    Some((start.parse::<u64>().ok()?, end.parse::<u64>().ok()?))
}

fn random_route_id() -> Result<String, String> {
    let mut bytes = [0u8; 16];
    getrandom::getrandom(&mut bytes).map_err(|e| format!("audio_proxy_random_failed: {e}"))?;
    let mut id = String::with_capacity(32);
    for byte in bytes {
        use std::fmt::Write;
        write!(&mut id, "{:02x}", byte).map_err(|e| e.to_string())?;
    }
    Ok(id)
}

fn status_reason(status: u16) -> &'static str {
    match status {
        200 => "OK",
        204 => "No Content",
        206 => "Partial Content",
        404 => "Not Found",
        405 => "Method Not Allowed",
        502 => "Bad Gateway",
        _ => "OK",
    }
}

fn redact_url_queries(detail: &str) -> String {
    let mut redacted = String::with_capacity(detail.len());
    let mut cursor = 0;

    while cursor < detail.len() {
        let remaining = &detail[cursor..];
        let next_http = remaining.find("http://");
        let next_https = remaining.find("https://");
        let Some(relative_start) = (match (next_http, next_https) {
            (Some(http), Some(https)) => Some(http.min(https)),
            (Some(http), None) => Some(http),
            (None, Some(https)) => Some(https),
            (None, None) => None,
        }) else {
            redacted.push_str(remaining);
            break;
        };

        let url_start = cursor + relative_start;
        redacted.push_str(&detail[cursor..url_start]);
        let url_and_suffix = &detail[url_start..];
        let url_end = url_and_suffix
            .find(|character: char| {
                character.is_whitespace()
                    || matches!(character, ')' | ']' | '}' | '"' | '\'' | ',' | ';')
            })
            .unwrap_or(url_and_suffix.len());
        let url = &url_and_suffix[..url_end];
        if let Some(query_start) = url.find('?') {
            redacted.push_str(&url[..=query_start]);
            redacted.push_str("<redacted>");
        } else {
            redacted.push_str(url);
        }
        cursor = url_start + url_end;
    }

    redacted
}

fn proxy_error(
    route_id: &str,
    upstream_host: Option<&str>,
    upstream_status: Option<u16>,
    phase: &str,
    forwarded_bytes: u64,
    detail: String,
) -> String {
    let detail = redact_url_queries(&detail);
    format!(
        "route={} upstream={} status={} phase={} bytes={}: {}",
        route_id,
        upstream_host.unwrap_or("?"),
        upstream_status
            .map(|status| status.to_string())
            .unwrap_or_else(|| "?".into()),
        phase,
        forwarded_bytes,
        detail
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::Duration;
    use tokio::{
        io::{AsyncReadExt, AsyncWriteExt},
        net::{TcpListener, TcpStream},
        time::timeout,
    };

    struct TimeoutResetGuard {
        value: &'static std::sync::atomic::AtomicU64,
        restore_to: u64,
    }

    impl Drop for TimeoutResetGuard {
        fn drop(&mut self) {
            self.value.store(self.restore_to, Ordering::Release);
        }
    }

    #[test]
    fn supported_audio_url_allows_only_kugou_file_cdn_hosts() {
        assert!(is_supported_audio_url("https://fs.wbpz.kugou.com/song.mp3"));
        assert!(is_supported_audio_url("http://fs.ab12.kugou.com/song.mp3"));
        assert!(is_supported_audio_url("https://imge.kugou.com/song.mp3"));
        assert!(!is_supported_audio_url(
            "https://imge.kugou.com.evil.com/song.mp3"
        ));
        assert!(!is_supported_audio_url("file:///tmp/song.mp3"));
        assert!(!is_supported_audio_url("https://cdn.example/song.mp3"));
        assert!(!is_supported_audio_url("https://127.0.0.1/song.mp3"));
        assert!(!is_supported_audio_url(
            "http://169.254.169.254/latest/meta-data"
        ));
        assert!(!is_supported_audio_url(
            "https://gateway.kugou.com/song.mp3"
        ));
        assert!(!is_supported_audio_url("https://m.kugou.com/song.mp3"));
    }

    #[test]
    fn allowlist_rejects_suffix_and_trailing_domain_attacks() {
        // Suffix-not-match: "evilkugou.com" has no "fs." prefix and is not a
        // kugou CDN host at all.
        assert!(!is_supported_audio_url("https://evilkugou.com/song.mp3"));
        // Trailing-domain attack: the host ends in ".evil.com", so
        // strip_suffix(".kugou.com") must fail (it does NOT strip a middle
        // substring).
        assert!(!is_supported_audio_url(
            "https://fs.evil.kugou.com.evil.com/song.mp3"
        ));
        assert!(!is_supported_audio_url(
            "https://fs.kugou.com.evil.com/song.mp3"
        ));
        // Empty label between "fs." and ".kugou.com" must be rejected.
        assert!(!is_supported_audio_url("https://fs..kugou.com/song.mp3"));
        // Positive case: case-insensitivity — the impl lowercases the host
        // before matching, so mixed-case prefixes/labels are accepted.
        assert!(is_supported_audio_url(
            "https://FS.YouthAndroid.kugou.com/song.mp3"
        ));
    }

    #[test]
    fn redirect_policy_allows_kugou_cdn_to_kugou_cdn() {
        let target = Url::parse("https://fs.audio.kugou.com/song.mp3").unwrap();

        assert_eq!(
            audio_redirect_decision(&target, 0),
            RedirectDecision::Follow
        );
    }

    #[test]
    fn redirect_policy_rejects_local_private_and_non_kugou_targets() {
        for target in [
            "http://localhost:8080/song.mp3",
            "http://127.0.0.1:8080/song.mp3",
            "http://169.254.169.254/latest/meta-data",
            "http://10.0.0.4/song.mp3",
            "https://cdn.example/song.mp3",
            "https://fs.audio.kugou.com.evil.example/song.mp3",
        ] {
            let target = Url::parse(target).unwrap();
            assert_eq!(
                audio_redirect_decision(&target, 0),
                RedirectDecision::Reject,
                "redirect target should be rejected: {target}"
            );
        }
    }

    #[test]
    fn redirect_policy_limits_redirect_chain_length() {
        let target = Url::parse("https://fs.audio.kugou.com/song.mp3").unwrap();

        assert_eq!(
            audio_redirect_decision(&target, MAX_AUDIO_REDIRECTS - 1),
            RedirectDecision::Follow
        );
        assert_eq!(
            audio_redirect_decision(&target, MAX_AUDIO_REDIRECTS),
            RedirectDecision::Reject
        );
    }

    #[test]
    fn proxy_errors_redact_signed_url_query_values() {
        let error = proxy_error(
            "route-id",
            Some("fs.audio.kugou.com"),
            None,
            "upstream_request",
            0,
            "request failed for url (https://fs.audio.kugou.com/song.flac?auth=SECRET&ssig=SIGNED&token=TOKEN)".into(),
        );

        assert!(error.contains("https://fs.audio.kugou.com/song.flac?<redacted>"));
        assert!(!error.contains("SECRET"));
        assert!(!error.contains("SIGNED"));
        assert!(!error.contains("TOKEN"));
    }

    #[test]
    fn register_uses_unguessable_route_ids() {
        let state = AudioProxyState::new(12345);

        let first = state
            .register("https://fs.wbpz.kugou.com/a.mp3".to_string())
            .expect("kugou CDN URL should register");
        let second = state
            .register("https://fs.wbpz.kugou.com/b.mp3".to_string())
            .expect("kugou CDN URL should register");

        let first_id = first.rsplit('/').next().expect("route id");
        let second_id = second.rsplit('/').next().expect("route id");
        assert_ne!(first_id, second_id);
        assert_ne!(first_id, "1");
        assert_ne!(second_id, "2");
        assert_eq!(first_id.len(), 32);
        assert_eq!(second_id.len(), 32);
        assert!(first_id.chars().all(|c| c.is_ascii_hexdigit()));
        assert!(second_id.chars().all(|c| c.is_ascii_hexdigit()));
    }

    #[test]
    fn disabled_proxy_refuses_registration() {
        let state = AudioProxyState::disabled();
        assert!(state
            .register("https://cdn.example/song.mp3".to_string())
            .is_err());
    }

    #[test]
    fn client_write_errors_are_classified_as_expected_disconnects() {
        assert!(is_client_disconnect(
            "route=abc stage=client_body bytes=0 client write failed (body chunk): An established connection was aborted"
        ));
        assert!(!is_client_disconnect(
            "route=abc stage=upstream_body upstream body read failed"
        ));
    }

    #[tokio::test]
    async fn options_reflects_allowed_origin_without_wildcard() {
        let response = send_one_proxy_request(
            AudioProxyState::new(12345),
            "OPTIONS /audio/route HTTP/1.1\r\nOrigin: http://localhost:1420\r\n\r\n",
        )
        .await;

        assert!(response.contains("Access-Control-Allow-Origin: http://localhost:1420\r\n"));
        assert!(!response.contains("Access-Control-Allow-Origin: *"));
    }

    #[tokio::test]
    async fn options_omits_cors_origin_for_untrusted_origin() {
        let response = send_one_proxy_request(
            AudioProxyState::new(12345),
            "OPTIONS /audio/route HTTP/1.1\r\nOrigin: https://evil.example\r\n\r\n",
        )
        .await;

        assert!(!response.contains("Access-Control-Allow-Origin:"));
    }

    #[tokio::test]
    async fn options_omits_access_control_allow_origin_when_origin_header_absent() {
        // T6 (review gap #6, security-critical): when the request carries NO
        // Origin header at all, the response must NOT include any
        // Access-Control-Allow-Origin line (not even a wildcard). A wildcard
        // here would let any page on the loopback read the proxied audio.
        let response = send_one_proxy_request(
            AudioProxyState::new(12345),
            "OPTIONS /audio/route HTTP/1.1\r\n\r\n",
        )
        .await;

        assert!(
            !response.contains("Access-Control-Allow-Origin:"),
            "absent Origin must not produce an ACAO header; got: {response:?}"
        );
        assert!(!response.contains("Access-Control-Allow-Origin: *"));
    }

    #[tokio::test]
    async fn options_reflects_each_allowed_origin_and_rejects_evil() {
        // T7 (review gap #7): the proxy must reflect exactly the requesting
        // origin (never a wildcard) for each of the 4 allowlisted origins, and
        // must omit ACAO entirely for an untrusted origin.
        for origin in [
            "tauri://localhost",
            "http://tauri.localhost",
            "https://tauri.localhost",
            "http://localhost:1420",
        ] {
            let request = format!("OPTIONS /audio/route HTTP/1.1\r\nOrigin: {origin}\r\n\r\n");
            let response = send_one_proxy_request(AudioProxyState::new(12345), &request).await;

            assert!(
                response.contains(&format!("Access-Control-Allow-Origin: {origin}\r\n")),
                "allowed origin {origin:?} should be reflected; got: {response:?}"
            );
            assert!(
                !response.contains("Access-Control-Allow-Origin: *"),
                "origin {origin:?} must not be reflected as wildcard"
            );
        }

        // Evil origin: no ACAO at all.
        let response = send_one_proxy_request(
            AudioProxyState::new(12345),
            "OPTIONS /audio/route HTTP/1.1\r\nOrigin: https://evil.example\r\n\r\n",
        )
        .await;
        assert!(
            !response.contains("Access-Control-Allow-Origin:"),
            "evil origin must not produce an ACAO header; got: {response:?}"
        );
    }

    #[tokio::test]
    async fn get_streams_upstream_body_without_buffering_entire_response() {
        let upstream = TcpListener::bind(("127.0.0.1", 0)).await.unwrap();
        let upstream_addr = upstream.local_addr().unwrap();
        let upstream_task = tokio::spawn(async move {
            let (mut stream, _) = upstream.accept().await.unwrap();
            let mut request = [0u8; 1024];
            let _ = stream.read(&mut request).await.unwrap();
            stream
                .write_all(
                    b"HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n",
                )
                .await
                .unwrap();
            stream.flush().await.unwrap();
            tokio::time::sleep(Duration::from_millis(500)).await;
            stream.write_all(b"5\r\nworld\r\n0\r\n\r\n").await.unwrap();
        });

        let state = AudioProxyState::new(12345);
        state.inner.routes.lock().unwrap().insert(
            "stream".to_string(),
            RouteEntry {
                url: format!("http://{}/song.mp3", upstream_addr),
                last_used: Instant::now(),
                seq: 0,
            },
        );

        let listener = TcpListener::bind(("127.0.0.1", 0)).await.unwrap();
        let proxy_addr = listener.local_addr().unwrap();
        let proxy_task = tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            handle_client(stream, state).await.unwrap();
        });

        let mut client = TcpStream::connect(proxy_addr).await.unwrap();
        client
            .write_all(b"GET /audio/stream HTTP/1.1\r\nOrigin: http://localhost:1420\r\n\r\n")
            .await
            .unwrap();

        let mut collected = Vec::new();
        loop {
            let mut buf = [0u8; 256];
            let n = timeout(Duration::from_millis(250), client.read(&mut buf))
                .await
                .expect("proxy should forward the first body bytes before upstream completes")
                .unwrap();
            if n == 0 {
                break;
            }
            collected.extend_from_slice(&buf[..n]);
            if collected.windows(5).any(|w| w == b"hello") {
                break;
            }
        }

        let response = String::from_utf8_lossy(&collected);
        assert!(response.contains("hello"), "response so far: {response:?}");
        proxy_task.await.unwrap();
        upstream_task.await.unwrap();
    }

    #[tokio::test]
    async fn upstream_body_errors_report_phase_and_forwarded_byte_count() {
        let upstream = TcpListener::bind(("127.0.0.1", 0)).await.unwrap();
        let upstream_addr = upstream.local_addr().unwrap();
        let upstream_task = tokio::spawn(async move {
            let (mut stream, _) = upstream.accept().await.unwrap();
            let mut request = [0u8; 1024];
            let _ = stream.read(&mut request).await.unwrap();
            stream
                .write_all(
                    b"HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\nZ\r\n",
                )
                .await
                .unwrap();
        });

        let state = AudioProxyState::new(12345);
        state.inner.routes.lock().unwrap().insert(
            "stream".to_string(),
            RouteEntry {
                url: format!("http://{}/song.mp3", upstream_addr),
                last_used: Instant::now(),
                seq: 0,
            },
        );

        let listener = TcpListener::bind(("127.0.0.1", 0)).await.unwrap();
        let proxy_addr = listener.local_addr().unwrap();
        let proxy_task = tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            handle_client(stream, state).await
        });

        let mut client = TcpStream::connect(proxy_addr).await.unwrap();
        client
            .write_all(b"GET /audio/stream HTTP/1.1\r\nOrigin: http://localhost:1420\r\n\r\n")
            .await
            .unwrap();
        client.shutdown().await.unwrap();

        let mut response = Vec::new();
        client.read_to_end(&mut response).await.unwrap();

        let err = proxy_task
            .await
            .unwrap()
            .expect_err("truncated upstream body should fail");
        assert!(err.contains("route=stream"), "{err}");
        assert!(err.contains("upstream=127.0.0.1"), "{err}");
        assert!(err.contains("status=200"), "{err}");
        assert!(err.contains("phase=upstream_body"), "{err}");
        assert!(err.contains("bytes=5"), "{err}");

        upstream_task.await.unwrap();
    }

    #[tokio::test]
    async fn upstream_body_error_resumes_partial_content_from_failed_offset() {
        let upstream = TcpListener::bind(("127.0.0.1", 0)).await.unwrap();
        let upstream_addr = upstream.local_addr().unwrap();
        let upstream_task = tokio::spawn(async move {
            let (mut first, _) = upstream.accept().await.unwrap();
            let mut first_request = [0u8; 1024];
            let _ = first.read(&mut first_request).await.unwrap();
            first
                .write_all(
                    b"HTTP/1.1 206 Partial Content\r\nContent-Type: audio/mpeg\r\nContent-Length: 10\r\nContent-Range: bytes 100-109/200\r\nAccept-Ranges: bytes\r\n\r\nabcde",
                )
                .await
                .unwrap();
            drop(first);

            let (mut second, _) = upstream.accept().await.unwrap();
            let mut second_request = Vec::new();
            let mut buf = [0u8; 256];
            loop {
                let n = second.read(&mut buf).await.unwrap();
                if n == 0 {
                    break;
                }
                second_request.extend_from_slice(&buf[..n]);
                if second_request.windows(4).any(|w| w == b"\r\n\r\n") {
                    break;
                }
            }
            let second_request = String::from_utf8_lossy(&second_request);
            let second_request_lower = second_request.to_ascii_lowercase();
            assert!(
                second_request_lower.contains("range: bytes=105-109\r\n"),
                "retry request should resume at failed offset, got: {second_request:?}"
            );
            second
                .write_all(
                    b"HTTP/1.1 206 Partial Content\r\nContent-Type: audio/mpeg\r\nContent-Length: 5\r\nContent-Range: bytes 105-109/200\r\nAccept-Ranges: bytes\r\n\r\nfghij",
                )
                .await
                .unwrap();
        });

        let state = AudioProxyState::new(12345);
        state.inner.routes.lock().unwrap().insert(
            "stream".to_string(),
            RouteEntry {
                url: format!("http://{}/song.mp3", upstream_addr),
                last_used: Instant::now(),
                seq: 0,
            },
        );

        let listener = TcpListener::bind(("127.0.0.1", 0)).await.unwrap();
        let proxy_addr = listener.local_addr().unwrap();
        let proxy_task = tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            handle_client(stream, state).await
        });

        let mut client = TcpStream::connect(proxy_addr).await.unwrap();
        client
            .write_all(b"GET /audio/stream HTTP/1.1\r\nOrigin: http://localhost:1420\r\nRange: bytes=100-109\r\n\r\n")
            .await
            .unwrap();
        client.shutdown().await.unwrap();

        let mut response = Vec::new();
        client.read_to_end(&mut response).await.unwrap();
        let response = String::from_utf8_lossy(&response);
        assert!(response.contains("\r\n\r\nabcdefghij"), "{response:?}");
        proxy_task
            .await
            .unwrap()
            .expect("proxy should resume and complete the response body");
        upstream_task.await.unwrap();
    }

    #[tokio::test]
    async fn upstream_200_ignoring_range_resumes_from_actual_body_start() {
        // B05 regression (the audit's exact counterexample): the client asks
        // for bytes=100- but the upstream IGNORES the Range header and
        // answers 200 with the full 1000-byte body. The true body starts at
        // 0, so after forwarding 200 bytes a resume must request
        // bytes=200-999 — the old code computed 300 (client start + 200)
        // and silently skipped 100 bytes of audio.
        let payload_a = vec![b'A'; 200];
        let payload_b = vec![b'B'; 800];
        let expected_prefix = payload_a.clone();
        let expected_tail = payload_b.clone();
        let upstream = TcpListener::bind(("127.0.0.1", 0)).await.unwrap();
        let upstream_addr = upstream.local_addr().unwrap();
        let upstream_task = tokio::spawn(async move {
            let (mut first, _) = upstream.accept().await.unwrap();
            let mut first_request = [0u8; 1024];
            let _ = first.read(&mut first_request).await.unwrap();
            first
                .write_all(b"HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nContent-Length: 1000\r\nAccept-Ranges: bytes\r\n\r\n")
                .await
                .unwrap();
            first.write_all(&payload_a).await.unwrap();
            drop(first); // mid-body failure

            let (mut second, _) = upstream.accept().await.unwrap();
            let mut second_request = Vec::new();
            let mut buf = [0u8; 256];
            loop {
                let n = second.read(&mut buf).await.unwrap();
                if n == 0 {
                    break;
                }
                second_request.extend_from_slice(&buf[..n]);
                if second_request.windows(4).any(|w| w == b"\r\n\r\n") {
                    break;
                }
            }
            let second_request = String::from_utf8_lossy(&second_request).to_ascii_lowercase();
            assert!(
                second_request.contains("range: bytes=200-999\r\n"),
                "retry must continue the 200 body at the forwarded offset 200, got: {second_request:?}"
            );
            second
                .write_all(b"HTTP/1.1 206 Partial Content\r\nContent-Type: audio/mpeg\r\nContent-Length: 800\r\nContent-Range: bytes 200-999/1000\r\nAccept-Ranges: bytes\r\n\r\n")
                .await
                .unwrap();
            second.write_all(&payload_b).await.unwrap();
        });

        let state = AudioProxyState::new(12345);
        state.inner.routes.lock().unwrap().insert(
            "stream".to_string(),
            RouteEntry {
                url: format!("http://{}/song.mp3", upstream_addr),
                last_used: Instant::now(),
                seq: 0,
            },
        );

        let listener = TcpListener::bind(("127.0.0.1", 0)).await.unwrap();
        let proxy_addr = listener.local_addr().unwrap();
        let proxy_task = tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            handle_client(stream, state).await
        });

        let mut client = TcpStream::connect(proxy_addr).await.unwrap();
        client
            .write_all(b"GET /audio/stream HTTP/1.1\r\nOrigin: http://localhost:1420\r\nRange: bytes=100-\r\n\r\n")
            .await
            .unwrap();
        client.shutdown().await.unwrap();

        let mut response = Vec::new();
        client.read_to_end(&mut response).await.unwrap();
        let body = response
            .windows(4)
            .position(|w| w == b"\r\n\r\n")
            .map(|i| &response[i + 4..])
            .expect("response must contain a header/body separator");
        // Byte-level check of the FINAL stream: 200 A's + 800 B's, in order,
        // with nothing skipped and nothing duplicated.
        assert_eq!(body.len(), 1000, "client must receive the full 1000 bytes");
        assert_eq!(&body[..200], &expected_prefix[..], "first 200 bytes intact");
        assert_eq!(&body[200..], &expected_tail[..], "resumed tail intact");
        proxy_task
            .await
            .unwrap()
            .expect("proxy should resume a 200 body at the forwarded offset");
        upstream_task.await.unwrap();
    }

    #[tokio::test]
    async fn malformed_206_without_content_range_never_blind_splices() {
        // B05: a 206 without a usable Content-Range has an unknowable body
        // start. Resume must be disabled: an upstream body error fails the
        // stream loudly — it must NOT retry with a guessed offset, and no
        // second upstream request may happen.
        let upstream = TcpListener::bind(("127.0.0.1", 0)).await.unwrap();
        let upstream_addr = upstream.local_addr().unwrap();
        let upstream_task = tokio::spawn(async move {
            let (mut first, _) = upstream.accept().await.unwrap();
            let mut first_request = [0u8; 1024];
            let _ = first.read(&mut first_request).await.unwrap();
            first
                .write_all(
                    b"HTTP/1.1 206 Partial Content\r\nContent-Type: audio/mpeg\r\nContent-Length: 10\r\nAccept-Ranges: bytes\r\n\r\nabcde",
                )
                .await
                .unwrap();
            drop(first);
            // Any further accept would be a bug; the listener is dropped here.
        });

        let state = AudioProxyState::new(12345);
        state.inner.routes.lock().unwrap().insert(
            "stream".to_string(),
            RouteEntry {
                url: format!("http://{}/song.mp3", upstream_addr),
                last_used: Instant::now(),
                seq: 0,
            },
        );

        let listener = TcpListener::bind(("127.0.0.1", 0)).await.unwrap();
        let proxy_addr = listener.local_addr().unwrap();
        let proxy_task = tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            handle_client(stream, state).await
        });

        let mut client = TcpStream::connect(proxy_addr).await.unwrap();
        client
            .write_all(b"GET /audio/stream HTTP/1.1\r\nOrigin: http://localhost:1420\r\nRange: bytes=100-109\r\n\r\n")
            .await
            .unwrap();
        client.shutdown().await.unwrap();
        let mut sink = Vec::new();
        let _ = client.read_to_end(&mut sink).await;

        let err = proxy_task
            .await
            .unwrap()
            .expect_err("unknowable-offset body must fail, not guess");
        assert!(err.contains("phase=upstream_body"), "{err}");
        assert!(err.contains("bytes=5"), "{err}");
        upstream_task.await.unwrap();
    }

    #[tokio::test]
    async fn contradictory_206_window_disables_resume() {
        // B05: Content-Length (10) contradicts the declared Content-Range
        // window (100-109 = 10 bytes... here deliberately mismatched: window
        // says 10, length says 5). The two headers cannot both be true, so
        // the true body start is unknowable and NO resume plan may be built.
        // The upstream then fails mid-body: the proxy must fail loudly
        // instead of retrying with a guessed offset (no second connection).
        let upstream = TcpListener::bind(("127.0.0.1", 0)).await.unwrap();
        let upstream_addr = upstream.local_addr().unwrap();
        let upstream_task = tokio::spawn(async move {
            let (mut first, _) = upstream.accept().await.unwrap();
            let mut first_request = [0u8; 1024];
            let _ = first.read(&mut first_request).await.unwrap();
            // Declared window 100-109 (10 bytes) but Content-Length 5, and
            // only 2 of those 5 bytes arrive before the connection dies.
            first
                .write_all(
                    b"HTTP/1.1 206 Partial Content\r\nContent-Type: audio/mpeg\r\nContent-Length: 5\r\nContent-Range: bytes 100-109/200\r\nAccept-Ranges: bytes\r\n\r\nab",
                )
                .await
                .unwrap();
            drop(first);
            // Any further accept would be a bug (a retry with a guessed
            // offset); the listener is dropped here.
        });

        let state = AudioProxyState::new(12345);
        state.inner.routes.lock().unwrap().insert(
            "stream".to_string(),
            RouteEntry {
                url: format!("http://{}/song.mp3", upstream_addr),
                last_used: Instant::now(),
                seq: 0,
            },
        );

        let listener = TcpListener::bind(("127.0.0.1", 0)).await.unwrap();
        let proxy_addr = listener.local_addr().unwrap();
        let proxy_task = tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            handle_client(stream, state).await
        });

        let mut client = TcpStream::connect(proxy_addr).await.unwrap();
        client
            .write_all(b"GET /audio/stream HTTP/1.1\r\nOrigin: http://localhost:1420\r\nRange: bytes=100-109\r\n\r\n")
            .await
            .unwrap();
        client.shutdown().await.unwrap();
        let mut sink = Vec::new();
        let _ = client.read_to_end(&mut sink).await;

        let err = proxy_task
            .await
            .unwrap()
            .expect_err("contradictory 206 must not blind-splice");
        assert!(err.contains("phase=upstream_body"), "{err}");
        upstream_task.await.unwrap();
    }

    #[tokio::test]
    async fn changed_resource_after_failure_fails_retry_loudly() {
        // B05: when the original response carried a validator, the resume
        // request must carry If-Range; an upstream serving a DIFFERENT
        // resource then answers 200 (not 206) and the proxy fails with an
        // explicit retry_response error instead of splicing two files.
        let upstream = TcpListener::bind(("127.0.0.1", 0)).await.unwrap();
        let upstream_addr = upstream.local_addr().unwrap();
        let upstream_task = tokio::spawn(async move {
            let (mut first, _) = upstream.accept().await.unwrap();
            let mut first_request = [0u8; 1024];
            let _ = first.read(&mut first_request).await.unwrap();
            first
                .write_all(
                    b"HTTP/1.1 206 Partial Content\r\nContent-Type: audio/mpeg\r\nContent-Length: 10\r\nContent-Range: bytes 100-109/200\r\nAccept-Ranges: bytes\r\nETag: \"v1\"\r\n\r\nabcde",
                )
                .await
                .unwrap();
            drop(first);

            let (mut second, _) = upstream.accept().await.unwrap();
            let mut second_request = Vec::new();
            let mut buf = [0u8; 256];
            loop {
                let n = second.read(&mut buf).await.unwrap();
                if n == 0 {
                    break;
                }
                second_request.extend_from_slice(&buf[..n]);
                if second_request.windows(4).any(|w| w == b"\r\n\r\n") {
                    break;
                }
            }
            let second_request = String::from_utf8_lossy(&second_request).to_ascii_lowercase();
            assert!(
                second_request.contains("if-range: \"v1\"\r\n"),
                "retry must carry the original ETag as If-Range, got: {second_request:?}"
            );
            // The resource changed: If-Range forces a full 200.
            second
                .write_all(
                    b"HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nContent-Length: 10\r\n\r\nOTHERFILE",
                )
                .await
                .unwrap();
        });

        let state = AudioProxyState::new(12345);
        state.inner.routes.lock().unwrap().insert(
            "stream".to_string(),
            RouteEntry {
                url: format!("http://{}/song.mp3", upstream_addr),
                last_used: Instant::now(),
                seq: 0,
            },
        );

        let listener = TcpListener::bind(("127.0.0.1", 0)).await.unwrap();
        let proxy_addr = listener.local_addr().unwrap();
        let proxy_task = tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            handle_client(stream, state).await
        });

        let mut client = TcpStream::connect(proxy_addr).await.unwrap();
        client
            .write_all(b"GET /audio/stream HTTP/1.1\r\nOrigin: http://localhost:1420\r\nRange: bytes=100-109\r\n\r\n")
            .await
            .unwrap();
        client.shutdown().await.unwrap();
        let mut sink = Vec::new();
        let _ = client.read_to_end(&mut sink).await;

        let err = proxy_task
            .await
            .unwrap()
            .expect_err("changed-resource retry must fail loudly");
        assert!(
            err.contains("phase=upstream_retry_response") && err.contains("non-partial status 200"),
            "{err}"
        );
        upstream_task.await.unwrap();
    }

    #[tokio::test]
    async fn upstream_that_never_sends_response_headers_is_reaped() {
        // The body idle timer cannot run until reqwest receives response
        // headers. A CDN that accepts the TCP connection and then stays silent
        // used to hold this proxy task and both sockets without a deadline.
        let _timeout_reset = TimeoutResetGuard {
            value: &UPSTREAM_RESPONSE_TIMEOUT_SECS,
            restore_to: 20,
        };
        set_upstream_response_timeout_for_test(1);
        let upstream = TcpListener::bind(("127.0.0.1", 0)).await.unwrap();
        let upstream_addr = upstream.local_addr().unwrap();
        let upstream_task = tokio::spawn(async move {
            let (mut first, _) = upstream.accept().await.unwrap();
            let mut request = [0u8; 1024];
            let _ = first.read(&mut request).await.unwrap();
            tokio::time::sleep(Duration::from_secs(30)).await;
        });

        let state = AudioProxyState::new(12345);
        state.inner.routes.lock().unwrap().insert(
            "stream".to_string(),
            RouteEntry {
                url: format!("http://{}/song.mp3", upstream_addr),
                last_used: Instant::now(),
                seq: 0,
            },
        );

        let listener = TcpListener::bind(("127.0.0.1", 0)).await.unwrap();
        let proxy_addr = listener.local_addr().unwrap();
        let proxy_task = tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            handle_client(stream, state).await
        });

        let mut client = TcpStream::connect(proxy_addr).await.unwrap();
        client
            .write_all(b"GET /audio/stream HTTP/1.1\r\n\r\n")
            .await
            .unwrap();

        let outcome = timeout(Duration::from_secs(5), proxy_task)
            .await
            .expect("silent upstream response headers must be bounded")
            .unwrap()
            .expect_err("silent upstream must release the proxy task with an error");
        assert!(
            outcome.contains("phase=upstream_response_headers_timeout"),
            "{outcome}"
        );
        upstream_task.abort();
    }

    #[tokio::test]
    async fn downstream_that_stops_reading_is_reaped() {
        // A stopped client blocks `write_all`; while blocked, the proxy cannot
        // observe upstream progress or apply the upstream body idle timeout.
        // The client write bound must close both connections in this case.
        let _timeout_reset = TimeoutResetGuard {
            value: &CLIENT_WRITE_TIMEOUT_SECS,
            restore_to: 30,
        };
        set_client_write_timeout_for_test(1);
        let upstream = TcpListener::bind(("127.0.0.1", 0)).await.unwrap();
        let upstream_addr = upstream.local_addr().unwrap();
        let (headers_tx, headers_rx) = tokio::sync::oneshot::channel();
        let upstream_task = tokio::spawn(async move {
            let (mut first, _) = upstream.accept().await.unwrap();
            let mut request = [0u8; 1024];
            let _ = first.read(&mut request).await.unwrap();
            first
                .write_all(
                    b"HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nContent-Length: 33554432\r\n\r\n",
                )
                .await
                .unwrap();
            let _ = headers_tx.send(());
            let chunk = vec![b'x'; 64 * 1024];
            for _ in 0..512 {
                if first.write_all(&chunk).await.is_err() {
                    break;
                }
            }
        });

        let state = AudioProxyState::new(12345);
        state.inner.routes.lock().unwrap().insert(
            "stream".to_string(),
            RouteEntry {
                url: format!("http://{}/song.mp3", upstream_addr),
                last_used: Instant::now(),
                seq: 0,
            },
        );

        let listener = TcpListener::bind(("127.0.0.1", 0)).await.unwrap();
        let proxy_addr = listener.local_addr().unwrap();
        let proxy_task = tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            handle_client(stream, state).await
        });

        let mut client = TcpStream::connect(proxy_addr).await.unwrap();
        client
            .write_all(b"GET /audio/stream HTTP/1.1\r\n\r\n")
            .await
            .unwrap();
        timeout(Duration::from_secs(5), headers_rx)
            .await
            .expect("fake upstream must send headers")
            .expect("fake upstream headers signal must remain available");

        // Keep the client socket open and deliberately do not read. Once the
        // local TCP buffers fill, body writes must time out and drop the task.
        let outcome = timeout(Duration::from_secs(8), proxy_task)
            .await
            .expect("a non-reading client must not retain the proxy task")
            .unwrap()
            .expect_err("stalled downstream writes must terminate the stream");
        assert!(outcome.contains("phase=client_body_stalled"), "{outcome}");
        upstream_task.abort();
    }

    #[tokio::test]
    async fn stalled_upstream_connection_is_reaped_by_idle_timeout() {
        // R02: the upstream goes silent mid-body (connection held open, no
        // more bytes). The proxy used to park on the upstream read forever —
        // a client disconnect only surfaces on a WRITE, so nothing ever
        // failed. The per-chunk IDLE timeout now reaps the connection: the
        // task ends, and its sockets return to baseline after a skip.
        set_upstream_idle_timeout_for_test(1);
        let upstream = TcpListener::bind(("127.0.0.1", 0)).await.unwrap();
        let upstream_addr = upstream.local_addr().unwrap();
        let upstream_task = tokio::spawn(async move {
            let (mut first, _) = upstream.accept().await.unwrap();
            let mut first_request = [0u8; 1024];
            let _ = first.read(&mut first_request).await.unwrap();
            // Headers + two bytes, then stall: the connection stays open.
            first
                .write_all(
                    b"HTTP/1.1 206 Partial Content\r\nContent-Type: audio/mpeg\r\nContent-Length: 10\r\nContent-Range: bytes 0-9/10\r\nAccept-Ranges: bytes\r\n\r\nab",
                )
                .await
                .unwrap();
            // Hold the socket silently for far beyond the idle window.
            tokio::time::sleep(Duration::from_secs(30)).await;
        });

        let state = AudioProxyState::new(12345);
        state.inner.routes.lock().unwrap().insert(
            "stream".to_string(),
            RouteEntry {
                url: format!("http://{}/song.mp3", upstream_addr),
                last_used: Instant::now(),
                seq: 0,
            },
        );

        let listener = TcpListener::bind(("127.0.0.1", 0)).await.unwrap();
        let proxy_addr = listener.local_addr().unwrap();
        let proxy_task = tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            handle_client(stream, state).await
        });

        let mut client = TcpStream::connect(proxy_addr).await.unwrap();
        client
            .write_all(b"GET /audio/stream HTTP/1.1\r\nOrigin: http://localhost:1420\r\nRange: bytes=0-9\r\n\r\n")
            .await
            .unwrap();
        // Drain the headers + two stalled bytes; the client stays connected
        // (worst case: the audio element keeps the socket open while stalling).
        let mut buf = [0u8; 256];
        let _ = timeout(Duration::from_secs(5), client.read(&mut buf)).await;

        // The proxy task must NOT stay parked on the silent upstream.
        let outcome = timeout(Duration::from_secs(5), proxy_task).await;
        assert!(
            outcome.is_ok(),
            "the idle timeout must release the proxy task"
        );
        let err = outcome
            .unwrap()
            .unwrap()
            .expect_err("a stalled upstream must be reaped");
        assert!(err.contains("phase=upstream_body_idle"), "{err}");
        set_upstream_idle_timeout_for_test(30);
        upstream_task.abort();
    }

    #[tokio::test]
    async fn slow_client_request_headers_are_bounded() {
        // R02: a client that opens a connection but never finishes its
        // request headers must not park a task forever.
        set_client_header_timeout_for_test(1);
        let state = AudioProxyState::new(12345);
        let listener = TcpListener::bind(("127.0.0.1", 0)).await.unwrap();
        let proxy_addr = listener.local_addr().unwrap();
        let proxy_task = tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            handle_client(stream, state).await
        });

        let _client = TcpStream::connect(proxy_addr).await.unwrap();
        // Send nothing.

        let outcome = timeout(Duration::from_secs(5), proxy_task).await;
        assert!(outcome.is_ok(), "header timeout must release the task");
        let err = outcome
            .unwrap()
            .unwrap()
            .expect_err("a header-silent client must be dropped");
        assert!(err.contains("client_header_timeout"), "{err}");
        set_client_header_timeout_for_test(10);
    }

    #[tokio::test]
    async fn slow_drip_streaming_is_never_truncated() {
        // R02 guard for the fix itself: a REAL slow stream (gaps between
        // chunks) must stream to completion — the header timeout and the
        // client-close watch must not truncate healthy long audio.
        let chunk = vec![b'x'; 64];
        let upstream = TcpListener::bind(("127.0.0.1", 0)).await.unwrap();
        let upstream_addr = upstream.local_addr().unwrap();
        let upstream_task = tokio::spawn(async move {
            let (mut first, _) = upstream.accept().await.unwrap();
            let mut first_request = [0u8; 1024];
            let _ = first.read(&mut first_request).await.unwrap();
            first
                .write_all(
                    b"HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nContent-Length: 320\r\nAccept-Ranges: bytes\r\n\r\n",
                )
                .await
                .unwrap();
            for _ in 0..5 {
                first.write_all(&chunk).await.unwrap();
                // Real CDNs pause between chunks; well above any watchdog
                // that would be too short, far below any human timeout.
                tokio::time::sleep(Duration::from_millis(120)).await;
            }
        });

        let state = AudioProxyState::new(12345);
        state.inner.routes.lock().unwrap().insert(
            "stream".to_string(),
            RouteEntry {
                url: format!("http://{}/song.mp3", upstream_addr),
                last_used: Instant::now(),
                seq: 0,
            },
        );

        let listener = TcpListener::bind(("127.0.0.1", 0)).await.unwrap();
        let proxy_addr = listener.local_addr().unwrap();
        let proxy_task = tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            handle_client(stream, state).await
        });

        let mut client = TcpStream::connect(proxy_addr).await.unwrap();
        client
            .write_all(b"GET /audio/stream HTTP/1.1\r\nOrigin: http://localhost:1420\r\n\r\n")
            .await
            .unwrap();

        let mut raw = Vec::new();
        let mut buf = [0u8; 512];
        let deadline = tokio::time::Instant::now() + Duration::from_secs(5);
        loop {
            let body_start = raw.windows(4).position(|w| w == b"\r\n\r\n").map(|i| i + 4);
            if let Some(start) = body_start {
                if raw.len() - start >= 320 {
                    break;
                }
            }
            let remaining = deadline.saturating_duration_since(tokio::time::Instant::now());
            assert!(
                !remaining.is_zero(),
                "slow stream must complete within budget"
            );
            let n = timeout(remaining, client.read(&mut buf))
                .await
                .unwrap()
                .unwrap_or(0);
            if n == 0 {
                break;
            }
            raw.extend_from_slice(&buf[..n]);
        }
        let sep = raw
            .windows(4)
            .position(|w| w == b"\r\n\r\n")
            .expect("response must contain a header/body separator");
        let body = &raw[sep + 4..];
        assert_eq!(
            body.len(),
            320,
            "all 320 body bytes must arrive despite the gaps"
        );
        proxy_task
            .await
            .unwrap()
            .expect("proxy completes a healthy slow stream");
        upstream_task.await.unwrap();
    }

    #[test]
    fn route_survives_long_pause_while_capacity_allows() {
        let state = AudioProxyState::new(12345);
        let url = "https://fs.wbpz.kugou.com/song.mp3".to_string();
        let registered = state.register(url.clone()).expect("should register");
        let route_id = registered.rsplit('/').next().expect("route id");

        // Simulate the audio element being paused by backdating last_used;
        // capacity eviction must be the only way this route can disappear.
        let old_time = Instant::now() - Duration::from_secs(30);
        {
            let mut routes = state.inner.routes.lock().unwrap();
            if let Some(entry) = routes.get_mut(route_id) {
                entry.last_used = old_time;
            }
        }

        // The route should still resolve because it is still in the route
        // table (not evicted by capacity). The audio element still holds
        // this loopback URL and should not get a 404 on resume.
        let resolved = state.resolve(route_id);
        assert!(
            resolved.is_some(),
            "route should survive a long pause as long as capacity allows"
        );
        assert_eq!(resolved, Some(url));
    }

    #[test]
    fn route_table_stays_bounded_by_max_routes() {
        let state = AudioProxyState::new(12345);
        for i in 0..(MAX_ROUTES + 10) {
            let url = format!("https://fs.ab{:03}.kugou.com/song.mp3", i);
            state.register(url).expect("should register");
        }
        let count = state.inner.routes.lock().unwrap().len();
        assert_eq!(
            count, MAX_ROUTES,
            "route table should stay bounded by MAX_ROUTES via capacity eviction"
        );
    }

    #[test]
    fn active_route_is_not_evicted_when_capacity_is_full() {
        let state = AudioProxyState::new(12345);

        // Fill the table to capacity; the first route registered will be the
        // least-recently-used once the table is full.
        let first = state
            .register("https://fs.ab000.kugou.com/active.mp3".to_string())
            .expect("should register first route");
        let active_id = first.rsplit('/').next().expect("route id").to_string();
        for i in 1..MAX_ROUTES {
            let url = format!("https://fs.ab{:03}.kugou.com/song.mp3", i);
            state.register(url).expect("should register");
        }
        assert_eq!(state.inner.routes.lock().unwrap().len(), MAX_ROUTES);

        // The audio element is still fetching this route: a resolve refreshes
        // its recency marker, so it must not be picked as the eviction victim.
        let resolved = state.resolve(&active_id).expect("active route resolves");
        assert_eq!(resolved, "https://fs.ab000.kugou.com/active.mp3");

        // Overflow the table: eviction must take some other (stale) route.
        state
            .register("https://fs.overflow.kugou.com/new.mp3".to_string())
            .expect("should register after evicting a stale route");

        let routes = state.inner.routes.lock().unwrap();
        assert_eq!(
            routes.len(),
            MAX_ROUTES,
            "table must stay bounded by MAX_ROUTES"
        );
        assert!(
            routes.contains_key(&active_id),
            "the actively used route must not be evicted when capacity is reached"
        );
    }

    #[test]
    fn eviction_falls_back_to_least_recently_used_when_every_route_is_active() {
        let state = AudioProxyState::new(12345);
        let mut first_id = String::new();
        for i in 0..MAX_ROUTES {
            let url = format!("https://fs.full{:03}.kugou.com/song.mp3", i);
            let registered = state.register(url).expect("should register");
            if i == 0 {
                first_id = registered.rsplit('/').next().expect("route id").to_string();
            }
        }

        // Extreme case: every entry was resolved (touched) recently, so no
        // route is exempt from eviction. Stamp them all with an identical
        // last_used so the decision falls entirely on the seq tie-breaker:
        // the victim must be the route with the oldest touch seq, i.e. the
        // first one registered.
        let stamp = Instant::now();
        {
            let mut routes = state.inner.routes.lock().unwrap();
            for entry in routes.values_mut() {
                entry.last_used = stamp;
            }
        }
        state
            .register("https://fs.overflow.kugou.com/new.mp3".to_string())
            .expect("insert must succeed even when all existing routes are active");

        let routes = state.inner.routes.lock().unwrap();
        assert_eq!(
            routes.len(),
            MAX_ROUTES,
            "all-active worst case must still leave the table bounded"
        );
        assert!(
            !routes.contains_key(&first_id),
            "with every last_used stamp equal, the victim must be the route with the oldest touch seq (the first registered)"
        );
    }

    async fn send_one_proxy_request(state: AudioProxyState, request: &str) -> String {
        let listener = TcpListener::bind(("127.0.0.1", 0)).await.unwrap();
        let addr = listener.local_addr().unwrap();
        let server = tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            handle_client(stream, state).await.unwrap();
        });

        let mut client = TcpStream::connect(addr).await.unwrap();
        client.write_all(request.as_bytes()).await.unwrap();
        client.shutdown().await.unwrap();

        let mut response = Vec::new();
        client.read_to_end(&mut response).await.unwrap();
        server.await.unwrap();
        String::from_utf8(response).unwrap()
    }
}
