use serde::{Deserialize, Serialize};
use std::sync::OnceLock;
use std::time::Duration;

const DEEPSEEK_API_URL: &str = "https://api.deepseek.com/chat/completions";
const DEEPSEEK_MODEL: &str = "deepseek-chat";
const REQUEST_TIMEOUT: Duration = Duration::from_secs(30);

fn shared_ai_client() -> Result<&'static reqwest::Client, String> {
    static CLIENT: OnceLock<reqwest::Client> = OnceLock::new();
    if let Some(c) = CLIENT.get() {
        return Ok(c);
    }
    let built = reqwest::Client::builder()
        .timeout(REQUEST_TIMEOUT)
        .build()
        .map_err(|e| format!("Failed to create HTTP client: {}", e))?;
    let _ = CLIENT.set(built);
    CLIENT
        .get()
        .ok_or_else(|| "ai_client_init_failed".to_string())
}

const DEFAULT_SYSTEM_PROMPT: &str = "You are a music listening analyst. Analyze the user's listening statistics and provide insights about their music taste, listening patterns, and recommendations. Respond in the same language as the user's prompt. Be concise and insightful.";

#[derive(Serialize)]
struct ChatMessage {
    role: String,
    content: String,
}

#[derive(Serialize)]
struct ChatRequest {
    model: String,
    messages: Vec<ChatMessage>,
    stream: bool,
}

#[derive(Deserialize)]
struct ChatResponse {
    choices: Vec<Choice>,
}

#[derive(Deserialize)]
struct Choice {
    message: ChatMessageResponse,
}

#[derive(Deserialize)]
struct ChatMessageResponse {
    content: String,
}

#[tauri::command]
pub async fn ai_analyze(
    api_key: String,
    stats_json: String,
    custom_prompt: Option<String>,
) -> Result<String, String> {
    if api_key.is_empty() {
        return Err("API key is required".to_string());
    }

    let user_content = custom_prompt
        .map(|p| format!("{}\n\nListening Statistics:\n{}", p, stats_json))
        .unwrap_or_else(|| {
            format!(
                "Please analyze my listening statistics and provide insights:\n\n{}",
                stats_json
            )
        });

    let request = ChatRequest {
        model: DEEPSEEK_MODEL.to_string(),
        messages: vec![
            ChatMessage {
                role: "system".to_string(),
                content: DEFAULT_SYSTEM_PROMPT.to_string(),
            },
            ChatMessage {
                role: "user".to_string(),
                content: user_content,
            },
        ],
        stream: false,
    };

    let client = shared_ai_client()?;

    let response = client
        .post(DEEPSEEK_API_URL)
        .header("Authorization", format!("Bearer {}", api_key))
        .json(&request)
        .send()
        .await
        .map_err(|e| format!("DeepSeek API request failed: {}", e))?;

    let status = response.status();
    let body = response
        .text()
        .await
        .map_err(|e| format!("Failed to read response body: {}", e))?;

    if !status.is_success() {
        return Err(format!("DeepSeek API error ({}): {}", status.as_u16(), body));
    }

    let chat_response: ChatResponse = serde_json::from_str(&body)
        .map_err(|e| format!("Failed to parse DeepSeek response: {}", e))?;

    chat_response
        .choices
        .first()
        .map(|c| c.message.content.clone())
        .ok_or_else(|| "Empty response from DeepSeek API".to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_chat_request_serialization() {
        let req = ChatRequest {
            model: "deepseek-chat".to_string(),
            messages: vec![
                ChatMessage {
                    role: "system".to_string(),
                    content: "You are helpful.".to_string(),
                },
                ChatMessage {
                    role: "user".to_string(),
                    content: "Hello".to_string(),
                },
            ],
            stream: false,
        };
        let json = serde_json::to_string(&req).unwrap();
        assert!(json.contains("deepseek-chat"));
        assert!(json.contains("\"stream\":false"));
    }

    #[test]
    fn test_chat_response_deserialization() {
        let raw = r#"{
            "choices": [
                {"message": {"role": "assistant", "content": "Great taste!"}}
            ]
        }"#;
        let resp: ChatResponse = serde_json::from_str(raw).unwrap();
        assert_eq!(resp.choices[0].message.content, "Great taste!");
    }

    #[tokio::test]
    async fn test_ai_analyze_rejects_empty_key() {
        let result = ai_analyze("".to_string(), "{}".to_string(), None).await;
        assert!(result.is_err());
        assert_eq!(result.unwrap_err(), "API key is required");
    }
}
