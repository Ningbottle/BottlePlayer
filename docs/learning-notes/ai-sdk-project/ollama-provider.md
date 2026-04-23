# 1. 前言：拥抱私有化，Ollama 的本地接入
在上一篇体验了“无缝切换”云端大模型的快感后，今天我们将把目光投向一个全新的领域——**本地私有化部署模型**。在企业级开发中，出于数据隐私的绝对要求，我们不可能总是把机密数据发给 DeepSeek 或是 GLM。这时候，[Ollama](https://ollama.com/) 作为本地大模型运行的霸主就登场了。
虽然我们的底层设计了强大的**多态基类（Polymorphic Base Class）**，但在具体接入 Ollama 时，你会发现它的协议格式与标准 OpenAI 体系（即 DeepSeek/GLM 遵循的体系）有着极其巨大的底层差异。这篇文章，我们将彻底撕开 Ollama 的协议包装，手把手带你搞定它的特殊接入逻辑。
![本地部署](file:///C:/Users/w1521/.gemini/antigravity/brain/8b0fcc1f-c673-43ec-b7cc-9311a41c4355/ollama_01_local_1782282732734.png)
# 2. 头文件设计：私有变量的灵活扩充
和前面几篇一样，我们要实现所有的纯虚函数。但请注意，本地模型可能随时会切换（比如从 `qwen` 切换到 `llama3`），所以我们将 `_modelName` 和 `_modelDesc` 提升为了类的私有成员变量，方便在初始化时动态注入，而不是像之前一样写死。
```cpp
// include/OllamalProvider.h
#pragma once
#include "LLMProvider.h"

namespace AI_Chat_SDK {
    class OllamalProvider : public LLMProvider {
    public:
        virtual ~OllamalProvider() = default;
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
        // 回到函数里面是第一个是增量，第二个是否还有增量
        virtual std::string SendMessageStream(std::vector<Message> messages,
                                    std::map<std::string, std::string> requestParam,
                                    std::function<void(std::string, bool)>) override;
        virtual std::string ModelDesc() const override;
    private:
        std::string _modelName;
        std::string _modelDesc;
    };
} // namespace AI_chat
```
# 3. 基础信息：摆脱鉴权束缚的初始化
我们先来看负责拉起 Ollama 驱动的几个基础函数。
## 3.1 核心函数介绍：Init
- **怎么来的**：在系统刚启动时，由 `LLMManager` 查阅用户的配置文件，提取属于 Ollama 的参数块后触发调用。
- **底层的设计机理**：云端 API 需要配置 `api_key` 作为通行证，但在本地主机（Localhost）上运行的模型一般没有鉴权这一层（因为只有你自己能访问本机的 11434 端口）。因此，这里的机理发生了变化，我们不再索要 `api_key`，而是强制要求用户在配置文件里提供具体的 `model_name`（比如 "llama3"）和本地服务的 `endpoint`（通常是 `http://localhost:11434`）。这体现了**组件高内聚**的思想，由自己决定需要什么资源。
```cpp
#include "../include/OllamalProvider.h"
#include <jsoncpp/json/config.h>
#include <jsoncpp/json/forwards.h>
#include <jsoncpp/json/json.h>
#include <jsoncpp/json/reader.h>
#include <jsoncpp/json/value.h>
#include <jsoncpp/json/writer.h>
#include "../include/common.h"
#include <httplib.h>
#include <map>
#include <sstream>

namespace AI_Chat_SDK
{
    bool OllamalProvider::Init(const std::map<std::string, std::string>& config)
    {
        auto it = config.find("model_name");
        if (it == config.end())
        {
            ERROR("model_name not found in config");
            return false;
        }
        _modelName = it->second;
        it = config.find("endpoint");
        if (it == config.end())
        {
            ERROR("endpoint not found in config");
            return false;
        }
        _Endpoint = it->second;
        it = config.find("model_desc");
        if (it == config.end())
        {
            ERROR("model_desc not found in config");
            return false;
        }
        _modelDesc = it->second;
        _IsAvailable = true;
        return true;
    }

    bool OllamalProvider::IsAvailable() const { return _IsAvailable; }
    std::string OllamalProvider::GetModelName() const { return _modelName; }
    std::string OllamalProvider::ModelDesc() const { return _modelDesc; }
```
# 4. 非流式实现：SendMessage 深度拆解
## 4.1 函数介绍与机理变更
`SendMessage` 负责一次性发包和收包。参数依然是聊天记录数组 `messages` 和模型偏好参数 `requestParam`。
这里最致命的机理变化是：Ollama **拒绝接受扁平化的参数配置**。在 DeepSeek 中，`temperature` 是直接挂在 JSON 根目录的；而在 Ollama 的协议中，所有的生成调整参数必须被严密打包进一个名为 `options` 的 JSON 子对象中，且上下文长度的字段名从标准的 `max_tokens` 变成了 `num_ctx`。
![参数打包](file:///C:/Users/w1521/.gemini/antigravity/brain/8b0fcc1f-c673-43ec-b7cc-9311a41c4355/ollama_02_options_1782282743175.png)
## 4.2 一步一步手撕逻辑
- **第一步：提取玄学参数**：使用标准库 `std::stod` 和 `std::stoi`，将外部传入的字符串参数转为浮点和整数。如果外部没传，我们提供默认的保守数值。
- **第二步：组装 Ollama 专属请求树**：
  我们通过 `Json::Value(Json::arrayValue)` 创建聊天数组 `messages`。这部分和标准接口一致。
  随后，我们创建了一个新对象 `Json::Value option(Json::objectValue)`。将 `temperature` 和 `num_ctx` 塞进这个名为 `option` 的小盒子里。最后把小盒子整体放进大包裹：`requestBody["options"] = option;`。
- **第三步：序列化与发车**：将构建好的庞大树状图用 `Json::StreamWriterBuilder` **序列化**成一串紧密的字符串。利用 `httplib::Client` 发送 POST 请求。因为没有鉴权，所以 Headers 里只需要干净利落的 `{"Content-Type", "application/json"}` 即可。
- **第四步：防崩溃的底层反序列化**：收到 `response` 后，将 `response->body` 放进 `stringstream`，利用流式解析重组为 JSON 对象。注意，Ollama 返回的文本层级不是嵌套三层的 `choices[0].message.content`，而是直接暴力的 `message.content`。所以在提取前，必须严苛校验 `jsonResponse.isMember("message")`，严防**访问越界导致的程序崩溃**。
```cpp
    std::string OllamalProvider::SendMessage(std::vector<Message> messages,
        std::map<std::string, std::string> requestParam)
    {
        if (!_IsAvailable)
        {
            ERROR("OllamalProvider is not available");
            return "";
        }
        // 第一步：开始配置参数，提取为数值型
        float temperature = 0.7f;
        int max_tokens = 1024;
        auto it = requestParam.find("temperature");
        if (it != requestParam.end())
            temperature = std::stod(it->second);
        it = requestParam.find("max_tokens");
        if (it != requestParam.end())
            max_tokens = std::stoi(it->second);
            
        // 第二步：构建请求体，应对特殊的 Options 打包结构
        Json::Value requestBody;
        requestBody["model"] = _modelName;
        requestBody["messages"] = Json::Value(Json::arrayValue);
        for (const auto& msg : messages)
        {
            Json::Value message;
            message["role"] = msg._role;
            message["content"] = msg._content;
            requestBody["messages"].append(message);
        }
        Json::Value option(Json::objectValue);  // 创建一个空的 options 对象,类型为object
        option["temperature"] = temperature;    // 设置温度参数
        option["num_ctx"] = max_tokens;         // 专属的上下文长度参数名
        requestBody["options"] = option;        // 将 options 对象添加到请求体中
        requestBody["stream"] = false;          // 强制关闭流式传输
        
        // 第三步：序列化为网络字符串
        Json::StreamWriterBuilder builder;
        std::string requestBodyStr = Json::writeString(builder, requestBody);
        
        // 构建客户端：发送阻塞式 POST
        httplib::Client client(_Endpoint.c_str());
        client.set_read_timeout(30,0);
        httplib::Headers headers = {
            {"Content-Type", "application/json"} // 彻底干掉了 Authorization
        };
        auto response = client.Post("/api/chat", headers, requestBodyStr, "application/json");
        if(!response)
        {
            ERROR("OllamaLLMProvider::sendMessage: failed to send request, error: {}", to_string(response.error()));
            return "";
        }
        if(response->status != 200)
        {
            ERROR("OllamaLLMProvider::sendMessage: failed to send request, status: {}", response->status);
            return "";
        }
        if(response->body.empty())
        {
            ERROR("OllamaLLMProvider::sendMessage: empty response body");
            return "";
        }
        
        // 第四步：严苛反序列化与数据抽取
        Json::Value jsonResponse;
        Json::CharReaderBuilder readerBuilder;
        std::string error;
        std::stringstream ss(response->body);
        if(!Json::parseFromStream(readerBuilder, ss, &jsonResponse, &error))
        {
            ERROR("OllamaLLMProvider::sendMessage: failed to parse response body, error: {}", error);
            return "";
        }
        
        // 铁桶防崩溃：层层递进的成员安全检查
        if(jsonResponse.isMember("message") && jsonResponse["message"].isObject()
            && jsonResponse["message"].isMember("content"))
        {
            return jsonResponse["message"]["content"].asString();
        }
        ERROR("OllamaLLMProvider::sendMessage: invalid response format");
        return "";
    }
```
# 5. 攻克硬核难点：SendMessageStream 流式机理详解
如果你觉得非流式的字段改动还能应付，那 Ollama 的流式传输机理绝对会让你跌破眼镜。它完全**颠覆了标准的 SSE（Server-Sent Events）协议**。
## 5.1 机理大反转：不再是双换行与前缀！
在标准的云端大模型接口中，流式回传的数据块是以 `data: ` 开头，并且用连着的两个换行符 `\n\n` 作为分包界的。
但是，**Ollama 直接抛弃了这一套！** 它的底层机理非常直白：不加任何前缀，每一个数据块就是一个完整的独立 JSON 字符串，包和包之间只用**一个换行符 `\n`** 隔开。这意味着我们之前为 DeepSeek 写的拆包机在这里会彻底失效。
![单换行拆包](file:///C:/Users/w1521/.gemini/antigravity/brain/8b0fcc1f-c673-43ec-b7cc-9311a41c4355/ollama_03_newline_1782282754475.png)
## 5.2 逐步手撕全新流式拆包器
为了适配 Ollama 狂野的风格，我们必须在回调函数 `content_receiver` 里手写一个新的拆包机：
- **第一步：挂载网络请求参数**：和之前一样打包 `options`，开启 `requestBody["stream"] = true`。因为这是一个长期运行的链接，必须利用 `client.set_read_timeout(200,0)` 放宽读取超时，防止 AI 想太久导致强制断开。
- **第二步：单换行符（`\n`）切割术**：
  把网卡层发来的碎片 `data` 一股脑倒进 `buff` 池子里。接着，利用一个核心的 `while((pos = buff.find("\n")) != std::string::npos)` 死磕单换行符。找到后，精准地用 `substr(0, pos)` 切出这完整的一行 JSON，再立刻 `erase(0, pos + 1)` 把它从池子里销毁掉。
- **第三步：极简内存反序列化**：
  切出来的 `line` 就是一个最纯粹的 JSON 串了。直接装入 `std::stringstream` 交由底层解析器 `Json::parseFromStream` 把它立体化。
- **第四步：布尔值的终章信号**：
  在标准的 SSE 协议中，遇到 `[DONE]` 字符串说明数据流结束。但 Ollama 的机理是：它会在每个 JSON 对象中附带一个 `done` 字段。只要 `root.get("done", false).asBool()` 为真，就说明这已经是最后一块拼图了。我们立马触发上层 `callback("", true)`，并且 `return false` 主动切断底层的 HTTP 连接。
![原生结束符](file:///C:/Users/w1521/.gemini/antigravity/brain/8b0fcc1f-c673-43ec-b7cc-9311a41c4355/ollama_04_done_1782282766104.png)
```cpp
    std::string OllamalProvider::SendMessageStream(std::vector<Message> messages,
                                std::map<std::string, std::string> requestParam,
                                std::function<void(std::string, bool)> callback)
    {
        if(!IsAvailable()) return "";
        // 第一步：参数提取与 JSON 重构
        float temperature = 0.7f;
        int max_tokens = 1024;
        auto it = requestParam.find("temperature");
        if (it != requestParam.end())
            temperature = std::stod(it->second);
        it = requestParam.find("max_tokens");
        if (it != requestParam.end())
            max_tokens = std::stoi(it->second);
            
        // 消息记录和请求体
        Json::Value requestBody;
        requestBody["model"] = _modelName;
        requestBody["messages"] = Json::Value(Json::arrayValue);
        for (const auto& msg : messages)
        {
            Json::Value message;
            message["role"] = msg._role;
            message["content"] = msg._content;
            requestBody["messages"].append(message);
        }
        Json::Value option(Json::objectValue);  
        option["temperature"] = temperature;    
        option["num_ctx"] = max_tokens;         
        requestBody["options"] = option;        
        requestBody["stream"] = true;          // 开启流式阀门
        
        Json::StreamWriterBuilder builder;
        std::string requestBodyStr = Json::writeString(builder, requestBody);
        
        httplib::Headers headers =
        {
            {"Content-Type", "application/json"},
            {"Accept", "text/event-stream"}
        };

        // 各种底层状态变量
        bool IsComplete = false;
        std::string buff;
        std::string fullResponse;
        bool gotErr = false;
        std::string ErrMsg;
        
        // 实例化原生 Request 对象
        httplib::Request request;
        request.method = "POST";
        request.path = "/api/chat";
        request.headers = headers;
        request.body = requestBodyStr;
        
        // 设置 HTTP 握手状态拦截器
        request.response_handler = [&](const httplib::Response& res) {
            if (res.status != 200) {
                gotErr = true;
                ErrMsg = res.body;
                ERROR("OllamaLLMProvider::sendMessage: status {}, body:{} ", res.status, ErrMsg);
                return false;
            }
            return true;
        };

        // 第二步：颠覆性的 content_receiver 拆包机
        request.content_receiver = [&](const char *data, size_t len, uint64_t offset,
                           uint64_t total_length)
        {
            if(gotErr) return false;
            buff.append(data,len); // 内存块压入暂存池
            size_t pos = 0;
            
            // 死磕单换行符 \n，这是 Ollama 独有的分包边界
            while((pos = buff.find("\n")) != std::string::npos)
            {
                std::string line = buff.substr(0, pos); // 精确切割
                buff.erase(0, pos + 1); // 抹除已读碎片
                if(line.empty()) continue;
                
                // 第三步：直接将整行文本当做完整 JSON 进行反序列化
                Json::Value root;
                std::stringstream ss(line);
                std::string err;
                Json::CharReaderBuilder builder;
                if(!Json::parseFromStream(builder, ss, &root, &err))
                {
                    ERROR("Failed to parse JSON: {}", err);
                    continue;
                }
                
                // 安全提取文本内容并推送给界面回调机制
                if (root.isMember("message") && root["message"].isObject()
                           && root["message"].isMember("content"))
                {
                    std::string content = root["message"]["content"].asString();
                    if (!content.empty())
                    {
                        fullResponse += content;
                        callback(content, false);
                    }
                }
                
                // 第四步：检查终章信号
                // 如果 JSON 树中含有 done 字段并且值为 true，宣告结束
                if (root.get("done", false).asBool())
                {
                    IsComplete = true;
                    callback("", true); // 通知上层渲染最后一块
                    return false; // 返回 false 意为主动切断 HTTP 底层长连接
                }
            }
            return true;
        };
        
        httplib::Client client(_Endpoint.c_str());
        client.set_connection_timeout(30,0);
        client.set_read_timeout(200,0); // 流式传输时间必须放宽
        
        auto res = client.send(request);
        if(!res)
        {
            ERROR("Failed to send request: {}", httplib::to_string(res.error()));
            return "";
        }
        
        // 异常网络波动导致流中断的兜底保险
        if(!IsComplete)
        {
            ERROR("OllamaLLMProvider::sendMessageStream: stream not finish, fullResponse: {}", fullResponse);
            callback("", true);
        }
        return fullResponse;
    }

} // namespace AI_SDK
```
# 6. 结语
写完 OllamaProvider，我们的多态架构版图又完成了一块最重要的拼图——本地化。
你看，尽管 Ollama 的参数必须打包在 `options` 盒子里，尽管它的流式拆解用的是单换行符 `\n`，尽管它的结束信标是一个不起眼的 `done: true` 的布尔值……但是，得益于 `LLMProvider` 基类的封装，我们在这一层把所有脏活累活干完后，上层调用者根本无需关心这些鸡毛蒜皮。
下层兵荒马乱，上层岁月静好，这就是架构设计的核心艺术。下一篇，我们去看看更上层负责调度这一切的幕后大老板：`LLMManager`！
