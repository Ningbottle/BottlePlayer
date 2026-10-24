# 存储与数据

默认 profile 位于可执行文件旁，可用 `--profile` 指定。Qt 控制器在该目录维护偏好、外观、布局、历史和队列；原生后端使用 profile 下的 `native/` 保存数据库与诊断。

Qt 的 `DataMigration` 与 `StatsArchive` 负责用户选择的档案导入导出。只导入白名单字段并校验类型、大小和数量；恢复队列不自动恢复播放。统计合并只读源文件，先备份目标统计，按稳定标识去重；不复制账号会话和设备凭据。

播放队列与播放偏好仍保存在 `preferences.ini`，队列使用原有 `queueSnapshot` v1 格式。`MusicController` 持有状态，向 `PlaybackStore` 提交只读值快照；单一 worker 在 GUI 线程外转换队列 JSON、创建独立 `QSettings` writer 并原子同步。GUI 中的设置对象只负责读取，音量等偏好也通过该 worker 写入，避免 `QSettings` 自动保存重新阻塞 GUI。
旧音质值 `hires` 在读取和档案导入时归一为 `high`；现有 profile 的规范值也通过播放 writer 保存，沿用原有档案版本。
这遵循 [Qt 6.8 的线程约束](https://doc.qt.io/qt-6.8/qsettings.html#accessing-settings-from-multiple-threads-or-processes-simultaneously)：不同线程使用各自的 `QSettings` 实例，保持默认的原子同步要求。

播放存储最多保留一份执行中的批次和一份合并后的待写批次；同名偏好与队列以最新值为准。失败批次保留未被更新的字段，下一次提交或显式 `flushStorage()` 重试，失败记录只包含事件和状态码。`DataMigration` 的快照准备回调同时等待播放和外观保存；失败则停止导出或导入。导入写入前停止并 join 播放 writer，防止旧快照覆盖导入结果；正常退出与控制器析构也排空写入，不依赖 GUI 事件循环处理完成通知。强制终止仍可能丢失尚未提交的更新。

外观控制器先更新内存状态，250ms 内连续修改合并到一次设置写入。侧栏宽度使用独立通知，避免拖动时重新计算全部外观属性。待写值在合并前不提交给 `QSettings`，以免它自行安排额外写盘。导出和导入备份前通过 `DataMigration` 的快照准备回调同步保存，保存失败则停止操作；正常退出和析构也会提交最后一次修改。

外观每次提交使用新的 `QSettings` writer，当前保存错误独立缓存；临时故障修复后可以重试，不将对象的历史错误状态作为永久失败。保存失败保留待写数据，导入完成后不再回放导入前的延迟写入。

数据库与持久化由原生存储层管理。会话使用 Windows DPAPI 保护。`IdentityCodec` 处理带版本身份，`ProfileWriterLease` 防止同一 profile 多写者，`SessionRepository` 和 `DeviceRepository` 只接受仍有效的条件提交。

测试必须在临时目录建立独立 profile，不可使用日常数据库。遇到数据问题先复制并校验备份，再在副本复现。不要把数据库、账号档案或未脱敏导出放入 Git。
