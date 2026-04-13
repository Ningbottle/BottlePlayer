---
tags:
  - moc
  - muduo
  - 网络编程
aliases:
  - muduo库学习入口
  - muduo知识地图
---

# muduo 库学习入口

muduo 是陈硕设计的高性能 C++ 网络库，基于 Reactor 模型。这条主线是 Linux 网络编程知识的工程化落地，直接连接 [[从零开始接入AI-SDK/00-学习入口]] 中的网络层设计。

## 前置知识

在开始之前，确保你已经掌握：

- [[2.Linux/1.【Linux基础】构筑基础概念---进程的概念和状态 (1)]] — 进程和线程基础
- [[2.Linux/14.初步认识线程]] — 线程的概念
- [[2.Linux/16.线程的互斥-锁的概念引出]] — 互斥锁
- [[2.Linux/一文总结网络：]] — TCP/IP 基础

## 模块学习顺序

```mermaid
graph TD
    A["1. 前置知识"] --> B["2. 整体设计"]
    B --> C["3. Buffer 模块"]
    C --> D["4. Socket 类"]
    D --> E["5. Channel 设计"]
    E --> F["EventLoop + Poller（待续）"]
    F --> G["TcpServer 完整框架（待续）"]

    style A fill:#8a8,stroke:#333
    style B fill:#8a8,stroke:#333
    style C fill:#88a,stroke:#333
    style D fill:#88a,stroke:#333
    style E fill:#a88,stroke:#333
    style F fill:#666,stroke:#333,color:#fff
    style G fill:#666,stroke:#333,color:#fff
```

- [[muduo库/1. 了解前置知识：]] — Reactor 模型概述、muduo 的设计哲学
- [[muduo库/2. 开始muduo库的设计：]] — 整体架构搭建、CMake 工程结构
- [[muduo库/3. 设计buffer模块]] — 读写缓冲区的设计与实现
- [[muduo库/4. 准备socket类]] — Socket 封装、地址处理
- [[muduo库/5.   Channel的设计：]] — Channel 抽象、事件分发机制

## 核心架构总览

```mermaid
graph TB
    subgraph Reactor 模型
        EL["EventLoop<br/>事件循环"]
        P["Poller / EPollPoller<br/>IO 多路复用"]
        CH["Channel<br/>事件分发"]
        CB["Callback<br/>回调函数"]
    end

    subgraph 网络层
        TCP["TcpServer"]
        CONN["TcpConnection"]
        SOCK["Socket"]
        BUF["Buffer"]
    end

    EL -->|轮询| P
    P -->|返回活跃 Channel| CH
    CH -->|触发| CB
    TCP --> CONN
    CONN --> CH
    CONN --> BUF
    CONN --> SOCK
```

## 和其他主线的连接

- 系统基础：[[2.Linux/00-学习入口]]
- 项目应用：[[从零开始接入AI-SDK/00-学习入口]]
- 面试表达：[[7天面试备战计划/00-学习入口]]
- 语言基础：[[1.c++学习/00-学习入口]]
