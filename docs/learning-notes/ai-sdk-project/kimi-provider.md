# 1. 前言：
先来介绍一下什么是kimi吧？这个回答是有kimi自己回答的：
> 我是 Kimi，由月之暗面（Moonshot AI）开发的人工智能助手。月之暗面是一家专注于通用人工智能研发的中国科技公司，致力于通过先进的模型技术为用户提供高效、智能的服务。
>与其他 AI 相比，Kimi 的核心优势在于**超长上下文处理能力**——我能够一次性理解和分析数十万字的文本、长文档或多轮复杂对话，在长文本总结、深度分析和连贯推理方面表现尤为突出。同时，我支持联网搜索、文件解析、图片理解等多种功能，力求在准确性和实用性上达到平衡。
>月之暗面的技术理念强调**有用性（Helpfulness）和可靠性（Harmlessness）**，在提供强大能力的同时注重安全对齐，确保输出内容真实、可控。如果你需要处理长篇资料、进行深度研究或复杂任务协作，Kimi 的长文本优势会非常明显。

在我看来，kimi是国内的前端审美能力很强，也是三家里面第一个做出多模态的大模型，在kimi k2.6的时候我经常用，真的非常不错。

在实现这个的时候，我们可以使用轨迹流动的api(原因是kimi官方的控制台，最低需要充值10元，学生实在是没有钱)。
我们可以看到了，轨迹流动提供了很多国内的模型:
![PixPin_2026-05-15_09-16-19.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/PixPin_2026-05-15_09-16-19.png)

