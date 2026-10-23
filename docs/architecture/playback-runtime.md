# 播放链路与排错

`MusicController` 拥有队列、当前曲目、循环方式、音质和播放意图。取链由 `NativeGateway` 调用 `/song/url`，结果通过当前播放代次校验后交给 `QMediaPlayer`。媒体加载有超时，失败后再次播放应重新取链；已选音质在媒体加载成功后才提交。

未选择歌曲时，播放器栏和侧面聆听面板允许预选标准、高品质、无损、Hi-Res 或臻品母带，并立即保存、显示默认音质。词表统一为 `128`/`320`/`flac`/`high`/`master`，旧的 `hires` 偏好按 `high` 读取。选择歌曲后，候选来自 `/privilege/lite` 的 `relate_goods`；未知或请求失败时回落到完整音质表。换曲即清空旧候选，本地文件不提供音质切换，清空队列后恢复完整选项。

`/song/url` 的 `available_qualities` 描述本次下发结果，不驱动选择菜单。v5 的音质标签取自容器名与归一化码率，只有在没有这些证据时才使用请求值；URL 中的 `_qu` 不作为覆盖码率或容器的证据。`delivery`/`is_preview` 描述是否为试听。v6 只提供加密地址 `en_tracker_url` 时返回 `native_song_url_v6_no_url` 并回落到 v5；候选音质不代表当前账号具有播放权益。

音频由 `EqualizerController` 与 `PcmOutput` 处理输出及均衡器。选择设备后要核对实际 PCM 路由；设备暂时消失时保留用户选择，恢复后再绑定。测试既要观察媒体时钟，也要确认目标设备收到音频且其他设备没有错误输出。

红心推荐先消费已有队列，接近尾部才拉取并追加去重。准备阶段可退出；手动选曲、换列表和账户变化会使旧推荐结果失效。请求失败保留已有播放。幕布和封面墙是外观选择，不能改变队列所有权。

歌词使用真实媒体位置；用户拖动时暂停自动跟随，回到当前行后恢复。F11 或页面按钮进入窗口内沉浸歌词，Esc 恢复之前页面，窗口几何不随此操作改变。

## 排查顺序

1. 核对可执行文件、版本、profile 和运行库，确保排查的是同一构建。
2. 在 profile 的 `native/requests-diagnostics.jsonl` 找请求 ID，核对开始、完成、取消与超时。
3. 对照 `playback-diagnostics.jsonl` 的取链、加载、播放及错误事件。
4. 对照 `audio-diagnostics.jsonl` 的设备与 PCM 状态，在耳机连接、拔出和重连场景实测。

每类诊断保留两份、每份约 512 KiB。分享前按 [数据边界](../security/data-boundaries.md) 脱敏。回归入口包括 `qt_music_flows`、`qt_audio_runtime` 及 `bottle_qt_smoke` 的 `user-fixes` 模式；音质菜单与切换由 `bottle_qt_smoke` 的 `account` 模式覆盖，需要 `BOTTLE_SMOKE_AUDIO` 指向可播放 wav。

Qt 诊断由单一后台 writer 串行写入。调用线程只生成记录并进入有界队列，不等待目录创建、轮转或写盘；最多排队 512 条，其中 64 条为错误/失败/超时等事件预留。单条记录上限 16KiB（含内部统计字段），超量丢弃和写失败通过统计及下一条成功记录中的 `diagnosticDropped`、`diagnosticCriticalDropped`、`diagnosticWriteFailures` 可见。文件短写会尝试回滚，下一次写入会清理未完成的末行。

`flushDiagnostics()` 只用于明确的测试和销毁屏障；网关销毁前排空已接收记录，应用在所有生产者销毁后调用 `shutdownDiagnostics()`。正常退出保留队列，强制终止可能丢失尚未写出的记录。此机制不替代 Native 的崩溃取证。
