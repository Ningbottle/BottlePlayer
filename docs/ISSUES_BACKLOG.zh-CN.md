# BottleMusic 任务切片池

本文是初始任务池，不是最终排期。正式开工前应按用户优先级重新确认。

## 1. 项目护栏文档落地

Type：AFK

Blocked by：None

What to build：

建立 BottleMusic 的中文项目文档、agent 指南、模块结构、技术栈、内存预算和 skill 使用规则。

Acceptance criteria：

- [x] 根目录存在 `AGENTS.md` 和 `CLAUDE.md`。
- [x] `docs/` 下存在技术栈、架构、模块、UI、内存、TDD、skill 文档。
- [x] 文档明确 BottleMusic 名称和 Melody 参考界面方向。

## 2. 新模块目录骨架

Type：AFK

Blocked by：1

What to build：

把 `native/` C++ 工程调整为 BottleMusic 模块结构，新增 `EchoImage`、`EchoAsync`、`EchoDiagnostics` 目标。

Acceptance criteria：

- [x] CMake target 与模块结构一致。
- [x] 空实现能编译。
- [x] 旧 `EchoCompatServer` 仍可作为开发期工具构建。

## 3. EchoDiagnostics 内存快照

Type：AFK

Blocked by：2

What to build：

实现一个可从测试和运行时调用的 memory snapshot，输出 Working Set、Private Bytes、图片缓存大小、任务队列长度和播放状态摘要。

Acceptance criteria：

- [x] 有 public Interface。
- [x] 有行为测试。
- [x] 运行时可打印快照。

## 4. EchoAsync 任务和事件队列

Type：AFK

Blocked by：2

What to build：

实现后台任务调度、取消、UI 事件投递基础设施。

Acceptance criteria：

- [x] 后台任务完成后可投递事件。
- [x] cancellation 可阻止无用回调。
- [x] 关闭时不泄漏任务。

## 5. EchoImage 图片加载和 LRU

Type：AFK

Blocked by：3, 4

What to build：

实现 WIC 图片解码、磁盘缓存索引、内存 LRU 和取消不可见请求。

Acceptance criteria：

- [x] 图片内存缓存默认不超过 16MB。
- [x] 超预算触发 LRU 淘汰。
- [x] 解码失败返回占位图。

## 6. Melody 首页静态原型

Type：HITL

Blocked by：2, 5

What to build：

用 Win32 + Direct2D + DirectWrite 绘制用户提供参考图中的首页，先使用假数据。

Acceptance criteria：

- [x] 左侧导航、顶部搜索、推荐横幅、推荐卡片、最近播放、艺人推荐、底部播放栏存在。
- [ ] 视觉方向接近 Melody 参考图。
- [ ] 截图需用户确认。

## 7. Melody 播放详情静态原型

Type：HITL

Blocked by：2, 5

What to build：

绘制播放详情页，包含封面、黑胶视觉、歌词、播放队列和底部播放栏。

Acceptance criteria：

- [x] 页面结构与参考图一致。
- [x] 歌词当前行高亮。
- [x] 队列列表使用虚拟化基础设施。
- [ ] 截图需用户确认。

## 8. 搜索纵向切片

Type：AFK

Blocked by：3, 4

What to build：

从 `IBackendFacade::SearchSongs` 到搜索结果 UI，完成关键词搜索的端到端路径。

Acceptance criteria：

- [x] 有行为测试。
- [x] 搜索结果能展示歌曲名、歌手、专辑、时长。
- [x] 空结果和网络错误有状态。

## 9. 歌曲 URL 到播放状态切片

Type：AFK

Blocked by：8

What to build：

点击搜索结果歌曲，解析 URL，交给 `EchoPlayback`，并更新底部播放栏状态。

Acceptance criteria：

- [x] 有播放状态机测试。
- [x] URL 解析失败有明确错误。
- [x] 切歌释放旧播放对象。

## 10. 歌词切片

Type：AFK

Blocked by：9

What to build：

获取歌词、解析时间戳，并在播放详情页按进度高亮当前行。