## 1.2 在官网生成ApiKey
[硅基流动统一登录](https://cloud.siliconflow.cn/me/models)
![PixPin_2026-05-15_09-17-59.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/PixPin_2026-05-15_09-17-59.png)
地址我放在图片旁边了，Apikey放在了轨迹流动旁边:
![PixPin_2026-05-15_09-20-59.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/PixPin_2026-05-15_09-20-59.png)
新建自己的api密钥，这里我们之前讲过了。接下来我们实现部分好写的函数:


# 2. 先实现比较简单的函数：
## 2.1 文具的准备：
![PixPin_2026-05-15_09-22-48.png|324](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/PixPin_2026-05-15_09-22-48.png)
先说项目结构：include 里面放的是我们的头文件，而src里面是cpp源文件。这样的分层，利于我们进行管理。在来说说我用的编译器使用的是zed，这个比较轻，使用RUST语言来重构的。而且有补全功能，如果你想要体会更强的ai coding功能，你甚至是可以使用claude code 或者codex或者cursor。他们的ai功能甚至更强。但是这里我们主要学习的是如何学习。所以用了一个比较好用的zed。也可以使用vscode，但是我的云服务器老是宕机。


## 2.2 Init 初始化的函数：
这个是用来干什么的？
>我们从用户的config里面读取设置api_key和base_url.我们利用相关配置来对模型进行初始化

我们来解释一下什么是base_url.**base_url** 是 API 请求的基础地址，它是所有 API 端点的前缀。后面还有其他的后缀，比如：base_url + 版本号 + 端点 = 完整的 API 请求地址。

那么这个就简单了，我们可以看到，config是`std::map<std::string,std::string>`类型的。我们可以利用find来找到`api_key`和`base_url`.如果有一个没有找到都是`false`，同时最后找到了，记得要把`_IsAvailable`设为真。

那么函数的设计如下：
```cpp
     bool KimiProvider::Init(const std::map<std::string, std::string>& config)
    {
        auto it = config.find("api_key");
        if (it == config.end()) { return false; }
        _ApiKey = it->second;
        it = config.find("base_url");
        if (it == config.end()) { return false; }
        _Endpoint = it->second;
        _IsAvailable = true;
        INFO("KimiProvider initialized successfully, endpoint {}", _Endpoint);
        return true;
    }
```

## 2.3 返回模型名称和描述的函数:
这两个还是比较简单，一个返回名称，一个是返回对于kimi的描述。
```cpp
    bool KimiProvider::IsAvailable() const {return _IsAvailable == true;}
    
    std::string KimiProvider::GetModelName() const {return "kimi k2.6";}
    
    std::string KimiProvider::ModelDesc() const
    {
        return "月之暗面开发的 AI 助手 Kimi，擅长长文本处理与多模态任务，致力于为你提供高效的服务";
    }
```

## 2.4.非流式发送消息:
### 2.4.1 对api进测试:
下载apifox，打开测试环境，添加前置base_url,由于之前测试过GLM官方得，这里我们来测试这个：
![PixPin_2026-05-15_10-36-13.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/PixPin_2026-05-15_10-36-13.png)

之后，我们打开官方得文档：[创建对话请求（OpenAI） - SiliconFlow](https://docs.siliconflow.cn/cn/api-reference/chat-completions/chat-completions)，这里详细得讲了应该怎么创建自己得结构体。
我们点击新建一个https得请求：我们一开始不测试流式输出：
![PixPin_2026-05-15_14-03-23.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/PixPin_2026-05-15_14-03-23.png)
![PixPin_2026-05-15_14-04-50.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/PixPin_2026-05-15_14-04-50.png)
![PixPin_2026-05-15_14-05-17.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/PixPin_2026-05-15_14-05-17.png)
按照上面的来进行设计，我们发送第一个请求：
![PixPin_2026-05-15_14-08-26.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/PixPin_2026-05-15_14-08-26.png)
我们可以看到kimi放回的格式，接下来我们开始写这个函数：

### 2.4.2 进行编写函数：
#### 1. 观察是否初始化：
如果没有初始化，直接放回空：
```cpp
  if(!IsAvailable()) { return ""; }
```

#### 2. 开始从Param中提取关键词：
现给出我们系统设置的默认温度值，在从config里面寻找，如果没有找到，就是用户默认使用使用默认值:
```cpp
        double temperature = 0.7;
        int max_tokens = 4096;
        auto it = requestParam.find("temperature");
        if (it != requestParam.end()) {
            temperature = std::stod(it->second);
        }
        it = requestParam.find("max_tokens");
        if (it != requestParam.end()) {
            max_tokens = std::stoi(it->second);
        }
```

#### 3.开始设置请求体：
我们先构建消息列表，注意消息是一个`Json::arrayValue`，是一个数组类型的，但是注意这个`Json::arrayValue`不是类型，他是一个枚举的类。同时这个数组里面依旧是一个对象 `Json::objectValue`，这样我们可以利用迭代器循环初始化`messageArray`。

```cpp
 Json::Value requestBody;
        Json::Value messagesArray(Json::arrayValue);
        for(auto& msg : messages)
        {
            Json::Value messageObj(Json::objectValue);
            messageObj["role"] = msg._role;
            messageObj["content"] = msg._content;
            messagesArray.append(messageObj);
        }
```
接下来，我们进行初始化其他的body，比如model：
```cpp
        requestBody["model"] = GetModelName();
        requestBody["messages"] = messagesArray;
        requestBody["temperature"] = temperature;
        requestBody["max_tokens"] = max_tokens;

```

#### 4. 初始化客户端，设置请求头
我们利用httplib库来进行初始化我们的客户端：
>cpp-httplib 是一个 C++ 的 HTTP/HTTPS 库。只需要复制一个头文件 `httplib.h` 就可以使用。**作者：** yhirose，住在纽约 Tuxedo Park，GitHub 上有 1200+ 关注者,是个人开发者。 [NeuryCode](https://neurycode.com/blog/5-best-ai-tools-for-developers-2025)
**规模：** 整个库就一个文件，20112 行代码，661KB，GitHub 上 16.4k 星标，2700 fork

```cpp

        httplib::Client client(_Endpoint.c_str());
        client.set_connection_timeout(30,0);
        client.set_read_timeout(60,0);
        httplib::Headers headers = {
            {"Authorization", "Bearer " + _ApiKey},
            {"Content-Type", "application/json"},
        };
```

#### 5.进行序列化：
我们对网络上的请求体都需要进行序列化：
```cpp
        Json::StreamWriterBuilder builder;
        std::string requestBodyStr = Json::writeString(builder, requestBody);
```
这个写法也是更加现代的写法。

#### 6. 开始发送：
我们需要设置，请求的路径，还有给出请求体和请求头：利用httplib的Post方法，来完成这个：
```cpp
        Json::StreamWriterBuilder builder;
        std::string requestBodyStr = Json::writeString(builder, requestBody);
        // 可以开始发送请求了：
        auto res = client.Post("/v1/chat/completions", headers, requestBodyStr, "application/json");
        if(!res)
        {
            ERROR("KimiProvider sendMessage POST request failed, error: {}",httplib::to_string(res.error()));
            return "";
        }
        if(res->status != 200)
        {
            ERROR("status: {}, body: {}", res->status, res->body);
            return "";
        }
```
我们对于不是自己的200，都进行报错，和为什么会报错：
![PixPin_2026-05-15_16-14-41.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/PixPin_2026-05-15_16-14-41.png)
比如这样的，这样就会出现报错了，我这里是请求路径出现了问题，少了一个 `/`给我找了半半天。
收到res，之后就要对`res->body`进行反序列化了。

#### 7. 反序列化和提取需要的信息:
```cpp
Json::CharReaderBuilder readerBuilder;
        Json::Value responseBody;
        std::string err;
        std::istringstream stream(res->body);
        if(!Json::parseFromStream(readerBuilder, stream, &responseBody, &err))
        {
            ERROR("KimiProvider sendMessage JSON parse failed, error: {}",err);
            return "";
        }
        if(responseBody.isMember("choices") && !responseBody["choices"].empty())
        {
            if(responseBody["choices"][0].isMember("message") && responseBody["choices"][0]["message"].isMember("content"))
            {
                std::string content = responseBody["choices"][0]["message"]["content"].asString();
                return content;
            }
            else
            {
                ERROR("KimiProvider sendMessage JSON parse failed, no content field in response");
                return "";
            }
        }
        else
        {
            ERROR("KimiProvider sendMessage JSON parse failed, no choices field in response");
            return "";
        }
```
对于这个反序列化，我们可以到我们对于找不到就，放回"",同时，我们需要先把`res->body`转换成流。这样才能进行反序列化。

### 2.4.3 测试:
![PixPin_2026-05-15_16-22-33.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/PixPin_2026-05-15_16-22-33.png)

先cd 到 test/build的路径下面来，随后编写CMakeLists.txt的时候，需要加上KimiProvider.cpp.
随后利用Google的测试脚手架来完成我们的测试，由于我怕麻烦，这里的apikey就没有放入环境中变量了。
```cpp
TEST(KimiProviderTest,SendMessage)
{
    auto provider = std::make_shared<AI_Chat_SDK::KimiProvider>();
    // 如果是不为空的话，就断言为正
    ASSERT_TRUE(provider != nullptr);
    std::map<std::string,std::string> config;
    config["api_key"] = "your api key";
    config["base_url"] = "https://api.siliconflow.cn";
    provider->Init(config);
    // 断言provider可用
    ASSERT_TRUE(provider->IsAvailable());
    std::map<std::string,std::string> params;
    params["temperature"] = "0.6";
    params["max_tokens"] = "2048";
    std::vector<AI_Chat_SDK::Message> messages;
    messages.push_back({"user", "你是谁"});

    // std::string responce = provider->SendMessageStream(messages,params,
    //     [&](std::string content, bool isComplete)
    //     {
    //         INFO("stream content: {}", content.c_str());
    //         if(isComplete) INFO("[DONE]");
    //     });
    //INFO("{}",responce.c_str());
    std::string responce = provider->SendMessage(messages,params);
    // 断言响应不为空
    INFO("{}",responce.c_str());
    ASSERT_FALSE(responce.empty());
}


int main(int argc, char **argv)
{
    bite::Logger::InitLogger("TestLLM", "stdout", spdlog::level::info);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

```
![PixPin_2026-05-15_16-26-51.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/PixPin_2026-05-15_16-26-51.png)
就是在官网的api获取的key。你自己填写自己的，一般可以把它设置在环境变量里面。
随后我们可以看到这个结果：
![PixPin_2026-05-15_16-28-28.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/PixPin_2026-05-15_16-28-28.png)

完美。

## 2.4 对流式开始进行编写和测试：
#### 1. 我们先对对apifox进行测试：
![PixPin_2026-05-15_19-17-54.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/PixPin_2026-05-15_19-17-54.png)
我们需要在请求体的里面加上stream 开启，就是置为true。在设置请求头，设置为下图所示：
![PixPin_2026-05-15_19-19-24.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/PixPin_2026-05-15_19-19-24.png)
点击保存，随后发送，可以看到下面的景象：
![PixPin_2026-05-15_19-20-44.png|808](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/PixPin_2026-05-15_19-20-44.png)
我们可以打开以`data:` 开头的数据，我们只要对这个数据进行不断的拆分就可以了。
![PixPin_2026-05-15_19-24-41.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/PixPin_2026-05-15_19-24-41.png)

#### 2. 开始梭哈代码：
前面的不用说流程类似：
开始从Param中读取相关值，随后开始构建请求体，这里需要带上`stream == true`，需要构建的是消息列表。随后构建客户端，顺便设置超时链接时间和请求头。
```cpp
        if(!IsAvailable()) { return ""; }
        // 查找温度和max_tokens
        double temperature = 0.7;
        int max_tokens = 4096;
        auto it = requestParam.find("temperature");
        if (it != requestParam.end()) {
            temperature = std::stod(it->second);
        }
        it = requestParam.find("max_tokens");
        if (it != requestParam.end()) {
            max_tokens = std::stoi(it->second);
        }
        //构建历史消息
        Json::Value requestBody;
        Json::Value messagesArray(Json::arrayValue);
        for(auto& msg : messages)
        {
            Json::Value messageObj(Json::objectValue);
            messageObj["role"] = msg._role;
            messageObj["content"] = msg._content;
            messagesArray.append(messageObj);
        }
        requestBody["model"] = GetModelName();
        requestBody["messages"] = messagesArray;
        requestBody["temperature"] = temperature;
        requestBody["max_tokens"] = max_tokens;
        requestBody["stream"] = true;
        std::string requestBodyStr = Json::writeString(Json::StreamWriterBuilder(), requestBody);
        //构建客户端：
        httplib::Client client(_Endpoint.c_str());
        client.set_read_timeout(200,0);
        client.set_connection_timeout(30,0);
        httplib::Headers headers =
        {
            {"Content-Type", "application/json"},
            {"Authorization", "Bearer " + _ApiKey},
            {"Accept", "text/event-stream"}
        };
```
这里都是大差不差的。我们开始设置后面重要的两个函数：
第一个是设置如果转台吗不对，应该打印什么消息。
```cpp
        request.response_handler = [&](const httplib::Response& res)
        {
            if (res.status != 200)
            {
                gotError = true;
                MsgError = res.body;
                statusCode = res.status;
                ERROR("HTTP status: {}, body:{} ", statusCode, MsgError);
                return false;
            }
            return true;
        };

```
第二个函数则开始对返回的流式信息开始做处理，这还是比较难的，我们这里可以分成：
1. 先将本次接收的body给接收了，随后查找 `\n\n`，进行对一行的分类
2. 随后对行开始操作，如果是空的或者是注释，直接跳过
3. 如果是 `data：`则开始进行解析，进行反序列化和提取相关内容

```cpp
request.content_receiver = [&](const char *data, size_t len, uint64_t offset,
                           uint64_t totalLength)
        {
            if(gotError == true) return false;
            buff.append(data, len);
            INFO("Kimi Send Msg {}", buff);
            size_t pos = 0;
            while((pos = buff.find("\n\n")) != std::string::npos)
            {
                std::string line = buff.substr(0, pos);
                buff.erase(0, pos + 2);
                if(line.empty() || line[0] == ':') continue;
                if(line.compare(0,6,"data: ") == 0)
                {
                    // 如果出现了data: 则解析数据
                    std::string modelData = line.substr(6);
                    if(modelData.compare(0, 5, "[DONE]") == 0)
                    {
                        callback("", true);
                        IsComplete = true;
                        return false;
                    }
                    Json::CharReaderBuilder builder;
                    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
                    Json::Value root;
                    std::string errs;
                    if(reader->parse(modelData.c_str(), modelData.c_str() + modelData.size(), &root, &errs))
                    {
                        if(root.isMember("choices") && root["choices"].isArray() && root["choices"].size() > 0
                            && root["choices"][0].isMember("delta") && root["choices"][0]["delta"].isMember("content"))
                        {
                            //后面有机会可以做思考链得显示
                            //std::string thing = root["choices"][0]["delta"]["reasoning_content"].asString();
                            std::string content = root["choices"][0]["delta"]["content"].asString();

                            if(!content.empty())
                            {
                                callback(content, false);
                                fullResponse += content;
                                return true;
                            }
                        }
                    }
                }
            }
            return true;
        };
```
最后开始进行发送：
```cpp
        auto result = client.send(request);
        if(!result)
        {
            ERROR("send request failed:{}", to_string(result.error()));
            return "";
        }
        if(!IsComplete)  // 如果没有结束
        {
            DEBUG("stream not finish");
            callback("", true);
        }
        return fullResponse;
```

#### 3. 进行测试：
先看结果，一次成功。爽
![PixPin_2026-05-15_20-58-19.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/PixPin_2026-05-15_20-58-19.png)
```cpp
TEST(KimiProviderTest,SendMessage)
{
    auto provider = std::make_shared<AI_Chat_SDK::KimiProvider>();
    // 如果是不为空的话，就断言为正
    ASSERT_TRUE(provider != nullptr);
    std::map<std::string,std::string> config;
    config["api_key"] = "your api key";
    config["base_url"] = "https://api.siliconflow.cn";
    provider->Init(config);
    // 断言provider可用
    ASSERT_TRUE(provider->IsAvailable());
    std::map<std::string,std::string> params;
    params["temperature"] = "0.6";
    params["max_tokens"] = "2048";
    std::vector<AI_Chat_SDK::Message> messages;
    messages.push_back({"user", "你是谁"});

    std::string responce = provider->SendMessageStream(messages,params,
        [&](std::string content, bool isComplete)
        {
            INFO("stream content: {}", content.c_str());
            if(isComplete) INFO("[DONE]");
        });
    INFO("{}",responce.c_str());
    //std::string responce = provider->SendMessage(messages,params);
    // 断言响应不为空
    //INFO("{}",responce.c_str());
    ASSERT_FALSE(responce.empty());
}


int main(int argc, char **argv)
{
    bite::Logger::InitLogger("TestLLM", "stdout", spdlog::level::info);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
```
![PixPin_2026-05-15_21-00-05.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/PixPin_2026-05-15_21-00-05.png)


# 3. 结语：

![PixPin_2026-05-15_21-01-04.png](https://cdn.jsdelivr.net/gh/Ningbottle/blog-images@main/img/PixPin_2026-05-15_21-01-04.png)
今日份收获，希望大家能看到这里吧