# 1. 拨云见日：统一调度中心 LLMManager 的诞生
在前面的几篇专栏中，我们像拼乐高一样，一块一块地实现了 `DeepSeekProvider`、`GLMProvider` 和本地的 `OllamaProvider`。它们各自都身怀绝技，能和对应的服务器无缝通信。
但是，对于最上层的 UI 界面（或者是想要调用这个 SDK 的小白用户）来说，他们根本不想知道什么是 `Json::CharReader`，也不想管到底发的是什么 HTTP 请求。他们只想要一个极其简单的接口：“给我发个消息给 DeepSeek，然后把字弹出来”。
这就要求我们必须在所有的 Provider 之上，构建一个**大模型管家（LLMManager）**。它将作为整个模块的**外观模式（Facade Pattern）**入口，统一掌管所有模型的生杀大权与消息路由。
![统一调度中心](file:///C:/Users/w1521/.gemini/antigravity/brain/8b0fcc1f-c673-43ec-b7cc-9311a41c4355/llm_manager_01_role_1782286280724.png)
# 2. 头文件设计：状态与实体的双重映射
打开头文件，你会发现 `LLMManager` 并没有继承任何基类，因为它就是发号施令的“王”。这里最核心的设计机理在于它的私有变量区域：我们使用了两个 `std::map`，将“模型实例”和“模型状态”进行了**状态分离**。
![双Map映射](file:///C:/Users/w1521/.gemini/antigravity/brain/8b0fcc1f-c673-43ec-b7cc-9311a41c4355/llm_manager_03_maps_1782286304439.png)
```cpp
// include/LLMManager.h
#pragma once
#include "LLMProvider.h"
#include "common.h"
#include <memory>
#include <vector>

namespace AI_Chat_SDK
{
    class LLMManager
    {
    public:
        // 1. 注册模型（把 Provider 实体交给管家）
        bool registerModel(const std::string& modelName, std::unique_ptr<LLMProvider> llmProvider);
        // 2. 初始化模型（注入 API Key 等配置）
        bool initModel(const std::string& modelName,const std::map<std::string, std::string>& param);
        // 3. 检测模型是否可用
        bool isModelAvailable(const std::string& modelName) const;
        // 4. 获取当前可用的所有模型列表
        std::vector<ModuleInfo> getAvailableModels() const;
        // 5. 路由：发送消息（非流式）
        std::string sendMessage(const std::string& modelName, const std::vector<Message> message,
            const std::map<std::string, std::string>& requestParam);
        // 6. 路由：发送消息（流式）
        std::string sendMessageStream(const std::string& modelName, const std::vector<Message> message,
            const std::map<std::string, std::string>& requestParam,
            std::function<void(const std::string&,bool)> onChunk);

    private:
        // 存放真实干活的模型实体，因为使用了多态基类指针，所以必须用智能指针管理生命周期
        std::map<std::string, std::unique_ptr<LLMProvider>> _llmProviders;
        // 存放供 UI 界面查询的模型状态信息（如是否可用、模型描述等）
        std::map<std::string, ModuleInfo> _moduleInfo;
    };
}
```
# 3. 核心功能拆解：管理者的日常
我们依次点开 `LLMManager.cpp` 的每一个函数，看看管家是怎么一步一步干活的。
## 3.1 实体收容：registerModel
- **怎么来的**：当系统（或 `ChatSDK` 初始化模块）启动时，会主动调用这个函数，把提前 `new` 好的 `DeepSeekProvider` 等实例注册进管家的花名册里。
- **所用工具与机理**：这里用到了 C++11 最核心的内存管理机制——**独占智能指针（`std::unique_ptr`）** 和 **所有权转移（`std::move`）**。因为 Provider 实体是多态的，大小不一，只能存在堆（Heap）上。而独占指针保证了同一时刻只有一个主人，防止内存泄漏。
- **一步一步手撕逻辑**：
  **第一步：空指针防御**。如果外面传进来一个空指针，直接报错打回，防止后面调用时引发**空指针解引用崩溃（Null Pointer Dereference）**。
  **第二步：所有权移交**。用 `std::move(llmProvider)` 把传入指针里的控制权，强制剥夺并交给我们私有成员字典 `_llmProviders[modelName]`。此时外面的指针就失效了，管家正式接管了这个模型的生老病死。
![所有权转移](file:///C:/Users/w1521/.gemini/antigravity/brain/8b0fcc1f-c673-43ec-b7cc-9311a41c4355/llm_manager_02_move_1782286291860.png)
```cpp
#include "../include/LLMManager.h"

namespace AI_Chat_SDK
{
    bool LLMManager::registerModel(const std::string &modelName, std::unique_ptr<LLMProvider> llmProvider)
    {
        if(!llmProvider)
        {
            ERROR("llmProvider is nullptr");
            return false;
        }
        _llmProviders[modelName] = std::move(llmProvider);
        INFO("Model {} registered successfully", modelName);
        return true;
    }
```
## 3.2 唤醒模型与状态同步：initModel
- **怎么来的**：模型实体被注册后，它们还只是处于“休眠”状态（没有 API Key）。这时候需要读取本地 SQLite 数据库或配置文件，把账号密码打包成 `param` 字典传入。
- **底层机理**：这不仅仅是初始化，更是一次**状态同步**。当底层 Provider 初始化成功后，管家必须把这个好消息上报更新到专门记录状态的 `_moduleInfo` 字典里，以备未来 UI 界面查询。
- **一步一步手撕逻辑**：
  **第一步：在实体库中捞取指针**。利用 `find` 从 `_llmProviders` 里找出对应的模型。找不到就报错。
  **第二步：触发底层初始化**。调用 `it->second->Init(param)`。这里的 `Init` 是一个虚函数，由于**多态**机制，如果你传入的是 DeepSeek 的字典，它就会精准跑到 `DeepSeekProvider::Init` 里去执行。
  **第三步：信息同步录入**。如果初始化返回 true，我们就通过 `ModelDesc()` 获取该模型的简介，并把名字和可用状态（`_isAvailable = true`）一起写进右边的状态柜子 `_moduleInfo` 里。
```cpp
    bool LLMManager::initModel(const std::string &modelName,const std::map<std::string, std::string>& param)
    {
        auto it = _llmProviders.find(modelName);
        if(it == _llmProviders.end())
        {
            ERROR("Model {} not Registered", modelName);
            return false;
        }
        bool res = it->second->Init(param);
        if(!res)
        {
            ERROR("Model {} init failed", modelName);
            return false;
        }
        _moduleInfo[modelName]._model_desc = it->second->ModelDesc();
        _moduleInfo[modelName]._model_name = modelName;
        _moduleInfo[modelName]._isAvailable = true;
        INFO("Model {} initialized successfully", modelName);
        return res;
    }
```
## 3.3 状态对外公示：查询机理
UI 界面在画下拉列表时，需要知道现在哪些模型能用。
- **机理**：这里严格遵循了**职责分离原则**。查询操作绝不触碰复杂的 `_llmProviders` 实体池，只去查极为轻量的 `_moduleInfo`。
- **逻辑剥析**：`isModelAvailable` 先看实体池里有没有它，再看状态池里它的 `_isAvailable` 是否为 true。而 `getAvailableModels` 则直接遍历状态池，把所有能用的装进 `vector` 里返给界面。
```cpp
    bool LLMManager::isModelAvailable(const std::string& modelName) const
    {
        if (_llmProviders.find(modelName) == _llmProviders.end())
            return false;
        auto infoIt = _moduleInfo.find(modelName);
        return infoIt != _moduleInfo.end() && infoIt->second._isAvailable;
    }

    std::vector<ModuleInfo> LLMManager::getAvailableModels() const
    {
        std::vector<ModuleInfo> res;
        for(auto &pair : _moduleInfo)
        {
            if(pair.second._isAvailable)
            {
                res.push_back(pair.second);
            }
        }
        return res;
    }
```
# 4. 极致路由：SendMessage 派发枢纽
最后，我们迎来了整个 AI_SDK 接收外部命令的最核心路口。
## 4.1 函数介绍与机理
- **怎么来的**：当用户在聊天框按下回车，或者拖拽文件分析时，前端的控制器会调用这里的发信函数。
- **核心机理（路由与代理）**：作为一个合格的管家，`LLMManager` 绝对不会亲自下场去解析什么 JSON、拼接什么 `httplib` 请求。它的唯一职责是**路由派发（Routing/Delegation）**。你指名道姓要找 `DeepSeek`，它就在字典里找到那个干活的伙计，把任务原封不动地砸给他。
## 4.2 一步一步手撕路由逻辑
不管是流式还是非流式，逻辑完全一样。
**第一步：合法性与存活双重校验**。先用 `find` 确认花名册里有这个人（防未注册崩溃）；再查状态表 `_moduleInfo` 确认他是不是活着（防未初始化崩溃）。
**第二步：多态代理执行**。直接执行 `it->second->SendMessageStream(...)`。因为我们在底层早就统一了接口，所以这里的调用代码只有一行，却能让全世界不同体系的模型跑起来。而前端传进来的神圣回调函数 `onChunk`，也会在这里顺着网线一直被透传到我们上一篇写的 `char_receiver` 拆包机里！
```cpp
    std::string LLMManager::sendMessage(const std::string& modelName, const std::vector<Message> message,
        const std::map<std::string, std::string>& requestParam)
    {
        auto it = _llmProviders.find(modelName);
        if(it == _llmProviders.end())
        {
            ERROR("Model {} not Registered", modelName);
            return "";
        }
        if(!_moduleInfo[modelName]._isAvailable)
        {
            ERROR("Model {} not available", modelName);
            return "";
        }
        std::string res = it->second->SendMessage(message, requestParam);
        return res;
    }

    std::string LLMManager::sendMessageStream(const std::string& modelName, const std::vector<Message> message,
        const std::map<std::string, std::string>& requestParam,
        std::function<void(const std::string&,bool)> onChunk)
    {
        auto it = _llmProviders.find(modelName);
        if(it == _llmProviders.end())
        {
            ERROR("Model {} not Registered", modelName);
            return "";
        }
        if(!_moduleInfo[modelName]._isAvailable)
        {
            ERROR("Model {} not available", modelName);
            return "";
        }
        std::string res = it->second->SendMessageStream(message, requestParam, onChunk);
        return res;
    }
}
```
# 5. 结语
看完 `LLMManager`，你一定会惊叹于上层代码的简洁！
这正是我们在前面花了几万字、死磕各种网络底层、各种 JSON 防崩溃机制换来的果实。底层的 `LLMProvider` 替我们扛下了所有脏乱差的适配工作，让作为调度中心的 `LLMManager` 能够以极为优雅的姿态，只做内存转移（`std::move`）和状态路由。
现在的架构就像一辆插上了多缸发动机的超跑，模型端已经完全就绪了。但在让这辆车真正冲上赛道之前，我们还需要解决一个非常现实的问题：聊天记录总不能只活在内存里吧？下一篇，我们将深入硬盘底层，看看负责持久化落盘的 `DataManager` 是如何运作的！
