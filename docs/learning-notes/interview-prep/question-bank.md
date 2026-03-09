---
tags:
  - 面试
  - 题库
  - cpp
  - linux
  - ai-sdk
---

# 总题库：只看题目版

> 用法：最后两天随机抽题。这里故意不放答案和提示，只用于高强度自测。

## C++ 基础 60 题

1. 指针和引用区别。
2. 野指针和悬空指针区别。
3. 数组名和指针区别。
4. const 指针四种写法解释。
5. static 的不同含义。
6. extern 的作用。
7. inline 和宏的区别。
8. constexpr 和 const 区别。
9. volatile 能否保证线程安全。
10. 堆和栈区别。
11. 内存区域划分。
12. new/delete 和 malloc/free 区别。
13. delete 和 delete[] 区别。
14. placement new 是什么。
15. RAII 是什么。
16. 深拷贝和浅拷贝。
17. 三法则、五法则、零法则。
18. 拷贝构造触发场景。
19. 移动构造触发场景。
20. 左值和右值。
21. std::move 做什么。
22. std::forward 做什么。
23. 构造函数能否为虚函数。
24. 析构函数为什么常设为虚函数。
25. 构造/析构中调用虚函数。
26. class 和 struct 区别。
27. 初始化列表作用。
28. 成员初始化顺序。
29. this 指针是什么。
30. 重载、重写、隐藏区别。
31. 多态成立条件。
32. 虚表和虚指针。
33. 纯虚函数和抽象类。
34. 菱形继承和虚继承。
35. 对象切片。
36. dynamic_cast 和 static_cast。
37. unique_ptr、shared_ptr、weak_ptr。
38. make_shared 优缺点。
39. shared_ptr 循环引用。
40. vector 底层和扩容。
41. vector 迭代器失效。
42. list 和 vector。
43. map 和 unordered_map。
44. unordered_map rehash。
45. set 的 key 为什么不能改。
46. emplace_back 和 push_back。
47. reserve 和 resize。
48. lambda 捕获。
49. auto 推导坑。
50. nullptr 和 NULL。
51. enum class。
52. override 和 final。
53. mutex、lock_guard、unique_lock。
54. condition_variable。
55. atomic。
56. future 和 promise。
57. 模板为什么写头文件。
58. 模板特化。
59. 完美转发。
60. C++11 常用特性。

## Linux 50 题

1. 进程和程序区别。
2. 进程地址空间。
3. 进程状态。
4. 僵尸进程。
5. 孤儿进程。
6. fork 返回值。
7. 写时拷贝。
8. exec 作用。
9. wait 和 waitpid。
10. exit 和 _exit。
11. 守护进程。
12. 进程上下文切换。
13. 线程和进程区别。
14. 线程共享资源。
15. 线程私有资源。
16. pthread_create。
17. join 和 detach。
18. 竞态条件。
19. 临界区。
20. 死锁四条件。
21. 预防死锁。
22. mutex 和 spinlock。
23. 读写锁。
24. 条件变量。
25. 虚假唤醒。
26. atomic 和 mutex。
27. 线程池。
28. IPC 方式。
29. 匿名管道。
30. 命名管道。
31. 共享内存。
32. 消息队列。
33. 信号量。
34. 信号。
35. SIGCHLD。
36. SIGPIPE。
37. 文件描述符。
38. open/read/write/close。
39. FILE* 和 fd。
40. dup2。
41. inode。
42. 硬链接和软链接。
43. VFS。
44. 非阻塞 fd。
45. fcntl。
46. 编译链接流程。
47. 静态库和动态库。
48. 头文件重复包含。
49. CMake 作用。
50. 动态库找不到怎么处理。

## 网络 50 题

1. TCP 和 UDP 区别。
2. TCP 可靠性。
3. 三次握手。
4. 为什么不是两次握手。
5. 四次挥手。
6. TIME_WAIT。
7. CLOSE_WAIT。
8. 粘包和半包。
9. 如何解决粘包。
10. 滑动窗口。
11. 拥塞控制。
12. 流量控制。
13. Nagle 算法。
14. TCP keepalive。
15. UDP 可靠性设计。
16. socket 是什么。
17. 服务端 socket 流程。
18. 客户端 socket 流程。
19. bind/listen/accept/connect。
20. recv 返回 0。
21. 阻塞和非阻塞 socket。
22. EAGAIN。
23. SO_REUSEADDR。
24. backlog。
25. 阻塞 IO。
26. 非阻塞 IO。
27. IO 多路复用。
28. select。
29. poll。
30. epoll。
31. LT 和 ET。
32. ET 为什么配非阻塞。
33. 惊群。
34. Reactor。
35. Proactor。
36. HTTP 报文。
37. GET 和 POST。
38. HTTP 状态码。
39. Cookie 和 Session。
40. HTTPS。
41. HTTP keep-alive。
42. chunked transfer。
43. SSE。
44. SSE 和 WebSocket。
45. SSE 的 data 和空行。
46. JSONL 流式协议。
47. HTTP 超时。
48. API 失败重试。
49. 连接断开处理。
50. 高并发瓶颈。

