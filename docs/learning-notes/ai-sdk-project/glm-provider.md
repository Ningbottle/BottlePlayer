# 1. 前言：智谱 GLM-4 的无缝接入
在上一篇搞定了 DeepSeek 的接入后，我们的架构已经经受住了第一次考验。今天我们要接入的是国产大模型中的佼佼者——智谱 GLM（glm-4.7-flash）。因为我们之前设计了极为强大的**多态基类（Polymorphic Base Class）**，所以接入 GLM 就像是拔下一个插头，插到另一个插座上一样简单。但这篇绝不是简单的重复劳动，我们将借着 GLM 的源码，更极致地一步步拆解**函数是怎么来的、怎么被使用的、底层的核心机理是什么**。
![无缝切换](file:///C:/Users/w1521/.gemini/antigravity/brain/8b0fcc1f-c673-43ec-b7cc-9311a41c4355/glm_01_switch_1782269133981.png)
# 2. 头文件设计：一模一样的配方
和之前完全一样，`GLMProvider` 也是继承自 `LLMProvider`。这体现了**外观模式（Facade Pattern）**的魅力，外界调用者根本不需要关心里面到底是 DeepSeek 还是 GLM，只要调相同的函数名就行了。
```cpp
// include/GLMProvider.h
#pragma once
#include "LLMProvider.h"
namespace AI_Chat_SDK {
    class GLMProvider : public LLMProvider {
    public:
        virtual ~GLMProvider() = default;
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
        // 6. 获取模型描述
        virtual std::string ModelDesc() const override;
    };
} // namespace AI_chat
```
# 3. 基础信息：网关替换
我们先来看前四个最基础的函数：`Init`、`IsAvailable`、`GetModelName` 和 `ModelDesc`。
## 3.1 函数介绍与机理
- **怎么来的**：当系统启动，模型管家（`LLMManager`）想要注册 GLM 时，就会主动调用 `Init` 函数，并传入一个装满配置信息的字典 `config`。
- **机理与工具**：大模型的接口是典型的**无状态协议（Stateless Protocol）**，它不认识你是谁，只认你携带的通行证（API Key）和请求地址（Base URL）。我们使用了 C++ 标准库中的 `std::map::find()` 来查找这些参数。如果用户在配置表里没传地址，我们的程序会直接给它兜底赋值为智谱官方的 `https://open.bigmodel.cn`。
![替换端点](file:///C:/Users/w1521/.gemini/antigravity/brain/8b0fcc1f-c673-43ec-b7cc-9311a41c4355/glm_02_endpoint_1782269146740.png)
```cpp
#include "../include/GLMProvider.h"
#include <jsoncpp/json/config.h>
#include <jsoncpp/json/forwards.h>
#include <jsoncpp/json/json.h>
#include <jsoncpp/json/reader.h>
#include <jsoncpp/json/value.h>
#include <jsoncpp/json/writer.h>
#include "../include/common.h"
#include <httplib.h>
#include <sstream>

namespace AI_Chat_SDK {
    bool GLMProvider::Init(const std::map<std::string, std::string>& config)
    {
        auto it = config.find("api_key");
        if(it == config.end())  return false;
        _ApiKey = it->second;
        it = config.find("base_url");
        if(it == config.end())  _Endpoint = "https://open.bigmodel.cn";
        else _Endpoint = it->second;
        _IsAvailable = true;
        INFO("GLMProvider initModel success, endpoint: {}", _Endpoint);
        return true;
    }
    bool GLMProvider::IsAvailable() const { return _IsAvailable == true; }
    std::string GLMProvider::GetModelName() const { return "glm-4.7-flash"; } // 需要什么后面自己改
    std::string GLMProvider::ModelDesc() const
    {
        return "GLM-4.7-Flash 作为 30B 级 SOTA 模型，提供了一个兼顾性能与效率的新选择。";
    }
```
# 4. 非流式实现：SendMessage 深度剖析
## 4.1 函数介绍与所用工具
- **参数一 `messages`**：这是一个装满了 `Message` 结构体的动态数组，代表了你和 AI 过去所有的聊天记录。
- **参数二 `requestParam`**：一个字符串字典，装载了前端用户设置的属性（比如输出的随机性、长度限制）。
- **返回值**：大模型深思熟虑后返回的完整文字结果。
- **使用了哪些核心工具？**
  1. `Json::Value` 和 `Json::arrayValue`：由 JsonCpp 库提供，用于在内存里像搭积木一样拼装 JSON 树状图。
  2. `Json::StreamWriterBuilder`：用于**序列化**。
  3. `httplib::Client`：一个极其好用的轻量级 C++ HTTP 网络通信库。
  4. `std::istringstream` 和 `Json::parseFromStream`：用于将网络收到的字符串转换回 JSON 对象（**反序列化**）。
## 4.2 逻辑机理：一步一步手撕非流式
非流式通信就像发电子邮件，客户端把所有的信件打包发出去，然后**阻塞等待（Blocking Wait）**，直到服务器把完整的长篇大论写完，一次性扔回来。具体步骤如下：
**第一步：提取玄学参数**
我们用 `requestParam.find` 去字典里找 `temperature` 和 `max_tokens`。因为字典里存的都是文本，所以必须用标准库的 `std::stod`（字符串转双精度浮点）和 `std::stoi`（字符串转整数）将其转换为数值。
**第二步：组装 JSON 数组**
计算机底层必须遵循大模型厂商规定的格式。我们定义 `requestBody["messages"] = Json::arrayValue` 声明这是一个 JSON 数组。然后用 `for` 循环遍历所有的聊天记录，把发件人（`role`）和内容（`content`）做成一张张卡片，`append` 串到大铁环上。
![JSON数组](file:///C:/Users/w1521/.gemini/antigravity/brain/8b0fcc1f-c673-43ec-b7cc-9311a41c4355/glm_03_json_array_1782269156590.png)
**第三步：序列化与发请求**
拼好的 JSON 只是 C++ 内存里的一个树状图，网卡根本不认识。我们必须用 `Json::writeString` 将它**序列化（Serialization）**成扁平的字符串。接着，我们实例化 `httplib::Client`，装载 `Headers`（里面带上我们的 `api_key` 作为通行证），调用 `Post` 发起请求。注意，这里设置超时时间 `set_read_timeout` 是为了防止服务器宕机导致我们的程序无限期卡死。
**第四步：反序列化与结果提取**
当 `client.Post` 执行完毕，如果 `status == 200`，说明服务器把信寄回来了。信件内容藏在 `result->body` 里，它是一长串字符串。我们通过 `std::istringstream` 将其变成数据流，喂给 `Json::parseFromStream` 进行**反序列化（Deserialization）**。最后，根据标准的 `choices[0].message.content` 路径，精准提取出里面的对话文本。
```cpp
    std::string GLMProvider::SendMessage(std::vector<Message> messages,
                            std::map<std::string, std::string> requestParam)
    {
        if(!IsAvailable())  return ""; // 如果没有初始化成功就直接返回
        
        // 1. 提取玄学参数，从字符串强制转为数值
        double temperature = 0.7;
        int maxOutputTokens = 4096;
        if(requestParam.find("temperature") != requestParam.end())
            temperature = std::stod(requestParam["temperature"]);
        if(requestParam.find("max_tokens") != requestParam.end())
            maxOutputTokens = std::stoi(requestParam["max_tokens"]);
            
        // 2. 构建 JSON 树与消息列表
        Json::Value requestBody;
        requestBody["model"] = GetModelName();
        requestBody["messages"] = Json::arrayValue; // 声明为 json 数组
        for(const auto& msg : messages) {
            Json::Value message;
            message["role"] = msg._role;
            message["content"] = msg._content;
            requestBody["messages"].append(message); // 循环挂载卡片
        }
        requestBody["temperature"] = temperature;
        requestBody["max_tokens"] = maxOutputTokens;
        
        // 3. 利用 httplib 构建网络管家
        httplib::Client client(_Endpoint.c_str());
        client.set_connection_timeout(30,0);
        client.set_read_timeout(30,0); // 防止死等的安全阀
        httplib::Headers headers = {
            {"Content-Type", "application/json"},
            {"Authorization", "Bearer " + _ApiKey}
        };
        
        // 4. 对内存树进行扁平化序列化
        Json::StreamWriterBuilder builder;
        builder.settings_["indentation"] = ""; // 紧凑输出，节约带宽
        std::string body = Json::writeString(builder, requestBody);
        
        // 5. 阻塞发送 POST 请求
        auto result = client.Post("/api/paas/v4/chat/completions", headers, body, "application/json");
        if(!result)
        {
            ERROR("GLMProvider sendMessage POST request failed, error: {}",httplib::to_string(result.error()));
            return "";
        }
        if(result->status != 200)
        {
            ERROR("GLMProvider sendMessage POST request failed, status: {}",result->status);
            return "";
        }
        
        // 6. 将收到的字符串反序列化回 JSON 树
        Json::CharReaderBuilder readerbuilder;
        Json::Value requestJson;
        std::string errs;
        std::istringstream ss(result->body);
        if (!Json::parseFromStream(readerbuilder, ss, &requestJson, &errs)) {
            ERROR("parse response body failed: {}", errs);
            return "";
        }
        
        // 7. 循着路径把文字扒出来
        std::string responseContent = requestJson["choices"][0]["message"]["content"].asString();
        return responseContent;
    }
```
# 5. 攻克硬核难点：SendMessageStream 流式机理详解
## 5.1 什么是流式传输？机理在哪？
现在大家都习惯了大模型的“打字机”效果——字是一个一个蹦出来的。这背后依托的是 **SSE（Server-Sent Events）** 协议。
机理在于：客户端和服务器建立连接后，服务器不关闭连接，而是源源不断地向客户端推送极小的数据块（Chunks）。而在底层协议中，服务器用 **两个连续的换行符 `\n\n`** 来标志一小句话传完了。传来的数据包里还带有一个前缀 `data: `。
## 5.2 逐步手撕流式代码逻辑
写这个函数，不仅需要写网络层，还需要写内存解析层。我们拆分为四大步骤：
**第一步：配置参数的变动**
我们要在 JSON 里多加一个非常核心的字段：`requestBody["stream"] = true`。这其实就是告诉智谱的服务器：“别给我一次性发，请像水管一样一点点流过来”。同时，HTTP 协议头必须增加 `{"Accept", "text/event-stream"}`。
**第二步：接管底层的错误与数据截获（钩子函数）**
以前我们用 `client.Post`，就像把信丢进邮筒就不用管了。但现在不行，水流是一直在流的。所以我们要实例化一个底层的 `httplib::Request` 对象，并在它身上挂载两个**回调拦截器（Callback）**：
- `response_handler`：这是在刚连上服务器，拿到 HTTP 状态码时触发。如果不是 200，立刻拦住并报错。
- `content_receiver`：这是全篇最硬核的地方。网络层每收到几个字节的包裹，就会无条件触发这个函数，把指针 `data` 丢给你。
**第三步：手写数据拆包机（在 content_receiver 中）**
我们定义了一个字符串 `buff` 做中转站。
1. `buff.append(data, len)`：把刚收到的杂乱字节全扔进站里。
2. `while(buff.find("\n\n") != std::string::npos)`：死磕这个双换行符。只要能找到 `\n\n`，说明我们完整截获了一个事件。
3. 如果没找到呢？退出 while 循环，等待下一次数据到来继续拼接。
4. 找到之后，用 `substr` 把这一块切下来存进 `chunk`，然后立刻用 `erase` 从 `buff` 里把切走的内容删掉。
5. 去掉前缀 `data: `。如果遇到 `[DONE]` 标志，直接触发上层函数（UI刷新），宣告全文结束。
**第四步：极致严谨的底层解析（CharReader）**
如果你切下来的 `chunk` 不是结束标志，那就是实打实的 JSON 片段了。为什么这里不用 `parseFromStream`？因为这时候数据在内存 `modelData` 字符串里。对纯内存的块进行反序列化，我们通过 `Json::CharReaderBuilder` 获取一个底层的指针解析器。你提供起始内存地址（`data()`）和结束地址（`data() + size()`），它直接在内存里“动刀”，不仅效率奇高，而且能极其精准地拦截格式错乱。
![CharReader解析](file:///C:/Users/w1521/.gemini/antigravity/brain/8b0fcc1f-c673-43ec-b7cc-9311a41c4355/glm_04_charreader_1782269166730.png)
**难点防御：** 如果网络波动导致这块 JSON 是残缺的（比如 `choices` 数组为空），新手代码写 `choices[0]` 就会立刻触发**数组越界段错误崩溃（Segmentation Fault）**。所以我们写下了 `!root.isMember("choices") || root["choices"].empty()` 的铁桶防御机制。
```cpp
    std::string GLMProvider::SendMessageStream(std::vector<Message> messages,
                                    std::map<std::string, std::string> requestParam,
                                    std::function<void(std::string, bool)>callback)
    {
        if(!IsAvailable()) { return ""; }
        // 1. 设置常规参数
        double temperature = 0.6;
        int maxTokens = 4096;
        auto it = requestParam.find("temperature");
        if(it != requestParam.end()) { temperature = std::stod(it->second); }
        it = requestParam.find("max_tokens");
        if(it != requestParam.end()) { maxTokens = std::stoi(it->second); }
        
        // 2. 构建包含 stream = true 的 JSON 树
        Json::Value requestBody;
        requestBody["model"] = GetModelName();
        Json::Value messagesArray;
        for(auto& msg : messages)
        {
            Json::Value message;
            message["role"] = msg._role;
            message["content"] = msg._content;
            messagesArray.append(message);
        }
        requestBody["messages"] = messagesArray;
        requestBody["temperature"] = temperature;
        requestBody["max_tokens"] = maxTokens;
        requestBody["stream"] = true; // 流式开关
        
        // 3. 序列化为字符串
        Json::StreamWriterBuilder writerBuilder;
        std::string requestBodyStr = Json::writeString(writerBuilder, requestBody);
        INFO("GLMProvider sendMessageStream requestBody: {}", requestBodyStr);
        
        // 4. 构建底层客户端，放宽读取超时限制
        httplib::Client client(_Endpoint);
        client.set_connection_timeout(30,0);
        client.set_read_timeout(300,0);      // 流式响应必须留出极长的时间
        
        // 5. 手动定制 Request 对象并装配协议头
        httplib::Request request;
        request.method = "POST";
        request.path = "/api/paas/v4/chat/completions";
        request.body = requestBodyStr;
        request.set_header("Authorization", "Bearer " + _ApiKey);
        request.set_header("Content-Type", "application/json");
        request.set_header("Accept","text/event-stream"); // SSE 核心标识
        
        // 6. 声明流式数据中转站（缓冲区）
        std::string buff;           
        bool gotError = false;      
        std::string MsgError;       
        int statusCode = 0;         
        bool IsComplete = false;    
        std::string fullResponse;   
        
        // 7. 设置 HTTP 状态首包拦截器
        request.response_handler = [&](const httplib::Response& res) {
            statusCode = res.status;
            if(statusCode != 200)
            {
                gotError = true;
                MsgError = res.body;
                ERROR("Request failed with status code: {}, body: {}", res.status, MsgError);
                return false;
            }
            return true;
        };
        
        // 8. 设置核心拆包机：content_receiver
        request.content_receiver = [&](const char* data, size_t len,uint64_t offset, uint64_t totalLength) {
            if(gotError == true) return false;
            buff.append(data, len); // 包裹无脑扔进缓冲池
            INFO("GLM Send Msg {}", buff);
            
            // 循环扫描双换行符（SSE 协议事件边界）
            while(buff.find("\n\n") != std::string::npos)
            {
                std::string chunk = buff.substr(0, buff.find("\n\n")); // 切下这块
                buff.erase(0, buff.find("\n\n") + 2); // 从池子里抹掉
                
                // 过滤心跳包（冒号开头或空行）
                if(chunk.empty() || chunk[0] == ':') continue;
                
                // 找准有价值的载荷
                if(chunk.compare(0, 6, "data: ") == 0)
                {
                    std::string modelData = chunk.substr(6); // 剔除前缀
                    if(modelData == "[DONE]")
                    {
                        callback("", true); // 触发上层结束响应
                        IsComplete = true;
                        return true;
                    }
                    
                    // 开始进行反序列化: 使用底层的 CharReader
                    Json::CharReaderBuilder readerBuilder;  
                    Json::Value root;                       
                    std::string errs;                       
                    std::unique_ptr<Json::CharReader> reader(readerBuilder.newCharReader());
                    // 圈定内存地址直接解析，更高效
                    if (!reader->parse(modelData.data(), modelData.data() + modelData.size(), &root, &errs))
                    {
                        ERROR("modelDataJson parse failed:{} ", errs);
                        return false;
                    }
                    else
                    {
                        // 铁桶级防崩溃逻辑：只有数组真的存在且不为空，才敢去取第 0 个元素
                        if(!root.isMember("choices") || root["choices"].empty()) continue;
                        Json::Value delta = root["choices"][0]["delta"];
                        
                        // 后续我打算提取思维链，给显现,这个先不用考虑
                        if (delta.isMember("content") && !delta["content"].isNull())
                        {
                            std::string content = delta["content"].asString();
                            if(!content.empty())
                            {
                                fullResponse += content;
                                callback(content, false); // 将增量字串火速推给前端界面
                            }
                        }
                    }
                }
            }
            return true;
        };
        
        // 9. 发起调用并挂起，直到服务器断开连接
        auto result = client.send(request);
        if(!result)
        {
            ERROR("send request failed:{}", to_string(result.error()));
            return "";
        }
        if(!IsComplete)  // 安全兜底
        {
            DEBUG("stream not finish");
            callback("", true);
        }
        return fullResponse;
    }
} // namespace AI_chat
```
# 6. 结语
看完这篇，你应该能深刻体会到**面向对象设计（多态）**带来的爽感了——换模型只是换一个地址参数和部分解析细节而已，大体结构雷打不动。
更关键的是，在这篇文章里，我们不再是走马观花地贴代码，而是真正手撕了网络底层。从 `httplib` 的拦截器回调，到 SSE 协议里那至关重要的 `\n\n` 拆包机制，再到内存级别的 `CharReader` 和越界防崩溃检查。只要你能顺畅地跟着把这部分手撸出来，国内外的所有主流大模型，对你来说都已经是随便捏的橡皮泥了！
下一篇，我们将挑战另一种全新的玩法：如何接入脱离云端的**本地私有部署模型**（OllamaProvider）。
