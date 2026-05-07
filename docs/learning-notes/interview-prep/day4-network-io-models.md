---
tags:
  - 面试
  - linux
  - 网络
  - io
  - 第四天
---

# 第四天：网络编程与IO模型

## 今日目标

把 TCP/UDP、socket、HTTP、SSE、select/poll/epoll、Reactor、粘包半包讲顺。今天是前 4 天八股压缩的收口日。

## 关联原笔记

- [[2.Linux/一文总结网络：]]
- [[muduo库/1. 了解前置知识：]]
- [[muduo库/2. 开始muduo库的设计：]]
- [[muduo库/3. 设计buffer模块]]
- [[muduo库/4. 准备socket类]]
- [[muduo库/5.   Channel的设计：]]
- [[从零开始接入AI-SDK/12-面试突击篇（下）——架构设计与网络流式传输]]
- [[从零开始接入AI-SDK/AI_SDK/BUILD_SUMMARY]]

## A. TCP / UDP 基础

1. TCP 和 UDP 的区别是什么？
2. TCP 为什么可靠？
3. TCP 三次握手过程是什么？
4. 为什么不是两次握手？
5. TCP 四次挥手过程是什么？
6. 为什么挥手通常是四次？
7. TIME_WAIT 是什么？
8. TIME_WAIT 为什么需要等待 2MSL？
9. CLOSE_WAIT 是什么？
10. 大量 CLOSE_WAIT 通常说明什么问题？
11. TCP 粘包和半包是什么？
12. 粘包是 TCP 的问题还是应用层协议设计问题？
13. 怎么解决粘包半包？
14. 滑动窗口是什么？
15. 拥塞控制大概做什么？
16. 流量控制和拥塞控制区别是什么？
17. Nagle 算法解决什么问题？
18. TCP keepalive 和 HTTP keep-alive 区别是什么？
19. UDP 适合什么场景？
20. UDP 怎么保证可靠性？
21. TCP 连接断开时，服务端怎么感知客户端异常退出？
22. `SIGPIPE` 和网络写失败有什么关系？
23. AI-SDK 流式传输为什么必须处理半包？
24. SSE 事件边界为什么不能直接等同于 TCP 包边界？
25. 如果大模型返回很慢，TCP 层和应用层分别要考虑什么？

## B. socket 编程

1. socket 是什么？
2. 服务端 socket 编程基本流程是什么？
3. 客户端 socket 编程基本流程是什么？
4. `bind` 做什么？
5. `listen` 做什么？
6. `accept` 返回什么？
7. `connect` 做什么？
8. `send` 和 `write` 区别大吗？
9. `recv` 返回 0 表示什么？
10. 阻塞 socket 和非阻塞 socket 区别是什么？
11. `EAGAIN` / `EWOULDBLOCK` 表示什么？
12. `SO_REUSEADDR` 解决什么问题？
13. 半关闭是什么？
14. 如何设置 fd 非阻塞？
15. 服务端为什么不能一个连接一个进程无限创建？
16. 线程 per connection 模型有什么问题？
17. 连接队列满了会怎样？
18. backlog 是什么？
19. 端口占用怎么排查？
20. AI-SDK 的 ChatServer 对外提供哪些 HTTP 接口？

## C. IO 模型

1. 阻塞 IO 是什么？
2. 非阻塞 IO 是什么？
3. IO 多路复用是什么？
4. 信号驱动 IO 是什么？
5. 异步 IO 是什么？
6. 同步 IO 和异步 IO 怎么区分？
7. `select` 的流程是什么？
8. `select` 有哪些缺点？
9. `poll` 相比 `select` 改进了什么？
10. `epoll` 相比 `select/poll` 改进了什么？
11. `epoll_create`、`epoll_ctl`、`epoll_wait` 分别做什么？
12. LT 和 ET 区别是什么？
13. ET 模式为什么通常要配合非阻塞 fd？
14. 惊群问题是什么？
15. Reactor 模式是什么？
16. Proactor 模式是什么？
17. muduo 为什么强调 one loop per thread？
18. Channel、EventLoop、Poller 分别可以怎么理解？
19. Buffer 解决什么问题？
20. AI-SDK 如果从 cpp-httplib 换成 epoll Reactor，要改哪些层？

