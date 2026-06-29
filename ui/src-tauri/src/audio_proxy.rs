use std::{
    collections::HashMap,
    net::TcpListener as StdTcpListener,
    sync::{Arc, Mutex},
    time::{Duration, Instant},
};

use futures_util::StreamExt;
use reqwest::{
    header::{
        ACCEPT, ACCEPT_RANGES, CONTENT_LENGTH, CONTENT_RANGE, CONTENT_TYPE, RANGE, USER_AGENT,
    },
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

const ROUTE_TTL: Duration = Duration::from_secs(10 * 60);
const MAX_ROUTES: usize = 128;

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
        prune_routes(&mut routes);
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
        let mut routes = self.inner.routes.lock().ok()?;
        prune_routes(&mut routes);
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
                        eprintln!("[AudioProxy WARN] request failed: {}", e);
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

    let range = header_value(&request, "range");

    let client = reqwest::Client::new();
    let mut req = client
        .get(upstream_url)
        .header(USER_AGENT, "BottleMusic/1.0 audio proxy")
        .header(ACCEPT, "audio/*,*/*");
    if let Some(range) = range {
        req = req.header(RANGE, range);
    }

    let upstream = req.send().await.map_err(|e| e.to_string())?;
    let status = upstream.status();
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
    stream
        .write_all(response.as_bytes())
        .await
        .map_err(|e| e.to_string())?;
    let mut body = upstream.bytes_stream();
    while let Some(chunk) = body.next().await {
        let chunk = chunk.map_err(|e| e.to_string())?;
        stream.write_all(&chunk).await.map_err(|e| e.to_string())?;
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
    response.push_str(&format!("Content-Length: {}\r\n", body.as_bytes().len()));
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

fn is_allowed_kugou_cdn_host(host: &str) -> bool {
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

fn prune_routes(routes: &mut HashMap<String, RouteEntry>) {
    let now = Instant::now();
    routes.retain(|_, route| now.duration_since(route.created_at) <= ROUTE_TTL);
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
            let request = format!(
                "OPTIONS /audio/route HTTP/1.1\r\nOrigin: {origin}\r\n\r\n"
            );
            let response =
                send_one_proxy_request(AudioProxyState::new(12345), &request).await;

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
