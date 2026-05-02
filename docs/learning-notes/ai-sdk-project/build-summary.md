# AI Chat SDK + ChatServer 搭建全纪录

> 从零搭建一个 C++17 多模型 LLM 对话 SDK，并封装为 HTTP 服务 + Web 前端。
> 最终效果：浏览器打开页面 → 选择模型 → 流式对话，支持代码高亮、markdown 渲染、会话管理。

---

## 一、整体架构

```
┌─────────────────────────────────────────────────────────┐
│                      浏览器 (HTML/JS/CSS)                │
│                  index.html + app.js + style.css         │
│                marked.js + highlight.js (CDN)           │
└──────────────────────┬──────────────────────────────────┘
                       │ HTTP + SSE (text/event-stream)
┌──────────────────────▼──────────────────────────────────┐
│                   ChatServer (C++ / cpp-httplib)         │
│  ┌─────────────────────────────────────────────────┐   │
│  │         ChatSDK (外观模式，统一入口)              │   │
│  │  ┌──────────┐  ┌─────────────┐  ┌──────────┐   │   │
│  │  │LLMManager│  │SessionManager│  │DataManager│   │   │
│  │  │模型路由  │  │会话/消息管理│  │SQLite持久化│   │   │
│  │  └────┬─────┘  └─────────────┘  └────┬─────┘   │   │
│  │       │                              │         │   │
│  │  ┌────┴──────────────────────────────┴─────┐   │   │
│  │  │ DeepSeek │ GLM │ Kimi │ Ollama          │   │   │
│  │  │ Provider │Provider│Provider│Provider    │   │   │
│  │  └────┬─────┴───┬───┴───┬───┴────┬────────┘   │   │
│  └───────┼─────────┼───────┼────────┼────────────┘   │
└──────────┼─────────┼───────┼────────┼────────────────┘
           │ HTTPS   │ HTTPS │ HTTPS  │ HTTP
    ┌──────▼──┐ ┌───▼───┐ ┌▼─────┐ ┌▼──────┐
    │DeepSeek │ │  GLM  │ │Kimi  │ │Ollama │
    │  API    │ │  API  │ │ API  │ │ 本地  │
    └─────────┘ └───────┘ └──────┘ └───────┘
```

---

## 二、核心文件清单

### SDK 层 (`SDK/`)

| 文件 | 职责 |
|------|------|
| `include/ChatSDK.h` / `src/ChatSDK.cpp` | 外观类，统一 API 入口 |
| `include/LLMProvider.h` | Provider 抽象基类（Init / SendMessage / SendMessageStream） |
| `include/LLMManager.h` / `src/LLMManager.cpp` | 模型注册、初始化、按模型名路由消息 |
| `include/SessionManager.h` / `src/SessionManager.cpp` | 会话 CRUD，消息列表管理，线程安全 |
| `include/DataManager.h` / `src/DataManager.cpp` | SQLite 持久化（sessions 表 + messages 表） |
| `include/common.h` | 数据结构：Message、Session、ApiConfig、OllamaConfig |
| `src/DeepSeekProvider.cpp` | DeepSeek API 对接（支持流式 SSE 解析） |
| `src/GLMProvider.cpp` | GLM API 对接 |
| `src/KimiProvider.cpp` | Kimi K2.6（SiliconFlow）对接，含 reasoning_content 处理 |
| `src/OllamalProvider.cpp` | Ollama 本地模型对接 |
| `include/util/myLog.h` / `src/util/myLog.cpp` | 基于 spdlog 的日志宏 |

### 服务端 (`ChatServer/`)

| 文件 | 职责 |
|------|------|
| `main.cpp` | 入口：gflags 参数解析、环境变量读取、启动服务 |
| `ChatServer.h` / `ChatServer.cpp` | HTTP 路由 + SSE 流式响应封装 |
| `ChatServer.conf` | 运行时配置文件（端口、模型参数等） |
| `CMakeLists.txt` | 构建配置（自动 FetchContent 下载 gflags） |

### 前端 (`ChatServer/www/`)

| 文件 | 职责 |
|------|------|
| `index.html` | 页面结构：侧边栏、聊天区、输入框、模态框 |
| `css/style.css` | 暗色主题（Claude 风格）、响应式布局、代码块样式 |
| `js/app.js` | 核心逻辑：会话管理、SSE 流式解析、markdown 渲染、复制 |

---

## 三、搭建过程

### 阶段 1：SDK 核心

**1.1 数据结构设计**

```cpp
// common.h
struct Message {
    std::string _message_id, _role, _content;
    std::time_t _timestamp;
};

struct Session {
    std::string _session_id, _model_name;
    std::vector<Message> _messages;
    std::time_t _createAt, _updateAt;
};

// 配置继承体系
struct Config { /* _module_name, _temperature, _max_tokens */ };
struct ApiConfig : Config { /* _apiKey */ };
struct OllamaConfig : Config { /* _end_point, _module_dec */ };
```

**1.2 Provider 抽象基类**

```cpp
class LLMProvider {
public:
    virtual bool Init(std::shared_ptr<Config>) = 0;
    virtual std::string SendMessage(messages, params) = 0;
    virtual std::string SendMessageStream(messages, params, callback) = 0;
    // callback: void(const std::string& chunk, bool done)
};
```

