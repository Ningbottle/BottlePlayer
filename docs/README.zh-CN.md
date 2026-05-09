# BottleMusic 文档入口

本目录是 BottleMusic 原生化重设计的中文项目事实来源。后续实现、评审、测试和任务拆分都应优先参考这些文档。

## 文档地图

- `PRODUCT_VISION.zh-CN.md`：产品愿景、目标和非目标。
- `TECH_STACK.zh-CN.md`：技术栈和依赖策略。
- `ARCHITECTURE.zh-CN.md`：整体架构、进程模型、依赖方向。
- `MODULES.zh-CN.md`：每个 Module 的职责、Interface、依赖和测试方式。
- `UI_REFERENCE_MELODY.zh-CN.md`：用户提供 Melody 截图的 UI 规格拆解。
- `MEMORY_BUDGET.zh-CN.md`：内存预算、缓存上限和验收口径。
- `TDD_PLAN.zh-CN.md`：测试驱动开发计划和首批 tracer bullet。
- `SKILL_USAGE.zh-CN.md`：指定 skills 的使用规则。
- `IMPLEMENTATION_RULES.zh-CN.md`：实现约束和禁止事项。
- `ISSUES_BACKLOG.zh-CN.md`：纵向切片任务池。

## Agent 辅助文档

- `../AGENTS.md`：通用 agent 项目规则。
- `../CLAUDE.md`：Claude 风格 agent 项目入口。
- `agents/context.zh-CN.md`：项目上下文和领域词汇。
- `agents/issue-tracker.zh-CN.md`：任务管理约定。
- `agents/skill-policy.zh-CN.md`：技能调用策略。

## 当前状态

当前仓库仍保留旧 Electron/Vue/Node 项目和此前新增的 `native/` C++ 骨架。新的 BottleMusic 设计以本文档为准，后续实现应逐步把已有原生代码调整到新模块结构。

旧文档仍保留在 `native/docs/`，它们记录了早期 EchoMusic Native 迁移状态。若与本目录冲突，以本目录为准。