> [!tip]- 怎么练
> - IO 模型题要按“等待数据阶段”和“拷贝数据阶段”拆开。
> - epoll 题要讲清 fd 注册、事件就绪、循环处理。
> - Reactor 题尽量联系 muduo 笔记，不要空背概念。

## D. HTTP / HTTPS / SSE

1. HTTP 请求报文由哪些部分组成？
2. HTTP 响应报文由哪些部分组成？
3. GET 和 POST 区别是什么？
4. 常见 HTTP 状态码有哪些？
5. HTTP 是无状态的吗？
6. Cookie 和 Session 分别解决什么问题？
7. HTTPS 比 HTTP 多了什么？
8. TLS 握手大概做什么？
9. 长连接和短连接区别是什么？
10. HTTP keep-alive 做什么？
11. chunked transfer 是什么？
12. SSE 是什么？
13. SSE 和 WebSocket 区别是什么？
14. SSE 为什么适合大模型流式输出？
15. SSE 的 `data:` 行和 `\n\n` 边界有什么意义？
16. `[DONE]` 这类结束标记有什么作用？
17. Ollama 的流式 JSONL 和标准 SSE 有什么差别？
18. 前端读取 SSE 时为什么要维护 buffer？
19. 服务端把 chunk 包成 JSON 有什么好处？
20. markdown 流式输出时为什么容易出现换行问题？
21. 如果网络中断，前端应该怎么处理？
22. 如果后端模型超时，HTTP 层应该怎么返回？
23. 如果 API key 错误，接口应该返回什么信息？
24. AI-SDK 里 DeepSeek、GLM、Kimi、Ollama 的协议差异怎么讲？
25. 面试官问“你项目里的网络难点是什么”，你准备怎么答？

## E. 网络项目追问

1. 你为什么选择 cpp-httplib？
2. cpp-httplib 和自己手写 socket server 有什么区别？
3. 你的服务是同步还是异步？
4. 高并发请求下，服务瓶颈在哪里？
5. 流式响应期间，一个连接会占用什么资源？
6. 如何限制单用户请求频率？
7. 如何设置请求超时？
8. 如何处理上游大模型 API 超时？
9. 如何处理上游返回非 JSON？
10. 如何处理上游半包 JSON？
11. 如何记录一次请求的日志链路？
12. 如何给每个请求加 request id？
13. 如何避免 API key 泄漏？
14. 如何做跨域 CORS？
15. 如何让服务支持 HTTPS？
16. 如何把单机服务部署到云服务器？
17. 如何排查端口被占用？
18. 如何排查服务 502 或连接失败？
19. 如何监控 QPS、延迟、错误率？
20. 如果模型响应特别长，内存如何控制？

## F. 手写题 / 设计题

1. 手写 TCP echo server 基本流程。
2. 手写 TCP client 基本流程。
3. 手写设置 fd 非阻塞函数。
4. 手写 select echo server 伪代码。
5. 手写 epoll echo server 伪代码。
6. 手写按 `\n\n` 拆分 SSE buffer 的伪代码。
7. 手写按 `\n` 拆分 JSONL buffer 的伪代码。
8. 设计一个简单 Reactor 类关系。
9. 设计一个连接超时清理策略。
10. 设计一个 HTTP 接口错误码表。
11. 画出浏览器到 ChatServer 再到 Provider 的请求链路。
12. 画出流式 token 从上游返回到前端显示的链路。

> [!tip]- 手写要求
> - 网络题先说边界：半包、粘包、断开、超时、错误码。
> - buffer 拆包题必须说明“TCP 包边界不等于应用消息边界”。
> - 不需要填完整代码答案，写伪代码结构即可。

## 今日验收

- [ ] 能完整讲 TCP 三次握手和四次挥手。
- [ ] 能解释 TIME_WAIT、CLOSE_WAIT、粘包半包。
- [ ] 能比较 select、poll、epoll。
- [ ] 能讲 SSE 与 JSONL 流式协议差异。
- [ ] 能把 AI-SDK 的流式传输难点讲成项目亮点。