## AI-SDK 项目 80 题

1. 30 秒介绍 AI-SDK。
2. 1 分钟介绍 AI-SDK。
3. 3 分钟介绍 AI-SDK。
4. 项目解决什么问题。
5. 为什么做 SDK。
6. 项目核心功能。
7. 项目技术栈。
8. 项目整体架构。
9. 一次普通请求链路。
10. 一次流式请求链路。
11. ChatSDK 职责。
12. LLMManager 职责。
13. SessionManager 职责。
14. DataManager 职责。
15. LLMProvider 职责。
16. Provider 派生类职责。
17. ChatServer 职责。
18. 前端职责。
19. 为什么分层。
20. 为什么用外观模式。
21. 为什么用多态。
22. 为什么避免 if-else。
23. 如何新增模型。
24. 如何初始化模型。
25. 如何注册模型。
26. 模型名不存在怎么办。
27. Provider 为什么用 unique_ptr。
28. Config 为什么用 shared_ptr。
29. dynamic_pointer_cast 用在哪里。
30. ChatSDK 是否应允许拷贝。
31. SessionManager 怎么生成 ID。
32. atomic 为什么适合 ID。
33. SessionManager 用什么容器。
34. vector<Message> 优缺点。
35. DataManager 为什么用 SQLite。
36. sessions 表设计。
37. messages 表设计。
38. 插入消息流程。
39. 启动恢复流程。
40. 删除会话流程。
41. DataManager 为什么加锁。
42. NoLock 解决什么问题。
43. 嵌套锁死锁怎么产生。
44. 是否需要事务。
45. SQLite 多线程注意点。
46. DeepSeek SSE 格式。
47. GLM 流式格式。
48. Kimi 特殊处理。
49. Ollama JSONL 格式。
50. 半包怎么处理。
51. 粘包怎么处理。
52. `[DONE]` 怎么处理。
53. 空 chunk 怎么处理。
54. JSON 解析失败怎么办。
55. 上游 HTTP 失败怎么办。
56. API key 缺失怎么办。
57. 模型超时怎么办。
58. 客户端断开怎么办。
59. markdown 换行丢失怎么修。
60. `.trim()` 为什么危险。
61. 静态文件 404 怎么排查。
62. model 参数名不一致怎么排查。
63. 模型名拼写错误怎么排查。
64. Provider 注册错怎么排查。
65. 消息 ID 重复怎么排查。
66. 时间戳为 0 怎么排查。
67. 如何做日志。
68. 如何设计错误码。
69. 如何做单元测试。
70. 如何 mock Provider。
71. 如何 mock HTTP。
72. 如何压测并发。
73. 如何监控 QPS。
74. 如何支持多用户。
75. 如何支持鉴权。
76. 如何支持模型 fallback。
77. 如何支持请求取消。
78. 如何支持 RAG。
79. 如何部署到 Linux。
80. 项目最大不足和优化方向。

## 手写 40 题

1. strlen。
2. strcpy。
3. memcpy。
4. memmove。
5. 字符串反转。
6. 回文判断。
7. 反转链表。
8. 删除倒数第 N 个节点。
9. 合并两个有序链表。
10. 判断链表有环。
11. 二叉树层序遍历。
12. 快速排序。
13. 归并排序。
14. TopK。
15. 二分查找。
16. LRU。
17. 单例模式。
18. unique_ptr 极简版。
19. shared_ptr 极简版。
20. 生产者消费者。
21. 线程池接口。
22. atomic ID 生成器。
23. RAII 文件类。
24. RAII 锁类。
25. TCP echo server。
26. TCP client。
27. 设置 fd 非阻塞。
28. select echo server。
29. epoll echo server。
30. SSE buffer 拆包。
31. JSONL buffer 拆包。
32. Provider 抽象基类。
33. 模型注册表。
34. ChatSDK 发送消息伪代码。
35. SQLite 建表语句。
36. SQLite 事务封装。
37. 错误码枚举。
38. 日志宏。
39. request id 生成。
40. HTTP 路由表设计。

