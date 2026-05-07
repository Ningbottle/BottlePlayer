---
title: 数据结构与算法
created: 2026-06-18
updated: 2026-06-18
type: concept
tags: [data-structure, algorithm, sort, stack, queue, list, tree, heap, learning-path]
sources: ["[[sources/cpp-learning-roadmap]]"]
confidence: high
---

# 数据结构与算法

数据结构与算法是编程能力的核心基础，在 [[C++ 学习路径]] 中承上启下。

## 线性结构

### 顺序表（数组）
连续内存存储，O(1) 随机访问，O(n) 插入删除。C 语言实现用 malloc + 指针运算。C++ 中对应 `std::vector`，见 [[STL 容器与模板]]。

### 栈（Stack）
后进先出（LIFO）。应用：函数调用栈、表达式求值、括号匹配。

### 队列（Queue）
先进先出（FIFO）。应用：BFS、任务调度。C++ 中有 `std::queue`（容器适配器），见 [[STL 容器与模板]]。

### 链表
非连续存储，O(1) 插入删除，O(n) 查找。是理解 [[muduo 网络库]] 中 Buffer 模块的基础。

## 树形结构

### 堆（Heap）
完全二叉树，用于优先队列。建堆 O(n)，插入/删除 O(log n)。排序算法 [[排序算法]] 中的堆排序依赖此结构。

### 二叉树
- 遍历：前序、中序、后序、层序
- 二叉搜索树（BST）
- 平衡树（AVL、红黑树）— 进阶内容

## 排序算法

详见 [[排序算法]]：
- O(n²): 冒泡、选择、插入
- O(n log n): 快排、归并、堆排
- O(n): 计数、基数、桶排（特殊场景）

## 与其他概念的关系

- [[C 语言基础]] — 用 C 实现数据结构（指针 + malloc）
- [[C++ 对象模型]] — 用类封装数据结构
- [[STL 容器与模板]] — 标准库中的数据结构实现
- [[muduo 网络库]] — Buffer 基于链表/向量，EventLoop 基于队列