**1.3 LLMManager — 模型注册与路由**

- `registerModel(name, provider)` — 注册 Provider 实例
- `initModel(name, config)` — 调用 Provider::Init
- `sendMessage(modelName, messages, params)` — 按模型名找到对应 Provider 并调用

**1.4 SessionManager — 会话与消息管理**

- 内存中用 `unordered_map` 缓存所有 Session
- 每个 Session 维护 `vector<Message>`
- 读写操作同时写 SQLite（通过 DataManager）

**1.5 DataManager — SQLite 持久化**

- `sessions` 表：session_id, model_name, created_at, updated_at, message_count
- `messages` 表：message_id, session_id, role, content, timestamp
- 启动时 `LoadAllSessions()` 从 SQLite 恢复到内存

**1.6 DeepSeekProvider 流式实现**

DeepSeek API 返回 SSE 格式：
```
data: {"choices":[{"delta":{"content":"你好"}}]}

data: {"choices":[{"delta":{"content":"世界"}}]}

data: [DONE]
```

Provider 里用 `content_receiver` 逐块接收，按 `\n\n` 分割 SSE event，解析 JSON 提取 `delta.content`，通过 callback 逐字传回。

**1.7 KimiProvider — SiliconFlow 特殊处理**

Kimi K2.6（通过 SiliconFlow）的流式响应先返回 `reasoning_content`（思维链），再返回 `content`（可见文本）。Provider 优先取 `content`，为空时回退到 `reasoning_content`。

---

### 阶段 2：ChatServer HTTP 服务

**2.1 技术选型**

- HTTP 库：`cpp-httplib`（header-only）
- 参数解析：`gflags`
- 日志：`spdlog`
- JSON：`jsoncpp`
- 配置：命令行参数 + 配置文件 `ChatServer.conf`

**2.2 API 路由设计**

| 方法 | 路径 | 说明 |
|------|------|------|
| `GET` | `/api/models` | 获取可用模型列表 |
| `POST` | `/api/session` | 创建会话 |
| `GET` | `/api/sessions` | 获取会话列表 |
| `DELETE` | `/api/session/:id` | 删除会话 |
| `GET` | `/api/session/:id/history` | 获取历史消息 |
| `POST` | `/api/message` | 非流式发送消息 |
| `POST` | `/api/message/async` | 流式发送消息 (SSE) |

**2.3 SSE 流式响应**

核心实现：
```cpp
response.set_chunked_content_provider("text/event-stream", [&](size_t offset, DataSink& sink) {
    auto WriteChunk = [&](const std::string& chunk, bool last) {
        if (chunk.empty() && !last) return true;  // 跳过空 chunk

        Json::Value event;
        event["content"] = chunk;
        event["done"] = last;

        std::string payload = Json::writeString(writer, event);
        sink.write(("data: " + payload + "\n\n").c_str(), ...);

        if (last) { sink.done(); return false; }
        return true;
    };
    _ChatSDK->sendMessageStream(sessionId, message, WriteChunk);
    return true;
});
```

关键设计：
- 用 JSON 包装每个 chunk：`{"content":"...","done":false}`
- 最后发 `{"content":"","done":true}`
- 跳过空 chunk（避免 reasoning token 产生大量空事件）

**2.4 静态文件服务**

```cpp
_ChatServer->set_mount_point("/", "/home/wwh/AI_SDK/ChatServer/www");
```

**2.5 配置加载**

```cpp
// 环境变量读取 API Key
std::string deepseekKey = std::getenv("deepseekapikey") ?: "";

// gflags 读取运行时参数
DEFINE_int32(port, 8080, "服务器监听端口");
DEFINE_double(temperature, 0.7, "模型温度");
```

---

### 阶段 3：Web 前端

**3.1 技术选型**

- 纯原生 JS（无框架）
- `marked.js` (CDN) — markdown 渲染
- `highlight.js` (CDN) — 代码高亮
- CSS Grid + Flexbox — 响应式布局
- `fetch` + ReadableStream — SSE 客户端

**3.2 页面布局**

```
┌──────────┬──────────────────────────┐
│ 侧边栏   │      聊天区域              │
│          │                          │
│ 会话列表 │    ┌──────────────────┐   │
│ [+新建]  │    │  用户消息 (右)   │   │
│          │    │  AI 回复 (左)    │   │
│ ──────── │    │  代码块 + 高亮   │   │
│ 会话 1 ✓ │    │  复制按钮        │   │
│ 会话 2   │    └──────────────────┘   │
│          │                          │
│          │  ┌────────────────────┐  │
│          │  │ 输入框  [发送]     │  │
└──────────┴──────────────────────────┘
```

**3.3 核心交互流程**

```
用户输入 → sendMessage()
  → fetch POST /api/message/async
  → 读取 ReadableStream
  → 按 \n\n 分割 SSE event
  → JSON.parse({content, done})
  → 追加 content → streamingText 累计
  → textContent 实时显示（纯文本）
  → done=true → renderMarkdown + highlight → innerHTML
  → 代码块添加复制按钮
```

