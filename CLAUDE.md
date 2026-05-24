# BottleMusic Claude 指南

Claude 或其他长上下文 agent 接手本项目时，请把本文件当作项目入口。完整规则见 `AGENTS.md`，设计基线见 `docs/REFERENCE.zh-CN.md`，执行队列与验证命令见 `docs/WORKLIST.zh-CN.md`。

## 一句话目标

BottleMusic 使用 **Tauri 2.0 + Vue 3 + Vanilla CSS** 做前端，**C++ EchoCompatServer**（`127.0.0.1:6609`）做后端 sidecar，实现 Newsprint 报纸风格的音乐客户端，播放中整进程内存控制在 220 MB 以内。

## 设计优先级

1. 先保持前后端接口契约正确，再追求页面功能数量。
2. 先保证 EchoCompatServer 接口稳定，再扩大 Vue 组件覆盖。
3. 先实现能端到端验收的纵向切片，再做大面积页面迁移。
4. 先按 `Music Player.html` Newsprint 参考设计 Vue 组件，不沿用旧 Vue/Melody 组件形状。

## 沟通和文档

- 默认用中文沟通和写项目文档。
- 代码标识符仍使用英文。
- 重要决策更新 `docs/REFERENCE.zh-CN.md` 与 `docs/WORKLIST.zh-CN.md`。
- 如果用户要求重新梳理，先停手沟通，不继续写代码。

## 实现前检查

每次动手前回答这几个问题：

- 这次改动属于哪个 Module？
- 调用方应该看到什么 Interface？
- 是否会增加长期内存占用？
- 是否需要 TDD tracer bullet？
- 是否会影响 Newsprint UI 参考方向（`Music Player.html`）？
- 是否只是 EchoCompatServer 开发期能力，还是最终 sidecar 能力？

## 技能调用

本项目优先使用：

- `improve-codebase-architecture`：架构与模块边界。
- `tdd`：行为测试和渐进实现。
- `design-an-interface`：关键 Interface 设计。
- `to-issues`：计划拆成可执行任务。

详细 skill 调用规则见 `AGENTS.md` 第 7 节。
