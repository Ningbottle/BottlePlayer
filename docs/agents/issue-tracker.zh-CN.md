# BottleMusic 任务管理约定

当前项目未绑定远程 issue tracker。现阶段使用 `docs/ISSUES_BACKLOG.zh-CN.md` 作为本地任务池。

## 任务类型

- HITL：需要用户参与确认，例如视觉截图、交互取舍、架构决策。
- AFK：agent 可以独立完成并验证。

## 任务切片原则

- 优先纵向切片。
- 每个任务要能独立验收。
- 每个任务要说明依赖。
- 不把“大模块搭完”当成一个任务。

## 发布到真实 issue tracker 前

如果后续接入 GitHub Issues 或其他 tracker，应先确认：

- 仓库位置。
- label 体系。
- milestone。
- 是否允许 agent 创建 issue。
- 是否需要中英文双语。

## 默认标签建议

- `needs-triage`
- `architecture`
- `ui`
- `backend`
- `playback`
- `memory`
- `test`
- `hitl`
- `afk`
