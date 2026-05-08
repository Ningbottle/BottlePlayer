---
tags:
  - 面试
  - ai-sdk
  - 项目
  - 架构
  - 第五天
---

# 第五天：AI-SDK项目架构

## 今日目标

后 3 天开始主攻项目。今天只做一件事：把 AI-SDK 项目讲成一个清晰、可追问、能体现 C++/Linux/网络能力的项目。

## 关联原笔记

- [[从零开始接入AI-SDK/AI_SDK/README]]
- [[从零开始接入AI-SDK/AI_SDK/BUILD_SUMMARY]]
- [[从零开始接入AI-SDK/07-LLMManager大模型管家的实现]]
- [[从零开始接入AI-SDK/08-DataManager数据管家的实现]]
- [[从零开始接入AI-SDK/09-SessionManager会话管家的实现]]
- [[从零开始接入AI-SDK/10-ChatSDK终极封装大满贯]]
- [[从零开始接入AI-SDK/11-面试突击篇（上）——C++高级特性与并发安全]]
- [[从零开始接入AI-SDK/12-面试突击篇（下）——架构设计与网络流式传输]]

## A. 项目介绍题

1. 用 30 秒介绍你的 AI-SDK 项目。
2. 用 1 分钟介绍你的 AI-SDK 项目。
3. 用 3 分钟介绍你的 AI-SDK 项目。
4. 这个项目解决了什么问题？
5. 为什么要做一个 SDK，而不是直接在业务里调用大模型 API？
6. 项目面向的使用者是谁？
7. 这个项目的核心功能有哪些？
8. 项目最终效果是什么？
9. 项目中最能体现 C++ 能力的地方是什么？
10. 项目中最能体现 Linux/网络能力的地方是什么？
11. 项目中最能体现工程能力的地方是什么？
12. 项目里你最熟悉的模块是哪一个？
13. 项目里你觉得最难的模块是哪一个？
14. 项目里最值得面试官追问的点是什么？
15. 项目如果写在简历上，你会怎么写 3 条 bullet？
16. 这个项目和普通 API demo 的区别是什么？
17. 这个项目有没有真实部署或运行？
18. 这个项目支持哪些模型？
19. 这个项目的数据如何持久化？
20. 这个项目如何支持流式对话？

> [!tip]- 口述框架
> - 背景：多个模型 API 调用方式不统一。
> - 目标：封装成统一 C++ SDK，并提供 HTTP 服务和前端对话。
> - 架构：ChatSDK 统一入口，下面分 LLMManager、SessionManager、DataManager、Provider。
> - 亮点：多态扩展、SQLite 持久化、SSE 流式解析、线程安全。

## B. 总体架构题

1. 画出项目总体架构图。
2. 浏览器发一条消息后，完整链路是什么？
3. `ChatSDK` 的职责是什么？
4. `LLMManager` 的职责是什么？
5. `SessionManager` 的职责是什么？
6. `DataManager` 的职责是什么？
7. `LLMProvider` 的职责是什么？
8. Provider 派生类分别负责什么？
9. ChatServer 和 SDK 层怎么分工？
10. 前端在项目中承担什么？
11. 为什么要把模型管理和会话管理拆开？
12. 为什么要把数据持久化单独拆成 DataManager？
13. 如果 UI 直接调用 DataManager 会有什么问题？
14. 如果 LLMManager 直接操作数据库会有什么问题？
15. 如果 Provider 直接管理 Session 会有什么问题？
16. 你如何理解模块边界？
17. 这个项目用了哪些设计模式？
18. 外观模式在项目中体现在哪里？
19. 多态在项目中体现在哪里？
20. 单一职责原则在项目中体现在哪里？
21. 开闭原则在项目中体现在哪里？
22. 依赖倒置在项目中体现在哪里？
23. 如果让你重构一版，你会保留哪些边界？
24. 如果业务层要换成 Qt 前端，SDK 需要改吗？
25. 如果底层数据库从 SQLite 换成 MySQL，哪些层应该改？

## C. ChatSDK 追问题

