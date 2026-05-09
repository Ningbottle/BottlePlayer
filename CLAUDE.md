# BottleMusic Claude 指南

Claude 或其他长上下文 agent 接手本项目时，请把本文件当作项目入口。更完整的通用规则见 `AGENTS.md`。

## 一句话目标

BottleMusic 要从现有 Electron/Vue/Node 音乐客户端迁移为 Windows 原生 C++ 客户端，使用 Win32 + Direct2D + DirectWrite 绘制 Melody 风格界面，并把播放中整进程内存控制在 180MB 以内。

## 设计优先级

1. 先保持架构方向正确，再追求功能数量。
2. 先保证内存预算可解释，再扩大缓存和页面。
3. 先实现能验收的纵向切片，再做大面积迁移。
4. 先按用户提供的 Melody 截图设计原生 UI，不沿用旧 Vue 组件形状。

## 沟通和文档

- 默认用中文沟通和写项目文档。
- 代码标识符仍使用英文。
- 重要决策要更新 `docs/` 下的中文设计文档。
- 如果用户要求重新梳理，先停手沟通，不继续写代码。

## 实现前检查

每次动手前回答这几个问题：

- 这次改动属于哪个 Module？
- 调用方应该看到什么 Interface？
- 是否会增加长期内存占用？
- 是否需要 TDD tracer bullet？
- 是否会影响 Melody UI 参考方向？
- 是否只是 Electron 兼容期能力，还是最终客户端能力？

## 技能调用

本项目指定优先使用以下技能方法：

- `improve-codebase-architecture`：架构与模块边界。
- `tdd`：行为测试和渐进实现。
- `design-an-interface`：关键 Interface 设计。
- `to-issues`：计划拆成可执行任务。

详细规则见 `docs/SKILL_USAGE.zh-CN.md`。