Acceptance criteria：

- [x] 有歌词解析行为测试。
- [x] 播放进度变化能更新当前歌词行。
- [x] 无歌词时显示空状态。

## 11. 大列表内存验证

Type：AFK

Blocked by：6, 7, 8

What to build：

验证搜索结果、歌单歌曲和播放队列的大列表虚拟化，确保滚动不持续增长内存。

Acceptance criteria：

- [ ] 一万个模拟项只创建可见绘制数据。
- [ ] 滚动后 memory snapshot 无持续增长。
- [ ] 图片请求随可见区域取消和复用。

## 12. 窄窗口播放栏与播放页响应式修复

Type：AFK

Blocked by：6, 7

What to build：

修复窗口缩小后底部播放栏按钮挤在一起、播放详情页歌词和队列互相压缩导致不可读的问题。

Acceptance criteria：

- [x] 900x640 窗口下底部播放栏不重叠。
- [x] 900px 宽时进度条保留不少于 220px 可读宽度。
- [x] 1180px 以下隐藏音量条，1120px 以下隐藏随机/循环。
- [x] 播放详情页窄屏隐藏右侧队列，歌词区域宽度不少于 260px。
- [x] 有布局行为测试覆盖紧凑和宽屏两种状态。

## 13. 首页紧凑布局修复

Type：AFK

Blocked by：6, 12

What to build：

修复首页在小窗口下固定坐标导致的区域重叠、图片压缩、内容画进底部播放栏的问题。

Acceptance criteria：

- [x] 首页绘制使用 `CalculateMelodyLayout`，不再使用会产生负宽度的固定坐标。
- [x] 900x640 下首页进入紧凑模式。
- [x] 紧凑模式隐藏最近播放、推荐歌单大面板和艺人推荐。
- [x] 首页内容被裁剪在内容区内，不会覆盖底部播放栏。
- [x] 推荐卡片按可用宽度和高度减少数量，不把图片压成窄条。

## 14. 极窄高度下图片和侧边栏裁剪修复

Type：AFK

Blocked by：13

What to build：

修复窗口继续缩小时，侧边栏列表穿到底部播放器下面、推荐卡片图片被压成横条的问题。

Acceptance criteria：

- [x] 侧边栏绘制边界不超过 `playerBar.top`。
- [x] 900x640 下推荐卡片区隐藏，避免真实图片被压扁。
- [x] 1280x720 下推荐卡片区可显示，且区域高度不少于 156px。
- [x] 行为测试覆盖 900x640 和 1280x720 两个边界。

## 15. DPI、字体清晰度和底栏完整性修复

Type：AFK

Blocked by：12, 14

What to build：

修复窗口被系统缩放导致的文字发糊、外窗口最小值小于客户区最小值导致的底部播放器裁剪，以及播放按钮依赖字体符号导致的廉价感。

Acceptance criteria：

- [x] 进程启用 Per-Monitor DPI Aware V2，避免系统位图缩放。
- [x] 最小窗口尺寸按 900x640 客户区换算外框尺寸。
- [x] 布局函数尊重真实客户区尺寸，不把 884x601 强行当作 900x640 绘制。
- [x] 底部播放栏播放、暂停、上一首、下一首使用 Direct2D 矢量绘制。
- [x] 行为测试覆盖真实小客户区边界。

## 16. 功能入口恢复和卡片比例修复

Type：AFK

Blocked by：15

What to build：

修复桌面宽度下因高度阈值导致最近播放等功能区消失、歌词入口不可达、首页卡片和横幅视觉被过度拉伸的问题。

Acceptance criteria：

- [x] 1280x720 下不进入极简首页，保留最近播放入口。
- [x] 推荐卡片使用最大宽度和固定比例，不随超宽窗口无限拉伸。
- [x] 底部“词”按钮点击可进入播放/歌词页。
- [x] 底部队列按钮点击可进入播放页。
- [x] 初始窗口自动限制在系统工作区内，避免播放器落到任务栏下面。