1. `ChatSDK` 为什么要作为统一入口？
2. `ChatSDK` 对外暴露哪些核心接口？
3. `ChatSDK` 初始化流程是什么？
4. `ChatSDK` 如何注册所有 Provider？
5. `ChatSDK` 如何创建 Session？
6. `ChatSDK` 发送普通消息的流程是什么？
7. `ChatSDK` 发送流式消息的流程是什么？
8. `ChatSDK` 如何处理未初始化状态？
9. `ChatSDK` 如何向上层隐藏底层复杂性？
10. `ChatSDK` 的接口为什么尽量使用基础类型？
11. 如果 ChatSDK 构造失败，应该如何设计错误返回？
12. ChatSDK 是否应该是单例？
13. ChatSDK 是否应该允许拷贝？
14. ChatSDK 的生命周期由谁管理？
15. ChatSDK 和 ChatServer 的关系是什么？
16. ChatSDK 如果被多线程调用，需要注意什么？
17. ChatSDK 是否应该直接持有 API key？
18. ChatSDK 是否应该暴露底层 Provider 指针？
19. ChatSDK 如何扩展新模型？
20. ChatSDK 如果接口越来越多，怎么防止膨胀？

## D. LLMManager 追问题

1. LLMManager 为什么叫模型管理器？
2. LLMManager 内部维护什么数据结构？
3. 模型名到 Provider 的映射怎么做？
4. 为什么 Provider 用基类指针保存？
5. 为什么可以用 `unique_ptr<LLMProvider>`？
6. 注册模型时为什么需要所有权转移？
7. 初始化模型和注册模型为什么分开？
8. `isModelAvailable` 用来解决什么问题？
9. 如果模型名不存在，LLMManager 应该怎么处理？
10. LLMManager 是否负责 JSON 解析？
11. LLMManager 是否负责网络请求细节？
12. LLMManager 如何体现路由与代理？
13. 如果新增模型，只改 Provider 是否足够？
14. 如果多个模型参数不同，LLMManager 如何传递参数？
15. LLMManager 是否线程安全？
16. 如果并发注册模型，会有什么风险？
17. 如果并发发送消息，会有什么风险？
18. LLMManager 如何做错误日志？
19. LLMManager 如何做模型健康检查？
20. LLMManager 如何支持模型降级或 fallback？
21. LLMManager 用 `map` 和 `unordered_map` 哪个更合适？
22. LLMManager 如何避免大量 `if-else`？
23. LLMManager 如果直接依赖 DeepSeekProvider，会有什么问题？
24. LLMManager 的单元测试应该怎么写？
25. LLMManager 在面试中最容易被问哪些 C++ 点？

## E. Provider 追问题

1. `LLMProvider` 抽象基类定义了哪些能力？
2. Provider 为什么要抽象 `Init`？
3. Provider 为什么要抽象 `SendMessage`？
4. Provider 为什么要抽象 `SendMessageStream`？
5. DeepSeekProvider 的协议特点是什么？
6. GLMProvider 的协议特点是什么？
7. KimiProvider 的特殊处理是什么？
8. OllamaProvider 的协议特点是什么？
9. 云端模型和本地模型配置有什么差异？
10. API key 应该在哪里传入？
11. endpoint 应该在哪里配置？
12. Provider 是否应该关心 Session？
13. Provider 是否应该关心数据库？
14. Provider 如何处理 HTTP 失败？
15. Provider 如何处理 JSON 解析失败？
16. Provider 如何处理空 chunk？
17. Provider 如何处理上游 `[DONE]`？
18. Provider 如何处理超时？
19. Provider 是否应该重试？
20. Provider 的错误如何向上传递？
21. Provider 的流式 callback 应该如何设计？
22. Provider 如何避免阻塞上层太久？
23. Provider 如何支持新模型？
24. Provider 如何做单元测试？
25. Provider 层最能体现网络能力的问题是什么？

## F. 架构图训练

1. 画出模块层级图。
2. 画出普通消息发送时序图。
3. 画出流式消息发送时序图。
4. 画出模型注册初始化流程图。
5. 画出 Session 创建和持久化流程图。
6. 画出 Provider 多态关系图。
7. 画出 ChatServer HTTP 路由表。
8. 画出错误从 Provider 传到前端的路径。
9. 画出新增模型的改动点。
10. 画出从浏览器输入到 markdown 渲染的链路。

> [!tip]- 画图要求
> - 图里只放模块和数据流，不写答案段落。
> - 每张图都能口述 1 分钟。
> - 画完问自己：面试官会沿哪条箭头追问？

## 今日验收

- [ ] 能 30 秒、1 分钟、3 分钟三个版本介绍项目。
- [ ] 能画出项目总体架构图。
- [ ] 能说清 ChatSDK / LLMManager / SessionManager / DataManager / Provider 的边界。
- [ ] 能解释项目里至少 3 个设计模式或设计原则。
- [ ] 能回答新增模型需要改哪里。

