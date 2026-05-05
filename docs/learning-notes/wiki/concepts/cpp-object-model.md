---
title: C++ 对象模型
created: 2026-06-18
updated: 2026-06-18
type: concept
tags: [cpp, class, inheritance, polymorphism, memory, learning-path]
sources: ["[[sources/cpp-learning-roadmap]]"]
confidence: high
---

# C++ 对象模型

C++ 对象模型是从 [[C 语言基础]] 到面向对象编程的核心跨越。

## 核心内容

### 类和对象
- 类的定义：数据成员 + 成员函数
- this 指针（隐式传递，本质是 C 语言指针的 OOP 应用）
- 构造函数、析构函数、拷贝构造、赋值运算符（Rule of Three/Five）
- 访问控制：public / private / protected

### 继承与多态
- 单继承、多继承、菱形继承
- 虚函数与虚表（vtable / vptr）
- 动态绑定 vs 静态绑定
- 纯虚函数与抽象类

### string 类
标准库 string 是类设计的经典案例，涉及：
- 写时拷贝（COW）vs SSO（小字符串优化）
- 深拷贝 vs 浅拷贝
- 运算符重载

## 在知识体系中的位置

```
[[C 语言基础]] → C++ 对象模型 → [[STL 容器与模板]]
                  ↓
         [[RAII 与智能指针]]
                  ↓
         [[muduo 网络库]]
```

## 相关概念

- [[C 语言基础]] — 前置知识
- [[STL 容器与模板]] — 泛型编程，对象模型的延伸应用
- [[RAII 与智能指针]] — 资源管理，构造/析构的核心应用场景
- [[数据结构与算法]] — 用类实现数据结构（链表、树节点等）
- [[内存对齐与底层优化]] — 对象内存布局的底层细节
