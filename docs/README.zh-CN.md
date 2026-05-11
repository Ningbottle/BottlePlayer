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
- `WORKLIST.zh-CN.md`：长期自动推进工作队列、上下文压缩规则和验证命令。

## Agent 辅助文档

- `../AGENTS.md`：通用 agent 项目规则。
- `../CLAUDE.md`：Claude 风格 agent 项目入口。
- `agents/context.zh-CN.md`：项目上下文和领域词汇。
- `agents/issue-tracker.zh-CN.md`：任务管理约定。
- `agents/skill-policy.zh-CN.md`：技能调用策略。
- `agents/ANTIGRAVITY_WORKLIST.zh-CN.md`：分配给 Antigravity 的较复杂任务清单。
- `agents/CLAUDE_CODE_WORKLIST.zh-CN.md`：分配给 Claude Code 的轻量任务清单。

## 当前状态

当前仓库已删除旧 Electron/Vue 前端和旧 Flutter 跨平台桌面工程残留，并以 `native/` C++ 工程作为 BottleMusic 原生客户端实现。`server/` 子模块保留为 KuGouMusicApi 行为参考和未完成接口迁移来源；新的 BottleMusic 设计以本文档为准。