**3.4 CSS 暗色主题**

基于 CSS 变量实现暗色主题：
```css
:root {
    --bg-primary: #0f1117;
    --bg-secondary: #171923;
    --surface: #1e2130;
    --accent: #3b82f6;
    --text-primary: #e2e8f0;
    --border: #2d3348;
    /* ... */
}
```

**3.5 移动端适配**

```css
@media (max-width: 768px) {
    .sidebar { transform: translateX(-100%); }
    .sidebar.open { transform: translateX(0); }
    /* 汉堡菜单按钮 */
}
```

---

## 四、踩坑记录

### 坑 1：model 参数名不匹配

**现象**：前端创建会话传 `{"model":"deepseek-v4-flash"}`，后端读 `requestJson["moduleName"]`，导致始终默认用 DeepSeek。

**修复**：服务端统一用 `"model"` 字段名。

### 坑 2：模型名拼写错误

**现象**：`falsh` vs `flash`，导致模型初始化失败。

**修复**：核对 API 文档，修正拼写。

### 坑 3：Kimi 注册用了 GLMProvider

**现象**：Kimi 模型错误路由到 GLM API。

**修复**：`RegisterAllProviders()` 里为 Kimi 使用 `KimiProvider`。

### 坑 4：Kimi API 缺少 base_url

**现象**：Kimi（通过 SiliconFlow）没配置 endpoint。

**修复**：`ApiConfig` 加 `_baseUrl` 字段，Kimi 设为 `https://api.siliconflow.cn`。

### 坑 5：消息 ID 重复

**现象**：同一会话的消息 ID 相同。

**根因**：ID 计数器按值传递，未持久化；插入 SQLite 时用的是原始 message 对象而非生成 ID 后的对象。

**修复**：
- 计数器存为 SessionManager 成员变量
- 用生成 ID 后的 `msg` 对象插入数据库

### 坑 6：消息时间戳不正确

**现象**：时间戳始终为 0。

**修复**：`AddMessage` 时自动设置 `_timestamp = std::time(nullptr)`。

### 坑 7：SSE 裸文本换行丢失

**现象**：代码块里的换行被压成一行，必须刷新才能正常渲染。

**根因**：服务端最初按"每行一个 `data:`"的格式发 SSE，文本 chunk 末尾的 `\n` 丢失。

**修复**：改为 JSON 格式 SSE — `{"content":"原始chunk","done":false}`，避免文本换行与 SSE 协议换行混淆。

### 坑 8：CSS 特异性导致代码块 `white-space` 被覆盖

**现象**：代码块显示为一行，但前端收到的是正确文本。

**根因**：`.message-ai .message-bubble { white-space: normal }` 特异性（0-2-0）高于 `.message-bubble pre { white-space: pre }`（0-1-1），`<pre>` 元素的 `white-space` 被父级覆盖。

**修复**：显式加 `.message-ai .message-bubble pre, .message-ai .message-bubble pre code { white-space: pre }`（特异性 0-2-1）。

### 坑 9：流式结束前 `.trim()` 破坏 markdown 结构

**现象**：`finalizeStreaming()` 用 `state.streamingText.trim()` 后再渲染 markdown，可能抹掉代码块前后的关键换行。

**修复**：去掉 `.trim()`，渲染用原始文本，仅判断时检查 `trim()` 是否有内容。

### 坑 10：DeepSeek reasoning token 产生大量空 SSE 事件

**现象**：SSE 流里出现大量 `{"content":"","done":false}`。

**根因**：Provider 把 API 返回的空 `content` delta（reasoning token）也 callback 出来了。

**修复**：服务端 `WriteChunk` 跳过空且非 done 的 chunk：`if (chunk.empty() && !last) return true;`

### 坑 11：复制按钮在 HTTP 下失败

**现象**：`navigator.clipboard.writeText` 在非 HTTPS/非 localhost 下被浏览器禁止。

**修复**：加 `textarea + document.execCommand("copy")` fallback。

### 坑 12：静态文件 404

**现象**：`set_mount_point("/", "./www")` 因工作目录不匹配，找不到前端文件。

**修复**：改为绝对路径 `/home/wwh/AI_SDK/ChatServer/www`。

---

## 五、启动方式

```bash
# 设置环境变量
export deepseekapikey="sk-xxx"
export glmapikey="xxx"
export kimiapikey="sk-xxx"

# 构建
cd /home/wwh/AI_SDK
cmake -S ChatServer -B ChatServer/build
cmake --build ChatServer/build

# 启动
cd ChatServer
nohup ./build/AIChatServer --flagfile=ChatServer.conf --port=8080 > /tmp/aiserver.log 2>&1 &

# 浏览器访问
http://服务器IP:8080
```

---

## 六、当前运行状态

- 监听地址：`0.0.0.0:8080`
- 已注册模型：`deepseek-v4-flash`、`glm-4.7-flash`、`Pro/moonshotai/Kimi-K2.6`
- 数据库：`ChatServer/chatDB.db`（SQLite，自动创建）
- 日志：`/tmp/aiserver.log`
- 前端版本：CSS v6 / JS v7
