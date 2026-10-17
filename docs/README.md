# BottleMusic 开发文档

这里只维护当前 Qt / C++ / QML 应用。先按问题选择主题，再核对代码和测试。

| 主题 | 入口 |
| --- | --- |
| 分层和代码归属 | [总体架构](architecture/overview.md) |
| 服务、请求策略和 C ABI | [原生后端](architecture/native-backend.md) |
| 播放、队列、输出设备和排错 | [播放链路](architecture/playback-runtime.md) |
| profile、身份和数据导入 | [存储与数据](architecture/storage-and-data.md) |
| 依赖、构建、测试和打包 | [构建与验证](development/build-and-test.md) |
| 分支、提交、PR 和发布 | [仓库工作流](development/repository-workflow.md) |
| 实现状态和待验收项 | [当前状态](development/status.md) |
| 数据所有权和诊断 | [数据边界](security/data-boundaries.md) |

稳定设计在相应主题更新；一次性排查、截图、日志和交接记录只保存在忽略的本地输出目录。根目录 [AGENTS.md](../AGENTS.md) 是开发约定。
