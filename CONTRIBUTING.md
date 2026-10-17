# 参与开发

当前应用使用 Qt Quick、QML 与 C++20。[开发约定](AGENTS.md) 和 [文档索引](docs/README.md) 是共同入口。

1. 从当前 Qt 主线建立任务分支，名称使用 `codex/<主题>`。
2. 按 [架构说明](docs/architecture/overview.md) 找到代码所有者，再修改对应页面、控制器或服务。
3. 按 [构建与验证](docs/development/build-and-test.md) 构建并运行相关测试；测试使用隔离 profile。
4. 提交前运行 `python tools/check-repository.py` 与 `git diff --check`，检查暂存区不含数据、日志或构建产物。
5. PR 说明用户可观察到的变化、验证结果和仍未验证的范围。Git 管理规则见 [仓库工作流](docs/development/repository-workflow.md)。

播放改动需要验证设备选择、暂停/恢复和切歌；队列改动需要验证手动换源及过期请求；账户和存储改动需要验证身份隔离、取消及失败后的数据状态。

隐私与安全问题遵循 [数据边界](docs/security/data-boundaries.md) 和 [安全政策](SECURITY.md)。第三方代码、素材和字体必须保留来源与许可。
