# BottleMusic 内存预算

## 验收口径

用户口径：

- Windows 任务管理器中 BottleMusic 进程的内存占用。
- 播放中目标：180MB 以内。

开发口径：

- Working Set。
- Private Bytes。
- 图片内存缓存大小。
- API cache 内存驻留大小。
- 播放管线缓冲状态。
- Direct2D / DirectWrite / WIC 资源数量。

`EchoDiagnostics` 应提供统一 memory snapshot，便于每个阶段记录。

## 总预算

建议目标：

- 冷启动后首页空闲：120MB 以内。
- 播放中：180MB 以内。
- 连续播放 4 小时：内存增长不超过 20MB。
- 连续切歌 100 次：播放对象无持续增长。
- 大歌单滚动 10 分钟：列表对象无持续增长。

## 模块预算

`EchoCore`：

- 空闲 20-35MB。
- 不长期保留完整 JSON 响应树。
- 分页数据按页面和 TTL 缓存。

`EchoStorage`：

- 空闲 5-15MB。
- SQLite page cache 受控。
- 查询结果使用后释放。

`EchoPlayback`：

- 空闲 5-15MB。
- 播放中 20-50MB。
- 不做整曲解码缓存。

`EchoWin32`：

- 空闲 40-70MB。
- 只保存当前页面 ViewModel 和可见区域布局数据。
- DirectWrite 文本布局对象按页面缓存，不为所有历史行长期持有。

`EchoImage`：

- 内存 LRU 默认 16MB。
- 首版上限 32MB。
- 磁盘缓存按总大小和过期时间淘汰。
- 不为不可见列表项保留已解码大图。

`EchoAsync`：

- 线程池规模受控。
- 后台任务队列有上限。
- 页面切换要取消不可见任务。

`EchoDiagnostics`：

- 日志缓冲有上限。
- trace 默认轻量。
- memory snapshot 不持有大对象引用。

`EchoCompatServer`：

- 仅开发期启用。
- 启用时单独记录内存，不计入正式客户端默认预算。

## 缓存策略

图片：

- 磁盘缓存保存原始图片或缩略图。
- 内存 LRU 保存可直接绘制的缩略图。
- 大封面离屏后应可淘汰。

搜索：

- 只缓存最近关键词和当前页。
- 不把所有搜索历史驻留内存。

歌单：

- 缓存分页和索引。
- 不一次性把大歌单全部转成 UI 行对象。

歌词：

- 当前歌曲歌词常驻。
- 最近少量歌词可缓存。
- KRC 或逐字歌词解析结果应有大小上限。

播放队列：

- 只保存队列必要字段。
- 不在队列对象中嵌入封面位图或完整专辑信息。

## 每次功能验收必须记录

- 功能前 memory snapshot。
- 功能后 memory snapshot。
- 新增缓存类型和上限。
- 是否增加长期驻留对象。
- 是否存在取消和释放路径。

## 当前实测记录

2026-05-11，`EchoWin32.exe`，隐藏窗口启动 3 秒，未播放音频，未进行真实网络图片加载：

- Debug：Working Set 约 19.9MB，Private Bytes 约 6.6MB。
- Release：Working Set 约 19.0MB，Private Bytes 约 6.6MB。
- 该记录只代表空闲启动口径，不代表播放中、长时间播放或大量图片加载口径。

2026-05-10，Debug 构建，`native/out/bottlemusic-check/EchoWin32.exe`，隐藏窗口启动 3 秒：

- List 04 完成后：Working Set 约 18.7MB，Private Bytes 约 6.5MB。
- List 05 完成后：Working Set 约 18.7MB，Private Bytes 约 6.5MB。
- List 06 启用 SQLite 和设置持久化后：Working Set 约 19.0MB，Private Bytes 约 6.7MB。

List 07 追加的回归口径：

- 10,000 项列表滚动只计算可见行范围，单次可见行数量保持在 14 行以内。
- 10,000 次封面写入后，`MemoryImageCache` 仍按 byte budget 淘汰，不随历史写入无限增长。
- 图片加载支持取消 token，取消后不进入内存缓存。
- `EchoDiagnostics::MemorySnapshotProvider` 记录 Working Set、Private Bytes、图片缓存字节数、待执行任务数和播放状态。

A3（2026-05-11）追加的回归口径和实测数据：

搜索/队列 ViewModel 绘制切片：

- `CalculateVisibleRows`：对 10,000 项列表做 1,001 步等间距滚动模拟，每步可见行数量 ≤14，`lastExclusive` 不超过总行数。经 `EchoNativeSmokeTests` 自动验证。
- `SearchViewModel`：10,000 条搜索结果的 JSON 构建 ViewModel，数据层保存全部 10,000 行；`CalculateVisibleRows` 在 200 步滚动模拟中每步绘制切片 ≤14 行。经自动验证。
- `PlaybackQueueState`：10,000 条队列项在 200 步滚动模拟中每步绘制切片 ≤12 行。经自动验证。

图片缓存 LRU 淘汰：

- 紧缩预算（64 KB）下写入 10,000 条 256B 封面（共约 2.5MB >> 64KB），LRU 淘汰后 `byteCount` ≤ 64KB，`itemCount` < 10,000 且 ≤ 理论上限（256 + 1 项）。最近写入的 `cover:9999` 仍可命中缓存。经自动验证。
- 默认预算（16MB）下同样写入 10,000 条，`byteCount` ≤ `byteBudget`，最近写入项可命中。经自动验证。

EchoWin32 进程启动内存（2026-05-11，Debug 构建，隐藏窗口启动 3 秒，未播放音频）：

- Working Set：19.9 MB，Private Bytes：6.6 MB。
- 离空闲目标（≤120 MB）有大量余量；播放中目标（≤180 MB）待播放管线接入真实 Media Foundation 后补充实测。
