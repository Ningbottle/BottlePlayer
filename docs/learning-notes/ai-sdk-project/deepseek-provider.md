# 1. 前言：为什么要接入 DeepSeek？
前面我们已经搞定了大模型的基类设计，这篇咱们来聊聊当红炸子鸡——DeepSeek 的完整接入过程。DeepSeek 为什么重要？因为它在逻辑推理和代码生成上表现极其优异，而且其 API 规范完全兼容 OpenAI。这意味着只要我们搞懂了 DeepSeek 的底层数据包是怎么封装的，以后接入其他主流云端模型也就手到擒来了。这篇咱们通过继承我们设计好的 `LLMProvider` 基类，一步步把 `DeepSeekProvider` 的内部机制彻底扒开。
![无缝接入](file:///C:/Users/w1521/.gemini/antigravity/brain/8b0fcc1f-c673-43ec-b7cc-9311a41c4355/ds_01_plugin_1782265847774.png)
# 2. 头文件设计：继承基类的大门
首先来看我们的头文件声明。C++ 的**多态（Polymorphism）**特性允许我们用一个统一的基类指针来操作不同的派生类。我们让 `DeepSeekProvider` 公开继承（`public`）`LLMProvider`，并使用 `override` 关键字重写基类的纯虚函数。这也是典型的**外观模式（Facade Pattern）**底层建筑，上层业务不需要知道 DeepSeek 是怎么工作的，只要调用这些虚函数就行了。
```cpp
// include/DeepSeekProvider.h
#pragma once
#include "LLMProvider.h"
namespace AI_Chat_SDK {
    class DeepSeekProvider : public LLMProvider {
    public:
        virtual ~DeepSeekProvider() = default;
        // 1. 初始化模型提供者，使用配置参数
        virtual bool Init(const std::map<std::string, std::string>& config) override;
        // 2. 是否初始化完成
        virtual bool IsAvailable() const override;
        // 3. 获取模型名称
        virtual std::string GetModelName() const override;
        // 4. 发送消息，非流式，就是非增量 第一个参数是消息，第二个参数是温度，最大tokens，之类的
        virtual std::string SendMessage(std::vector<Message> messages,
                            std::map<std::string, std::string> requestParam) override;
        // 5. 发送消息，流式: 第一个参数是消息，第二个参数是温度，最大tokens，之类的，第三个参数是回调函数
        // 回调函数里面第一个是增量内容，第二个是标志是否传输完毕
        virtual std::string SendMessageStream(std::vector<Message> messages,
                                    std::map<std::string, std::string> requestParam,
                                    std::function<void(std::string, bool)>) override;
        // 6. 获取模型描述
        virtual std::string ModelDesc() const override;
    };
} // namespace AI_chat
```
# 3. 基础信息的搭建：初始化与状态判断
接下来进入源文件的实现。我们要实现的最基础的方法有四个：`Init`、`IsAvailable`、`GetModelName` 和 `ModelDesc`。
大模型服务商的 API 是典型的**无状态协议（Stateless Protocol）**，它不认识你是谁，只认你携带的通行证。这个通行证就是 `API Key`。`Init` 函数的作用，就是从大管家（上层传入的 `config` 字典）那里拿到这把钥匙，以及请求的根地址（`base_url` 或称 `Endpoint`）。如果用户没传地址，我们就默认使用 DeepSeek 的官方地址。拿到后，我们将内部状态 `_IsAvailable` 标记为真。
![门禁鉴权](file:///C:/Users/w1521/.gemini/antigravity/brain/8b0fcc1f-c673-43ec-b7cc-9311a41c4355/ds_02_auth_1782265858768.png)
```cpp
#include "../include/DeepSeekProvider.h"
#include <jsoncpp/json/config.h>
#include <jsoncpp/json/forwards.h>
#include <jsoncpp/json/json.h>
#include <jsoncpp/json/reader.h>
#include <jsoncpp/json/writer.h>
#include "../include/common.h"
#include <httplib.h>
#include <sstream>

namespace AI_Chat_SDK {
    bool DeepSeekProvider::Init(const std::map<std::string, std::string>& config)
    {
        auto it = config.find("api_key");
        if (it == config.end()) return false;
        _ApiKey = it->second;
        it = config.find("base_url");
        if (it == config.end()) _Endpoint = "https://api.deepseek.com";
        else _Endpoint = it->second;
        _IsAvailable = true;
        INFO("DeepSeekProvider initModel success, endpoint: {}",_Endpoint);
        return true;
    }

    bool DeepSeekProvider::IsAvailable() const { return _IsAvailable;}
    std::string DeepSeekProvider::GetModelName() const  { return "deepseek-v4-flash";}
    std::string DeepSeekProvider::ModelDesc() const
    {
        return "这是一个世界顶级的ai 助手，DeepSeek,擅长解题和代码以及逻辑推理";
    }
```
# 4. 深入剖析：SendMessage 的非流式逻辑
## 4.1 非流式 vs 流式，区别在哪？
我们要实现大模型聊天，第一种方式就是“非流式”。非流式就像寄平信，你把整条历史记录打包发过去，服务器在那边吭哧吭哧写完全部的回信，最后一次性扔给你。在这个过程中，客户端必须**阻塞等待（Blocking Wait）**，用户界面看起来就是卡住不动，体验非常糟糕。但这部分是基础，必须先搞懂它。

## 4.2 请求结构到底长啥样？
不管是流式还是非流式，咱们在发给 DeepSeek 之前，都必须把内存里的 C++ 结构体组装成一段标准的 JSON。最终发送给服务器的数据包长这个样子：
```json
{
  "model": "deepseek-v4-flash",
  "messages": [
    {"role": "user", "content": "你好"},
    {"role": "assistant", "content": "你好，有什么可以帮你的？"}
  ],
  "temperature": 0.7,
  "max_tokens": 4096
}
```
## 4.3 httplib 替我们干了什么？
咱们在底层用到的是开源神器 `cpp-httplib`。网络通信是很繁杂的，你要处理底层的 TCP Socket、连接池、甚至 SSL 证书握手。`httplib` 把这些全屏蔽了。它提供了两个极度简化的核心功能：
1. **网络管家**：帮我们设置连接超时（`set_connection_timeout`）和读取超时（`set_read_timeout`），这在极度容易卡壳的 LLM 接口调用中非常关键。
2. **极简请求**：我们只需要实例化 `httplib::Client`，装配好 `Headers`，调用一个 `.Post()`，剩下的握手和数据传送全由它代劳。

## 4.4 函数逻辑深度拆解
接下来看看这段代码到底是怎么跑起来的，我把逻辑分为了这几步：
1. **入参提取**：从 `requestParam` 里把 `temperature` 和 `max_tokens` 这两个控制大模型“随机性”和“回答长度”的玄学参数取出来。
2. **组装 JSON 数组**：遍历传入的 `std::vector<Message>`，把每个 `role` 和 `content` 填入 JSON 对象，塞进 `MessageArray`。
3. **序列化（Serialization）**：计算机网卡只认识一串 0 和 1 构成的字符串，咱们用 `Json::writeString` 把刚才拼好的 JSON 对象转换成可发送的字符串 `requestBodyStr`。
4. **发起 HTTP POST 请求**：注意，这里我们在 Header 中带上了 `{"Authorization", "Bearer " + _ApiKey}`，相当于给请求加上了门禁卡。
5. **反序列化（Deserialization）与提取**：等待请求成功后（`res->status == 200`），收到的 `res->body` 是一串巨长的 JSON 字符串。我们必须把它逆向解析成 JSON 对象，然后一层一层剥开 `choices[0].message.content` 获取最终的文本。
![打包JSON POST](file:///C:/Users/w1521/.gemini/antigravity/brain/8b0fcc1f-c673-43ec-b7cc-9311a41c4355/ds_03_post_1782265868574.png)
```cpp
    std::string DeepSeekProvider::SendMessage(std::vector<Message> messages,
                    std::map<std::string, std::string> requestParam)
    {
        // 1. 检测模型是否进行初始化了
        if(!_IsAvailable) return "";
        // 2. 构造请求参数：模型名称、消息列表、温度值、max_tokens数
        double temperature = 0.7;
        int max_output_tokens = 4096;
        if(requestParam.find("temperature") != requestParam.end())
            temperature = std::stod(requestParam["temperature"]);
        if(requestParam.find("max_tokens") != requestParam.end())
            max_output_tokens = std::stoi(requestParam["max_tokens"]);
        
        // 3. 构建消息列表
        Json::Value MessageArray(Json::arrayValue);
        for(const auto& msg : messages)
        {
            Json::Value message;
            message["role"] = msg._role;
            message["content"] = msg._content;
            MessageArray.append(message);
        }
        
        // 4. 构建完整的请求体
        Json::Value requestBody;
        requestBody["model"] = GetModelName();
        requestBody["messages"] = MessageArray;
        requestBody["temperature"] = temperature;
        requestBody["max_tokens"] = max_output_tokens;
        
        // 5. 序列化：把 Json 对象转成网络传输需要的字符串
        Json::StreamWriterBuilder WriteBuilder;
        std::string requestBodyStr = Json::writeString(WriteBuilder, requestBody);
        
        // 6. 创建客户端，使用第三方 httplib
        httplib::Client client(_Endpoint.c_str());
        client.set_connection_timeout(30,0);
        client.set_read_timeout(60,0);
        httplib::Headers headers = {
            {"Authorization", "Bearer " + _ApiKey},
            {"Content-Type", "application/json"}
        };
        
        // 7. 发送 POST 请求 (阻塞等待)
        auto res = client.Post("/chat/completions", headers, requestBodyStr, "application/json");
        if(!res)
        {
            ERROR("DeepSeekProvider sendMessage POST request failed, error: {}",httplib::to_string(res.error()));
            return "";
        }
        if(res->status != 200)
        {
            ERROR("DeepSeekProvider sendMessage POST request failed, status: {}", res->status);
            return "";
        }
        
        // 8. 反序列化解析响应体
        Json::Value root;
        Json::CharReaderBuilder readerBuilder;
        std::istringstream responseStream(res->body);
        std::string errorJson;
        Json::Value requestJson;
        if(!Json::parseFromStream(readerBuilder,responseStream, &requestJson, &errorJson))
        {
            ERROR("DeepSeekProvider sendMessage parse response body failed, error: {}", errorJson);
            return "";
        }
        
        // 9. 提取响应内容并返回
        std::string responseContent = requestJson["choices"][0]["message"]["content"].asString();
        return responseContent;
    }
```
# 5. 硬核解析：SendMessageStream 流式输出的挑战
## 5.1 为什么流式更难？难点在哪里？
现在的聊天应用都是“打字机”效果。如果一直让用户干等着，体验太差。所以我们需要改用**流式输出（Streaming）**。这在实现上有三大难点：
1. **协议机制变了**：流式采用的是 **SSE（Server-Sent Events）流式协议**。此时服务器不会一次性发送完整的 JSON 并断开，而是保持连接长开，源源不断地向我们推送极小的数据块（Chunks）。
2. **拆包难**：我们收到的数据是碎片化的，而且协议规定，每一次数据推送都以 `\n\n` 结尾。并且前边会带有一个特定的前缀，比如 `data: `。它的原始长相是这样的：
   ```text
   data: {"choices":[{"delta":{"content":"你"}}]}

   data: {"choices":[{"delta":{"content":"好"}}]}

   data: [DONE]
   ```
   我们必须手写一个“拆包机”，实时去扫描 `\n\n` 来判断当前这段数据是否完整。
3. **接管底层读取逻辑**：我们不能再等 httplib 执行完 `Post` 再拿结果了。我们必须挂载一个底层的钩子函数（也就是 `content_receiver`），网络卡一接到数据，就立刻触发我们的回调，做到“来一点处理一点”。

## 5.2 函数逻辑深度拆解
这段代码非常长，但如果你顺着我的思路拆开看，会觉得十分精妙：
1. **加挂流式档位**：在组装请求参数时，必须显式加上 `requestBody["stream"] = true`。然后在 Header 头里加上 `{"Accept", "text/event-stream"}`，这是向服务器喊话：“我准备好一直接收数据包了”。
2. **定制化 Request 对象**：这一次咱们不能调用傻瓜版的 `.Post` 方法，而是自己手动实例化一个 `httplib::Request` 对象，把刚才的数据结构硬核绑定上去。
3. **编写底层拆包拦截器 (`content_receiver`)**：这是全篇最难啃的骨头。
   - 每当服务器发来一段原始字符指针 `data`，我们先把这个包裹一股脑全拼接进我们的总缓冲区 `buff` 里。
   - 随后进入一个巨大的 `while` 循环，**寻找双换行符 `\n\n`**。如果没找到，证明这句话没说完，退出循环继续等。
   - 如果找到了，用剪刀（`substr`）把这一截 `chunk` 剪下来，剩下的继续放回 `buff` 里等。
   - 判断这截 `chunk` 是不是以 `data: ` 开头。如果这截里传过来的是 `[DONE]`，说明服务器闭嘴了，我们调用外部传入的 `callback("", true)` 通知上层界面：可以结束打字机动画了。
   - 如果不是 `[DONE]`，我们就把后面的 JSON 片段提取出来，进行二次**反序列化**，解析出 `delta.content`，并调用上层 callback（传入 false），此时上层的 UI 界面上就会立刻蹦出一个新字！这就是**回调机制（Callback Mechanism）**的威力。
![流式拆包](file:///C:/Users/w1521/.gemini/antigravity/brain/8b0fcc1f-c673-43ec-b7cc-9311a41c4355/ds_04_stream_1782265879321.png)
![回调上层](file:///C:/Users/w1521/.gemini/antigravity/brain/8b0fcc1f-c673-43ec-b7cc-9311a41c4355/ds_05_callback_1782265890410.png)
```cpp
    std::string DeepSeekProvider::SendMessageStream(std::vector<Message> messages,
                                std::map<std::string, std::string> requestParam,
                                std::function<void(std::string, bool)> callback)
    {
        if(!IsAvailable()) {return "";}
        
        double temperature = 0.7;
        int max_output_tokens = 4096;
        if(requestParam.find("temperature") != requestParam.end())
            temperature = std::stod(requestParam["temperature"]);
        if(requestParam.find("max_tokens") != requestParam.end())
            max_output_tokens = std::stoi(requestParam["max_tokens"]);

        // 1. 构建请求体并加入 stream = true
        Json::Value requestBody;
        requestBody["model"] = GetModelName();
        Json::Value messageArray;
        for(const auto& message : messages)
        {
            Json::Value messageItem;
            messageItem["role"] = message._role;
            messageItem["content"] = message._content;
            messageArray.append(messageItem);
        }
        requestBody["messages"] = messageArray;
        requestBody["temperature"] = temperature;
        requestBody["max_tokens"] = max_output_tokens;
        requestBody["stream"] = true; // 开启流式响应的关键钥匙

        Json::StreamWriterBuilder WriteBuilder;
        WriteBuilder.settings_["indentation"] = ""; // 紧凑输出，减少网络体积
        std::unique_ptr<Json::StreamWriter> writer(WriteBuilder.newStreamWriter());
        std::stringstream ss;
        writer->write(requestBody, &ss);
        std::string requestBodyStr = ss.str();
        
        httplib::Client client(_Endpoint.c_str());
        client.set_connection_timeout(30,0);
        client.set_read_timeout(300,0); // 流式读取超时时间要放宽
        
        // 2. 构建包含 Accept 协议的请求头
        httplib::Headers headers = {
            {"Authorization", "Bearer " + _ApiKey},
            {"Content-Type", "application/json"},
            {"Accept", "text/event-stream"}         // 告诉服务器我们接受 SSE 数据流
        };
        
        std::string buff;           // 接收流式响应的数据块拼接区
        bool gotError = false;      
        std::string MsgError;       
        int statusCode = 0;         
        bool IsComplete = false;    // 记录响应是否传输完成
        std::string fullResponse;   // 记录最终拼接出的完整回复

        // 3. 手动实例化 Request 对象
        httplib::Request request;
        request.method = "POST";
        request.path = "/v1/chat/completions";
        request.headers = headers;
        request.body = requestBodyStr;
        
        // 4. 设置 HTTP 错误处理钩子
        request.response_handler = [&](const httplib::Response& res) {
            statusCode = res.status;
            if (res.status != 200)
            {
                gotError = true;
                MsgError = res.body;
                ERROR("Request failed with status code: {}, body: {}", res.status, MsgError);
                return false;
            }
            return true;
        };
        
        // 5. 重点：挂载 content_receiver，处理传送带上的数据流
        request.content_receiver = [&](const char* data, size_t len,uint64_t offset, uint64_t totalLength)
        {
            if(gotError == true) return false;
            buff.append(data, len); // 把刚收到的包裹扔进缓冲区
            
            ssize_t pos;
            // 找包裹之间的切分标志 \n\n
            while((pos = buff.find("\n\n")) != std::string::npos)
            {
                std::string chunk = buff.substr(0, pos);
                buff = buff.substr(pos + 2); // 剩下的内容留给下一次处理
                
                // 忽略心跳包空行和冒号开头的行
                if(chunk.empty() || chunk[0] == ':') continue;
                
                // 验证格式为 "data: "
                if(chunk.compare(0, 6, "data: ") == 0)
                {
                    std::string modelData = chunk.substr(6);
                    if(modelData == "[DONE]") // 服务器宣告发送结束
                    {
                        callback("", true); // 触发回调通知 UI 层传输结束
                        IsComplete = true;
                        return true;
                    }
                    
                    // 开始对独立的增量 JSON 进行反序列化
                    Json::Value modelDataJson;
                    Json::CharReaderBuilder builder;
                    std::string errors;
                    std::istringstream modelDataStream(modelData);
                    if(Json::parseFromStream(builder,modelDataStream, &modelDataJson, &errors))
                    {
                        // 层层剥离，安全防崩溃检查
                        if(modelDataJson.isMember("choices") && modelDataJson["choices"].isArray()
                        && modelDataJson["choices"].size() > 0 && modelDataJson["choices"][0].isMember("delta")
                        && modelDataJson["choices"][0]["delta"].isMember("content"))
                        {
                            std::string content = modelDataJson["choices"][0]["delta"]["content"].asString();
                            fullResponse += content;  // 内部累加，作为最后的返回值
                            callback(content, false); // 核心回调机制：立马把最新的字串弹给上层渲染
                        }
                    }
                    else    WARN("modelDataJson parse failed:{} ", errors);
                }
            }
            return true;
        };

        // 6. 发射！挂起等待接收完成
        auto result = client.send(request);
        if(result == false)
        {
            DEBUG("send request failed, maybe is Network: {}", to_string(result.error()));
            return "";
        }
        if(!IsComplete)  // 安全兜底：如果异常断开且没收到 [DONE]
        {
            DEBUG("stream not finish");
            callback("", true);
        }
        return fullResponse;
    }
}
```
# 6. 结语
到这里，`DeepSeekProvider.cpp` 里的所有逻辑就被我们拆解完了。你会发现，所谓高级的流式回复，底层依然是我们亲手维护的缓冲区拼装与剪裁。理解了 JSON 结构的序列化以及底层的 SSE 事件协议，之后再遇到类似的高级网络交互也就能见招拆招了。
今日份收获，完美！下一篇咱们看看智谱 AI（GLM）是怎么折腾的。
