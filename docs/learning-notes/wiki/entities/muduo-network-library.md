---
title: muduo 网络库
created: 2026-06-18
updated: 2026-06-18
type: entity
tags: [muduo, network, reactor, event-loop, linux, cpp]
sources: ["[[sources/cpp-learning-roadmap]]"]
confidence: medium
---

# muduo 网络库

muduo 是陈硕开发的 C++ 网络库，基于 Reactor 模式，是学习 [[Linux 系统编程]] 和网络编程的经典项目。

## 核心架构

### Reactor 模式
- EventLoop：事件循环，每个线程一个
- Channel：文件描述符的事件分发器
- Poller：I/O 多路复用封装（epoll/kqueue）
- TimerQueue：定时器管理

### 网络层
- Acceptor：接受新连接
- TcpConnection：已建立的连接
- TcpServer / TcpClient：服务端/客户端封装
- Buffer：应用层缓冲区

### 线程模型
- one loop per thread
- 线程池处理计算密集型任务
- mutex / condition_variable 同步

## 设计模式

- Reactor（事件驱动）
- Observer（Channel 回调）
- 单例（EventLoop per thread）
- 对象池（Buffer）

## 在学习路径中的位置

```
[[C 语言基础]] → [[C++ 对象模型]] → [[STL 容器与模板]]
                                      ↓
                              muduo 网络库
                              ↓          ↓
                        网络编程     多线程编程
```

## 相关概念

- [[数据结构与算法]] — Buffer 基于 vector，Channel 管理用 map
- [[C++ 对象模型]] — 大量使用继承、虚函数、智能指针
- [[STL 容器与模板]] — 使用 shared_ptr、function、bind 等
- [[Linux 系统编程]] — epoll、socket、线程、信号处理
