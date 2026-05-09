# BottleMusic Agent Skill Policy

## 默认规则

skills 是工作流程，不是项目事实。项目事实以 `docs/`、`AGENTS.md` 和用户最新指令为准。

## 必用场景

使用 `improve-codebase-architecture`：

- 重新划分模块。
- 发现依赖方向混乱。
- 设计深 Module。
- 解释为什么一个 Interface 值得存在。

使用 `tdd`：

- 新增可观察功能。
- 修 bug。
- 迁移后端接口。
- 实现缓存和内存预算。

使用 `design-an-interface`：

- 设计 `IBackendFacade`、`IPlaybackController`、`IImageLoader` 等关键 Interface。
- 用户要求比较方案。
- 当前 Interface 可能锁死后续架构。

使用 `to-issues`：

- 用户要求拆任务。
- 计划过大，需要可执行切片。
- 准备让多个 agent 或多轮对话协作。

## 禁止行为

- 不要因为 skill 建议就绕过用户确认。
- 不要把 skill 输出当作已批准实现计划。
- 不要在用户说“先不动手”时修改 C++ 代码。
- 不要让 skill 结论停留在聊天里，重要结论必须写入文档。
