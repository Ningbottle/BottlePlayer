---
title: STL 容器与模板
created: 2026-06-18
updated: 2026-06-18
type: concept
tags: [cpp, stl, template, vector, list, stack, queue]
sources: ["[[sources/cpp-learning-roadmap]]"]
confidence: high
---

# STL 容器与模板

STL（Standard Template Library）是 [[C++ 对象模型]] 之上的泛型编程层。

## 序列容器

### vector
动态数组，连续内存。随机访问 O(1)，尾部插入均摊 O(1)。扩容策略：通常 2x 增长。
对应 [[数据结构与算法]] 中的顺序表。

### list
双向链表，非连续内存。任意位置插入/删除 O(1)，随机访问 O(n)。

### string
特殊化的字符容器。涉及写时拷贝、SSO 等优化策略，详见 [[C++ 对象模型]]。

## 容器适配器

### stack
基于 deque/vector/list 的后进先出适配器。

### queue
基于 deque/list 的先进先出适配器。

## 模板

- 函数模板、类模板
- 模板特化（全特化、偏特化）
- 可变参数模板（C++11）
- SFINAE（Substitution Failure Is Not An Error）

## 迭代器

迭代器是容器和算法之间的桥梁：
- 输入迭代器 → 前向迭代器 → 双向迭代器 → 随机访问迭代器
- begin() / end() 范围

## 与其他概念的关系

- [[C++ 对象模型]] — 模板是类的泛化
- [[数据结构与算法]] — STL 容器是数据结构的标准实现
- [[muduo 网络库]] — 大量使用 STL 容器（vector、map、shared_ptr）
