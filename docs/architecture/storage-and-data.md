# 存储与数据

默认 profile 位于可执行文件旁，可用 `--profile` 指定。Qt 控制器在该目录维护偏好、外观、布局、历史和队列；原生后端使用 profile 下的 `native/` 保存数据库与诊断。

Qt 的 `DataMigration` 与 `StatsArchive` 负责用户选择的档案导入导出。只导入白名单字段并校验类型、大小和数量；恢复队列不自动恢复播放。统计合并只读源文件，先备份目标统计，按稳定标识去重；不复制账号会话和设备凭据。

数据库与持久化由原生存储层管理。会话使用 Windows DPAPI 保护。`IdentityCodec` 处理带版本身份，`ProfileWriterLease` 防止同一 profile 多写者，`SessionRepository` 和 `DeviceRepository` 只接受仍有效的条件提交。

测试必须在临时目录建立独立 profile，不可使用日常数据库。遇到数据问题先复制并校验备份，再在副本复现。不要把数据库、账号档案或未脱敏导出放入 Git。
