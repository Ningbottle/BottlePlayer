# 1. 万剑归宗：终极外观层 ChatSDK 登场
历经前面 9 篇专栏的硬核鏖战，我们从最底层的 HTTP 拆包（SSE 与流式截断）一路杀到了 SQLite 数据库的并发免锁设计。现在，所有的零件都已打造完毕。
但是，如果你把这堆散装的管理器丢给前端同学（或者 QT 界面开发人员），他们一定会抓狂：我只是想发一句“你好”，凭什么要我去手动调数据库、拼历史记录，然后再去找 LLMManager 发送？
为了让 SDK 达到真正的**企业级易用标准**，我们需要最后一步：**外观模式（Facade Pattern）**。今天我们要手撕的 `ChatSDK.cpp`，就是整个大模型架构的终极指挥官。它将屏蔽底层所有的血雨腥风，向外界暴露出最极简的接口。
![最高层封装](file:///C:/Users/w1521/.gemini/antigravity/brain/8b0fcc1f-c673-43ec-b7cc-9311a41c4355/chatsdk_01_facade_1782288324798.png)
# 2. 头文件设计：左手网络，右手磁盘
打开 `ChatSDK.h`，它的私有成员极度干净，只有两只替它卖命的“神兽”：
- `_LLMManager`：负责管理 DeepSeek、GLM、Ollama 等所有网络请求引擎。
- `_SessionManager`：负责管理内存缓存和底层 SQLite 磁盘落盘。
外面的世界不管发生什么，只要调用 `ChatSDK` 的公共接口即可。
```cpp
// include/ChatSDK.h
#pragma once
#include "common.h"
#include "LLMManager.h"
#include "SessionManager.h"
#include <unordered_map>

namespace AI_Chat_SDK
{
    class ChatSDK
    {
    public:
        ChatSDK(std::string dbName = "chatDB.db");
        // 核心接口一：大一统初始化
        bool InitAllModels(const std::vector<std::shared_ptr<Config>>& configs);
        
        // 核心接口二：会话生命周期管理
        std::string CreateSession(const std::string& modelName);
        std::vector<std::string> GetSessionList();
        std::shared_ptr<Session> GetSession(const std::string& sessionId);
        bool DeleteSession(const std::string& sessionId);
        std::vector<ModuleInfo> GetAvailableModels();
        
        // 核心接口三：极简对话入口
        std::string SendMessage(const std::string& sessionId, const std::string& message);
        std::string sendMessageStream(const std::string& sessionId, const std::string& message,
                                            std::function<void(const std::string&, bool)> callback);
    private:
        void RegisterAllProviders(const std::vector<std::shared_ptr<Config>>& configs);
        void InitAllProviders(const std::vector<std::shared_ptr<Config>>& configs);
        bool InitApiModules(std::string moduleName,const std::shared_ptr<ApiConfig>& configs);
        bool InitOllamaModules(std::string moduleName,const std::shared_ptr<OllamaConfig>& configs);

    private:
        bool _IsInitialized = false;     // 全局护城河拦截器
        LLMManager _LLMManager;          // 左手网络调度
        SessionManager _SessionManager;  // 右手数据存储
        std::unordered_map<std::string, std::shared_ptr<Config>> _moduleConfigs;
    };
};
```
# 3. 破解配置谜题：RTTI 动态向下转型
在系统初始化时，用户会丢进来一个装着各种配置的数组 `std::vector<std::shared_ptr<Config>>`。
这里遇到一个**多态（Polymorphism）的棘手问题**：因为云端模型需要的是 `api_key`，而本地 Ollama 需要的是 `endpoint`（IP 端口），它们的子类结构完全不同！我们怎么在遍历基类指针 `Config` 时，认出谁是谁，并把它们扔进正确的初始化通道呢？
## 3.1 机理大揭秘：`std::dynamic_pointer_cast`
C++ 为我们提供了 **RTTI（Run-Time Type Identification，运行时类型识别）** 的终极武器：**向下转型（Downcasting）**。
就像用放大镜观察一个黑盒子，`dynamic_pointer_cast<ApiConfig>` 会去试探这个基类指针。如果是云端配置，它会成功转为子类指针；如果是 Ollama 本地配置，它就会安全地返回一个空指针（`nullptr`），不会引发任何崩溃！
![向下转型](file:///C:/Users/w1521/.gemini/antigravity/brain/8b0fcc1f-c673-43ec-b7cc-9311a41c4355/chatsdk_02_cast_1782288336942.png)
## 3.2 一步一步手撕初始化逻辑
```cpp
#include "../include/ChatSDK.h"
#include "../include/DeepSeekProvider.h"
#include "../include/GLMProvider.h"
#include "../include/KimiProvider.h"
#include "../include/OllamalProvider.h"
#include <memory>

namespace AI_Chat_SDK
{
    ChatSDK::ChatSDK(std::string dbName)
        : _SessionManager(dbName)
    {
    }

    // 第一步：注册所有模型引擎（这里以云端固定注册，本地动态注册为例）
    void ChatSDK::RegisterAllProviders(const std::vector<std::shared_ptr<Config>>& configs)
    {
        if(!_LLMManager.isModelAvailable("deepseek-v4-flash"))
            _LLMManager.registerModel("deepseek-v4-flash", std::make_unique<DeepSeekProvider>());
            
        if(!_LLMManager.isModelAvailable("glm-4.7-flash"))
            _LLMManager.registerModel("glm-4.7-flash", std::make_unique<GLMProvider>());
            
        // 通过 dynamic_pointer_cast 找出 Ollama 的配置并注册
        std::unordered_set<std::string> ModuleName; 
        for(const auto& config : configs)
        {
            auto ollamaConfig = std::dynamic_pointer_cast<OllamaConfig>(config);
            if(ollamaConfig)
            {
                auto name = ollamaConfig->_module_name;
                if(!_LLMManager.isModelAvailable(name))
                    _LLMManager.registerModel(name,std::make_unique<OllamalProvider>());
            }
        }
    }

    // 第二步：运用 RTTI 向下转型，进行精准的子类分发初始化
    void ChatSDK::InitAllProviders(const std::vector<std::shared_ptr<Config>>& configs)
    {
        for(const auto& config : configs)
        {
            // 拿着云端的放大镜去照
            auto apiConfig = std::dynamic_pointer_cast<ApiConfig>(config);
            if(apiConfig)
            {
                InitApiModules(apiConfig->_module_name, apiConfig);
            }
            // 如果照不出云端，就换本地 Ollama 的放大镜去照
            else if(auto ollamaConfig = std::dynamic_pointer_cast<OllamaConfig>(config))
            {
                InitOllamaModules(ollamaConfig->_module_name, ollamaConfig);
            }
            else
            {
                ERROR("Unsupported config type: {}", config->_module_name);
            }
        }
    }

    bool ChatSDK::InitAllModels(const std::vector<std::shared_ptr<Config>>& configs)
    {
        RegisterAllProviders(configs);
        InitAllProviders(configs);
        _IsInitialized = true; // 开启护城河！
        return true;
    }
```
*(注：`InitApiModules` 等函数的内部逻辑就是把对应参数装进 `map` 后调用 `_LLMManager.initModel`，这里为保紧凑不再赘述。)*
# 4. 终极奥义：标准通信流水线 (SendMessage)
现在，到了全盘揭晓的时刻！
当用户在 UI 界面敲下一句“今天天气真好”，并且传入 `sessionId` 时，底层的运转流程堪称是一场壮丽的接力赛。
![标准通信流](file:///C:/Users/w1521/.gemini/antigravity/brain/8b0fcc1f-c673-43ec-b7cc-9311a41c4355/chatsdk_03_flow_1782288348202.png)
## 4.1 一步一步手撕全自动流水线
在这短短的几十行代码里，完美融合了我们之前写过的所有模块：
- **步骤一（存户入库）**：将用户发来的纯文本，组装成 `Message("user", message)`，直接丢给 `_SessionManager`。它会在内存和 SQLite 硬盘里双重落盘。
- **步骤二（取史备考）**：从数据库里抽出该会话（`sessionId`）所有的历史消息，构建出供 AI 思考的上下文（`messages`）。
- **步骤三（参数加装）**：去 `_moduleConfigs` 里翻出用户配置的温度（`temperature`）和上下文长度，装进字典。
- **步骤四（路由发车）**：把历史记录和回调函数 `callback`，一脚油门踩进 `_LLMManager`。从此数据穿梭于网络。
- **步骤五（回信盖戳）**：收到 AI 回复后，再次组装 `Message("assistant", response)` 入库落盘，并调用 `UpdateSessionTimeStamp` 给会话盖上最新的时间戳（让 UI 界面把这个会话置顶）。
整个过程，滴水不漏！
```cpp
    std::string ChatSDK::sendMessageStream(const std::string &sessionId, const std::string &message,
        std::function<void (const std::string &, bool)> callback)
    {
        if (!_IsInitialized)
        {
            ERROR("ChatSDK is not initialized");
            return "";
        }
        
        // 查验合法性
        auto Session = _SessionManager.GetSession(sessionId);
        if (!Session)
        {
            ERROR("Session not found: {}", sessionId);
            return "";
        }
        
        // 【步骤一】组装用户数据并落盘
        Message userMessage("user", message);
        _SessionManager.AddMessage(sessionId, userMessage);
        
        // 【步骤二】提取完整的上下文历史（这决定了大模型有没有记忆！）
        std::vector<Message> messages = _SessionManager.GetMessage(sessionId);
        
        // 【步骤三】提取并装配特定模型的偏好参数
        std::map<std::string, std::string> requestParam;
        auto it = _moduleConfigs.find(Session->_model_name);
        if(it != _moduleConfigs.end())
        {
            requestParam["temperature"] = std::to_string(it->second->_temperature);
            requestParam["max_tokens"] = std::to_string(it->second->_max_tokens) ;
        }
        
        // 【步骤四】调用万能路由，向网络世界开炮！
        std::string response = _LLMManager.sendMessageStream(Session->_model_name, messages, requestParam, callback);
        if(response.empty())
        {
            ERROR("sendMessageStream failed: {}", response);
            return "";
        }
        
        // 【步骤五】获取网络回信，组装机器人消息落盘
        Message assistantMessage("assistant", response);
        _SessionManager.AddMessage(sessionId, assistantMessage);
        
        // 盖上最新时间戳，完成整个交互闭环
        _SessionManager.UpdateSessionTimeStamp(sessionId);
        INFO("ChatSDK::sendMessageStream: send message to model {} successed", Session->_model_name);
        return response;
    }
```
# 5. C++ 大模型 SDK 篇：完美收官！
至此，我们《从零开始手写企业级 AI SDK》的核心架构已经**全线贯通**！
回首望去，从构建最底层的 `common.h` 实体类，到利用**多态架构**封装 `DeepSeekProvider`、`GLMProvider` 与 `OllamalProvider`；从手写流式拆包器（`char_receiver`）到破除**死锁危机**的 `DataManager` 持久化；再到利用**无锁并发**的 `SessionManager`，直到今天用**外观模式**在 `ChatSDK.cpp` 结下最完美的顶层封印。
经过这十篇地狱般的硬核洗礼，你已经彻底掌握了现代 C++ 在网络、并发、面向对象设计与持久化领域的全部精华。
接下来，你可以用 Qt/ImGui/Electron 随时挂载这个神级 SDK，属于你自己的满血大模型客户端，已经触手可及！
（全文完）
