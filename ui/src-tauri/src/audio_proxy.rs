use std::{
    collections::HashMap,
    net::TcpListener as StdTcpListener,
    sync::{Arc, Mutex, OnceLock},
    time::Instant,
};

use futures_util::StreamExt;
use reqwest::{
    header::{
        ACCEPT, ACCEPT_RANGES, CONTENT_LENGTH, CONTENT_RANGE, CONTENT_TYPE, RANGE, USER_AGENT,
    },
    redirect::Policy,
    Client,
    Url,
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
}

struct RouteEntry {
    url: String,
    created_at: Instant,
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
            if let Some(oldest_id) = routes
                .iter()
                .min_by_key(|(_, route)| route.created_at)
                .map(|(id, _)| id.clone())
            {
                routes.remove(&oldest_id);
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
                created_at: Instant::now(),
            },
        );
        Ok(format!("http://127.0.0.1:{}/audio/{}", self.port(), id))
    }

    fn resolve(&self, id: &str) -> Option<String> {
        let routes = self.inner.routes.lock().ok()?;
        routes.get(id).map(|route| route.url.clone())
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
    let request = read_http_request(&mut stream).await?;
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

    let upstream = req.send().await.map_err(|e| {
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
    stream.write_all(response.as_bytes()).await.map_err(|e| {
        proxy_error(
            &route_id,
            upstream_host.as_deref(),
            Some(upstream_status),
            "response_headers",
            0,
            format!("client write failed (response headers): {e}"),
        )
    })?;
    let resume_plan = ResumePlan::from_headers(range.as_deref(), upstream_status, &headers);
    let mut body = upstream.bytes_stream();
    let mut forwarded_bytes = 0u64;
    let mut retry_count = 0usize;
    'streaming: loop {
        while let Some(chunk) = body.next().await {
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
                    let retry = client
                        .get(upstream_url.clone())
                        .header(USER_AGENT, "BottleMusic/1.0 audio proxy")
                        .header(ACCEPT, "audio/*,*/*")
                        .header(RANGE, retry_range)
                        .send()
                        .await
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
                    validate_retry_response(&retry, retry_start).map_err(|retry_err| {
                        proxy_error(
                            &route_id,
                            upstream_host.as_deref(),
                            Some(retry.status().as_u16()),
                            "upstream_retry_response",
                            forwarded_bytes,
                            retry_err,
                        )
                    })?;
                    body = retry.bytes_stream();
                    continue 'streaming;
                }
            };
            stream.write_all(&chunk).await.map_err(|e| {
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
    fn from_headers(
        request_range: Option<&str>,
        status: u16,
        headers: &reqwest::header::HeaderMap,
    ) -> Option<Self> {
        if status != 200 && status != 206 {
            return None;
        }
        let expected_len = headers
            .get(CONTENT_LENGTH)
            .and_then(|v| v.to_str().ok())
            .and_then(|v| v.parse::<u64>().ok())?;
        if expected_len == 0 {
            return None;
        }

        let (body_start, body_end) = headers
            .get(CONTENT_RANGE)
            .and_then(|v| v.to_str().ok())
            .and_then(parse_content_range_bounds)
            .unwrap_or_else(|| {
                let start = request_range.and_then(parse_range_start).unwrap_or(0);
                (start, start + expected_len - 1)
            });

        Some(Self {
            body_start,
            body_end,
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
) -> Result<(), String> {
    if response.status().as_u16() != 206 {
        return Err(format!(
            "retry returned non-partial status {}",
            response.status().as_u16()
        ));
    }
    let Some((actual_start, _)) = response
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

fn parse_range_start(range: &str) -> Option<u64> {
    let range = range.trim();
    let rest = range.strip_prefix("bytes=")?;
    let (start, _) = rest.split_once('-')?;
    start.parse::<u64>().ok()
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
                created_at: Instant::now(),
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
                created_at: Instant::now(),
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
                created_at: Instant::now(),
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

    #[test]
    fn route_survives_beyond_old_ttl_for_active_audio_element() {
        let state = AudioProxyState::new(12345);
        let url = "https://fs.wbpz.kugou.com/song.mp3".to_string();
        let registered = state.register(url.clone()).expect("should register");
        let route_id = registered.rsplit('/').next().expect("route id");

        // Simulate the audio element being paused for longer than the old
        // 10-minute TTL by backdating created_at past ROUTE_TTL.
        let old_time = Instant::now() - Duration::from_secs(601);
        {
            let mut routes = state.inner.routes.lock().unwrap();
            if let Some(entry) = routes.get_mut(route_id) {
                entry.created_at = old_time;
            }
        }

        // The route should still resolve because it is still in the route
        // table (not evicted by capacity). The audio element still holds
        // this loopback URL and should not get a 404 on resume.
        let resolved = state.resolve(route_id);
        assert!(
            resolved.is_some(),
            "route should survive beyond old TTL as long as capacity allows"
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
            "route table should be bounded by MAX_ROUTES even without TTL pruning"
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
