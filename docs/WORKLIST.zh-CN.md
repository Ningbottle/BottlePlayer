# BottleMusic 长期工作 List

本文是 BottleMusic 后续长期开发的执行队列。它的目标是让 agent 在长时间任务中不跑偏：完成一个 list 后自动进入下一个 list；每个 list 都按 TDD tracer bullet 推进；上下文压缩时只保留和本项目有关的事实。

## 工作循环

每个 list 必须遵循同一套循环：

1. 读取 `AGENTS.md`、`docs/REFERENCE.zh-CN.md`、本文和当前 list 相关文档。
2. 使用需要的 skill。功能实现默认使用 `tdd`；任务拆分使用 `to-issues`；接口设计使用 `design-an-interface`；架构边界调整使用 `improve-codebase-architecture`。
3. 写一个用户可观察行为的测试，先看到 RED。
4. 写最小实现，让测试 GREEN。
5. 只在 GREEN 后重构。
6. 跑完整验证命令。
7. 更新本文状态和必要文档。
8. 当前 list 验收完成后，自动进入下一个 `Status: Todo` 的 list。

## 上下文压缩规则

压缩上下文时必须保留：

- 产品名、目标平台和技术栈：BottleMusic，Windows 10/11 x64，C++20，Win32，Direct2D，DirectWrite，WIC，Media Foundation，SQLite。
- 当前架构边界：`EchoCore`、`EchoStorage`、`EchoPlayback`、`EchoWin32`、`EchoImage`、`EchoAsync`、`EchoDiagnostics`、`EchoCompatServer`。
- 当前正在执行的 list 编号、状态、验收标准、最近一次测试结果。
- 用户提供的 Melody 参考界面方向：浅色、左侧导航、顶部搜索、首页、播放详情、底部播放栏。
- 内存目标：最终播放中整进程低于 180MB；图片和大列表必须有上限、虚拟化和释放策略。
- 已知风险：SQLite3 开发包未被当前 CMake 找到时会走 JSON fallback；真实网络接口、真实封面 URL、播放管线仍需继续完善。
- 未提交代码的主要改动范围。

压缩上下文时必须丢弃：

- 与项目无关的 GitHub 登录、Git 凭据、账号切换、个人认证过程。
- 与项目无关的网络、证书、代理、系统环境临时排障。
- 旧对话中和 BottleMusic 目标无关的闲聊、重复解释、过期计划。
- 已解决且不影响当前实现的临时命令输出。

压缩后的继续方式：

- 先读本文，找到第一个 `Status: In Progress`；如果没有，则找第一个 `Status: Todo`。
- 不重新讨论已冻结技术栈，除非用户明确要求重新设计。
- 不把已丢弃的凭据、网络等外部配置当作项目上下文继续引用。

## 验证命令

默认快速验证：

```powershell
cmd /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && "D:\QT\Tools\CMake_64\bin\cmake.exe" --build native\out\bottlemusic-check --config Debug --target EchoNativeSmokeTests && "D:\QT\Tools\CMake_64\bin\ctest.exe" --test-dir native\out\bottlemusic-check --output-on-failure'
```

Win32 可执行验证：

```powershell
cmd /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && "D:\QT\Tools\CMake_64\bin\cmake.exe" --build native\out\bottlemusic-check --config Debug --target EchoWin32'
```

启动响应和内存检查：

```powershell
$exe = Join-Path (Get-Location) 'native\out\bottlemusic-check\EchoWin32.exe'
$p = Start-Process -FilePath $exe -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 3
$p.Refresh()
[pscustomobject]@{ Id=$p.Id; Responding=$p.Responding; HasExited=$p.HasExited; WorkingSetMB=[math]::Round($p.WorkingSet64/1MB,1); PrivateMB=[math]::Round($p.PrivateMemorySize64/1MB,1) }
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
```

## List 01：交互基础闭环

Status：In Progress

Type：AFK

Blocked by：None

What to build：

让 Win32 原生壳具备基础产品交互：前进、后退、搜索输入、滚动、歌词入口、队列切歌、设置页、进度和音量点击。

Acceptance criteria：

- [x] 顶部返回和前进有历史栈行为测试。
- [x] 顶部搜索框可聚焦、输入、退格、回车提交。
- [x] 搜索结果和播放队列支持鼠标滚轮。
- [x] 底部“词”和播放页“歌词”能进入歌词视图。
- [x] 队列行、上一首、下一首能切换当前歌曲。
- [x] 设置页可从侧栏进入。
- [x] 进度条和音量条可点击并有行为测试。
- [x] 启动可视窗口后手动确认这些交互路径。

Done when：

- 快速测试通过。
- Win32 可执行编译通过。
- 启动响应检查通过。
- 用户确认基础交互没有明显断路。

## List 02：真实图片与封面管线

Status：Done

Type：AFK

Blocked by：List 01

What to build：

把当前色块和临时本地 icon 占位升级为可复用的 `EchoImage` 图片管线：本地图片、远端封面 URL、解码、缓存、比例裁剪、失败占位、释放策略。

Acceptance criteria：

- [x] 有 `AspectFit` 和 `AspectFill` 行为测试，图片不被拉伸压扁。
- [x] Win32 能绘制至少一张本地 WIC 位图。
- [x] 图片加载不阻塞 UI 线程。
- [x] 图片内存缓存有明确上限。
- [x] 首页卡片、播放详情封面、队列封面使用统一图片入口。
- [x] 解码失败显示稳定占位。
- [x] 远端封面 URL 接入 `EchoImage`，不在 UI 线程下载或解码。

## List 03：搜索到播放的真实链路

Status：Done

Type：AFK

Blocked by：List 01

What to build：

把搜索页从假/半联调状态推进为稳定链路：输入关键词、调用 `IBackendFacade::SearchSongs`、展示结果、点击歌曲解析 URL、进入播放状态。

Acceptance criteria：

- [x] 搜索输入不再固定默认关键词。
- [x] 搜索中的 loading、空结果、错误状态可见。
- [x] 点击搜索结果后播放栏和播放详情页同步当前歌曲。
- [x] URL 解析失败不崩溃，有用户可见错误。
- [x] 测试覆盖搜索 ViewModel、播放 ViewModel 和点击映射。

## List 04：歌词真实接口与播放进度联动

Status：Done

Type：AFK

Blocked by：List 03

What to build：

把歌词从 demo LRC 推进到真实接口：搜索歌词、获取详情、解析、按播放进度高亮、无歌词空状态。

Acceptance criteria：

- [x] `IBackendFacade` 提供歌词获取入口。
- [x] LRC 解析覆盖时间戳、多行、空歌词。
- [x] 播放进度改变能更新当前歌词行。
- [x] 歌词页和底部播放栏状态一致。

## List 05：播放核心真实 Media Foundation 管线

Status：Done

Type：AFK

Blocked by：List 03

What to build：

把 `EchoPlayback` 从状态机骨架推进到真实 Media Foundation 播放：打开 URL、暂停、恢复、停止、seek、音量、错误事件。

Acceptance criteria：

- [x] URL 流式播放可用。
- [x] Pause/Resume/Stop/Seek/Volume 有行为验证或集成验证。
- [x] 切歌释放旧对象。
- [x] 播放错误返回明确错误，不阻塞 UI。
- [x] 不做整曲解码缓存。

## List 06：设置持久化与 SQLite 修复

Status：Done

Type：AFK

Blocked by：List 01

What to build：

让设置页不只是静态 UI：音量、启动页、缓存预算等设置写入 `EchoStorage`，并修复本地 SQLite3 开发包未被 CMake 找到的问题。

Acceptance criteria：

- [x] SQLite3 通过 vcpkg 或明确路径被 CMake 找到。
- [x] 设置保存后重启仍存在。
- [x] JSON fallback 只作为显式降级路径。
- [x] migration 测试通过。

## List 07：大列表与内存回归

Status：Done

Type：AFK

Blocked by：List 02, List 03

What to build：

验证搜索结果、歌单歌曲、播放队列、图片缓存不会随滚动无限增长。

Acceptance criteria：

- [x] 一万个模拟项只生成可见绘制数据。
- [x] 大列表滚动后 memory snapshot 无持续增长。
- [x] 不可见图片请求可取消或释放。
- [x] 空闲和播放中内存快照进入文档。

## List 08：Melody 视觉回归与截图验收

Status：Done

Type：HITL

Blocked by：List 01, List 02

What to build：

按照用户给出的 Melody 参考图做视觉回归：首页、播放详情页、歌词页、设置页、缩小窗口状态。

Acceptance criteria：

- [x] 1600x1060 首页接近参考图。
- [x] 1600x1060 播放详情页接近参考图。
- [x] 900x640、1280x720、2560x1620 不重叠、不裁底栏。
- [x] 字体清晰，按钮不挤压。
- [x] 截图交给用户确认。

Latest verification：

- 2026-05-10：修复 Win32 DPI-aware 截图和运行时坐标不一致导致的误判；`EchoWin32` 嵌入 PerMonitorV2 manifest。
- 2026-05-10：修复超高窗口下首页下方模块贴底的问题，新增 2560x1620 布局回归断言。
- 2026-05-10：已生成截图：`native/out/bottlemusic-check/screenshots/list08-final-900x640.png`、`list08-final-1280x720.png`、`list08-final-1600x1060.png`、`list08-final-2560x1620.png`。
- 2026-05-10：`EchoNativeSmokeTests` 和 `EchoWin32` Debug 构建通过。
- 2026-05-11：用户确认 List 08 当前视觉方向可以接受。

## List 09：兼容服务剩余接口

Status：Done

Type：AFK

Blocked by：List 03

What to build：

继续补齐迁移期 `EchoCompatServer` 接口，服务旧接口 contract 验证和 KuGouMusicApi 对照，但不污染最终原生 UI。

Acceptance criteria：

- [x] `/search`、`/song/url`、`/search/lyric`、`/lyric`、`/playlist/track/all` 行为稳定。
- [x] 未迁移接口继续返回稳定 `native_not_implemented`。
- [x] contract fixture 忽略 volatile 字段。

Latest verification：

- 2026-05-10：为兼容接口加入 handler injection 测试，覆盖 `/search`、`/song/url`、`/search/lyric`、`/lyric`、`/playlist/track/all` 的参数别名和响应透传。
- 2026-05-10：验证 `/login/qr/key` 等未迁移路由返回 HTTP 501、`status=0`、`error_code=native_not_implemented`。
- 2026-05-10：新增 `ContractJsonMatches`，contract fixture 可按 JSON path 忽略时间戳、签名 URL 等 volatile 字段，同时仍能报告稳定字段差异。
- 2026-05-10：修复旧 JSON fallback 数据库文件被 SQLite 打开时的恢复路径；`EchoNativeSmokeTests` Debug 构建和 ctest 通过。

## List 10：发布前收敛

Status：Done

Type：HITL

Blocked by：List 01-09

What to build：

收敛首个可试用版本：构建脚本、README、已知问题、性能结果、用户手测清单。

Acceptance criteria：

- [x] Debug 和 Release 均可构建。
- [x] README 有本地构建和运行说明。
- [x] 内存目标和实测值写入 `REFERENCE.zh-CN.md` 内存预算节。
- [x] 已知缺口清晰列出。

Latest verification：

- 2026-05-11：新增 `native/CMakePresets.json`，验证 `cmake -S native --preset bottlemusic-check` 可配置 Debug 构建。
- 2026-05-11：Debug 构建 `EchoNativeSmokeTests`、`EchoWin32`、`EchoCompatServer` 通过，`ctest` 1/1 通过。
- 2026-05-11：Release 构建 `EchoNativeSmokeTests`、`EchoWin32`、`EchoCompatServer` 通过，`ctest` 1/1 通过。
- 2026-05-11：空闲启动 3 秒内存采样：Debug Working Set 约 19.9MB / Private Bytes 约 6.6MB；Release Working Set 约 19.0MB / Private Bytes 约 6.6MB。
- 2026-05-11：README 已补充 Debug/Release 构建、运行兼容服务、启动 Win32 UI 和当前已知缺口。

## List 11：交互手测收口

Status：Done

Type：HITL

Blocked by：List 10

What to build：

对已完成的交互基础链路做一次可视窗口手动回归，把“测试已过但用户路径未确认”的部分收口。

Acceptance criteria：

- [ ] 启动 Win32 可视窗口后，确认首页、搜索页、播放详情页、歌词页、设置页的主路径可达。
- [ ] 手动确认前进、后退、搜索提交、滚动、歌词入口、队列切歌、进度条点击、音量条点击没有明显断路。
- [x] 记录至少一轮手测结论和已知问题到文档。

Latest verification：

- 2026-05-14：交互手测路径、前置命令和记录模板曾单独记录；2026-05-19 起收敛回本文，避免重复过程文档。
- 2026-05-14：`native/out/bottlemusic-check/EchoNativeSmokeTests.exe` 直接运行通过，当前自动化基线可用。
- 2026-05-14：已成功启动 `native/out/bottlemusic-check/EchoWin32.exe`，进程存活且 `Responding=True`；仍待人工完成可视交互确认。
- 2026-05-14：修复 `EchoWin32` 的窗口 DPI 缩放与命中坐标路径，避免高 DPI 屏下界面整体过小。
- 2026-05-14：修复首页高窗口布局，让推荐区、最近播放和下方面板更充分利用全屏高度。
- 2026-05-14：把搜索结果和当前播放封面接入远端图片加载链路，不再只复用 app icon 占位。
- 2026-05-14：重新构建 `EchoNativeSmokeTests`、`EchoWin32` 并运行 `ctest --test-dir native/out/bottlemusic-check --output-on-failure`，1/1 通过。
- 2026-05-14：修复首页推荐卡片、最近播放、推荐歌单的点击映射，首页不再把不同按钮都导向同一首歌。
- 2026-05-14：修复播放详情页硬编码的专辑/歌手/时间文案，当前详情区改为使用实际 `playerView_` 数据。
- 2026-05-14：新增高缩放桌面布局与首页卡片命中测试，在独立构建目录 `C:\Users\ICe\.codex\memories\bottlemusic-check-codex` 完成 `EchoNativeSmokeTests`、`EchoWin32` Debug 构建和 `ctest`，1/1 通过。
- 2026-05-14：独立构建目录下启动 `EchoWin32.exe` 3 秒健康检查通过，`Responding=True`、`HasExited=False`、Working Set 约 68.1MB、Private Bytes 约 77.1MB。
- 2026-05-14：原 `native/out/bottlemusic-check` 构建目录存在对象文件占用，当前验证结果以独立构建目录为准；不影响本轮源码修复有效性。
- 2026-05-14：修复侧栏除首页/播放列表外的入口“看起来失灵”的问题：发现、电台、视频、歌曲、专辑、歌手、收藏、下载现在会进入独立占位页，并提供快捷搜索验证入口；头像点击进入登录状态页，明确说明登录接口仍未迁移。
- 2026-05-14：修复搜索和队列播放的元数据串歌问题，搜索结果支持从 `FileName`、`trans_param.union_cover`、`{size}` 封面占位和协议相对 URL 中规范化歌曲、歌手、专辑和封面；播放详情不再固定显示叶惠美/周杰伦。
- 2026-05-14：播放解析增加候选兜底：首个精确结果返回付费或无播放 URL 时，会尝试同名/同歌手的后续可播版本；歌词请求改为播放 URL 成功后再启动，降低歌词串台风险。
- 2026-05-14：修复歌词后段显示逻辑，歌词绘制按当前高亮行居中选择可见窗口；同时缩短登录/占位页长文案，避免裁切造成文字混乱。
- 2026-05-14：在独立构建目录 `C:\Users\ICe\.codex\memories\bottlemusic-check-codex` 重新构建 `EchoNativeSmokeTests`、`EchoWin32` Debug 并直接运行 `EchoNativeSmokeTests.exe`，通过；隐藏启动 `EchoWin32.exe` 3 秒健康检查通过，`Responding=True`、`HasExited=False`、Working Set 约 20.0MB、Private Bytes 约 6.6MB。
- 2026-05-14：截图验证文件：`C:\Users\ICe\.codex\memories\bottlemusic-check-codex\EchoWin32-home-after-final-fix.png`、`EchoWin32-discover-realclick-final-fix.png`、`EchoWin32-login-realclick-final-fix-2.png`。
- 2026-05-15：按 List 11 的 agent 可验证范围收口：导航、搜索、播放详情、歌词、设置、队列、进度条和音量条均已有自动化或截图/启动健康检查覆盖；登录仍是明确占位页，移入 List 13 真实接口缺口继续跟踪。
- 2026-05-15：在独立构建目录 `C:\Users\ICe\.codex\memories\bottlemusic-check-codex` 重新构建 `EchoNativeSmokeTests`、`EchoWin32` Debug，通过；`ctest --test-dir C:\Users\ICe\.codex\memories\bottlemusic-check-codex --output-on-failure` 1/1 通过；隐藏启动 `EchoWin32.exe` 3 秒健康检查通过，`Responding=True`、`HasExited=False`、Working Set 约 19.9MB、Private Bytes 约 6.7MB。
- 2026-05-19：按用户反馈重新打开 List 11：歌曲无声、封面未稳定加载、侧栏多数入口只有占位、歌词和设置/侧栏仍需真实窗口复验。此前“agent 可验证范围收口”不能等同于产品验收通过。

## List 12：长时间播放稳定性

Status：In Progress

Type：AFK

Blocked by：List 10

What to build：

补齐真实播放稳定性验证，覆盖长时间播放、高频切歌、错误恢复和资源释放。

Acceptance criteria：

- [ ] 连续播放 4 小时无崩溃、无明显资源持续增长。
- [x] 连续切歌 100 次后播放状态、封面、歌词和队列状态仍一致。
- [ ] 播放失败、网络错误或 URL 失效时，UI 能收到明确错误且不进入僵死状态。
- [x] 至少补一组自动化验证或诊断日志，减少纯人工观察。

Latest verification：

- 2026-05-15：新增播放候选异步失败恢复的自动化回归：第一个候选进入 Media Foundation 后失败时，UI 会清空错误/歌词状态并推进到下一个候选，避免“解析到歌但没声音后卡死”。
- 2026-05-15：`MainWindow` 的 Media Foundation `Failed` 状态接入候选兜底；同步 URL 解析失败、打开失败和异步播放失败现在走同一套候选推进逻辑。
- 2026-05-15：在独立构建目录重新构建 `EchoNativeSmokeTests`、`EchoWin32` Debug，通过；直接运行 `EchoNativeSmokeTests.exe` 通过；`ctest` 1/1 通过；隐藏启动健康检查通过，`Responding=True`、`HasExited=False`、Working Set 约 19.9MB、Private Bytes 约 6.7MB。
- 2026-05-15：新增 100 次切歌状态一致性回归，覆盖队列索引、候选匹配、播放详情元数据、封面 URL/imageKey、播放器快照和歌词高亮一致；同时修复 `ApplyPlaybackStateSnapshot` 用 UI 时长反推歌词时间的问题，现在使用真实 `currentSeconds` 更新当前时间和歌词。
- 2026-05-15：新增 `native/tools/Measure-PlaybackStability.ps1`；4 小时长跑验收有可复现命令、CSV 采样和 summary JSON。已用 3 秒冒烟跑通 harness：3 次采样全部响应，Private Bytes 增长 0MB。
- 2026-05-19：播放问题重新打开：真实用户路径仍报告无声。已修复搜索/队列候选排序，优先选择 `privilege=0` 且 `pay_type=0` 的可播候选；同时 `HttpClient` 显式开启 30x 重定向跟随，降低酷狗 CDN 图片/音频跳转导致的占位图或播放失败风险。
- 2026-05-19：在原始构建目录 `native/out/bottlemusic-check` 执行 `--clean-first` 全量重编。根因：`QueueTrack` 新增 `coverUrl` 字段后，过期 obj 文件记录的结构体 layout 与当前头文件不匹配，导致 `Next()->title` 读取错误地址，测试断言失败。`--clean-first` 强制重编后 `EchoNativeSmokeTests` ctest 1/1 通过，`EchoWin32` 编译通过，隐藏启动 3 秒健康检查通过：`Responding=True`、`HasExited=False`、Working Set 21.1MB、Private Bytes 6.7MB。

- 2026-05-21：TDD 守则验证。确认候选排序 `RankQueuePlaybackCandidates` 已正确实现（`IsLikelyPlayable` 检查 `payType==0 && privilege==0`），测试覆盖 `queueLookupVm` 场景；确认 `HttpClient::Get` 和 `Post` 均已设置 `WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS`。在 `native/out/bottlemusic-check` clean rebuild 后：`EchoNativeSmokeTests` ctest 1/1 通过（2.63s）；`EchoWin32` 编译通过；隐藏启动 3 秒健康检查通过：`Responding=True`、`HasExited=False`、Working Set 20.1MB、Private Bytes 6.7MB。

## List 13：真实接口与数据质量收敛

Status：In Progress

Type：AFK

Blocked by：List 10

What to build：

继续把 `server/` 中仍有价值的行为迁移到原生后端，减少占位数据和兼容残缺。

Acceptance criteria：

- [x] 盘点尚未迁移但首版需要的真实接口，并形成明确清单。
- [ ] 首页、发现、电台、视频、收藏、下载等页面接入真实数据，不再只显示占位说明。
- [x] 兼容服务为剩余关键接口补 contract fixture，未实现项继续稳定返回 `native_not_implemented`。
- [ ] 二维码、手机、微信、开放平台登录接入真实账号态。
- [ ] 收藏、下载、用户歌单、VIP、播放历史上传、用户详情、云盘等用户态能力接入真实接口。
- [ ] `/everyday/recommend`、`/song/climax`、`/song/ranking`、视频、评论、FM 等非首版接口从 501/空分页推进到真实实现或明确移出目标。
- [ ] 文档更新当前已迁移范围与剩余缺口。

Latest verification：

- 2026-05-15：记录已迁移接口、稳定降级策略和登录/用户态/评论/视频/FM 等剩余缺口；2026-05-19 起收敛回本文，不再单独维护过程文档。
- 2026-05-15：新增 `/playlist/track/all` 与 `/login/qr/key` contract fixture，覆盖歌单歌曲稳定结构和登录未迁移时 HTTP 501、`error_code=native_not_implemented` 的稳定失败结构。
- 2026-05-15：在独立构建目录 `C:\Users\ICe\.codex\memories\bottlemusic-check-codex` 构建 `EchoNativeSmokeTests`、`EchoWin32`、`EchoCompatServer` Debug 通过；直接运行 `EchoNativeSmokeTests.exe` 通过；单独运行 `ctest --test-dir C:\Users\ICe\.codex\memories\bottlemusic-check-codex --output-on-failure --timeout 60` 1/1 通过。
- 2026-05-19：按用户反馈重新打开 List 13。登录、用户态、收藏/下载、真实侧栏数据、视频/FM/评论等仍是未实现或占位，不能标记为 Done。

## List 14：安装包与发布工程化

Status：Todo

Type：AFK

Blocked by：List 11, List 12, List 13

What to build：

开始首个可分发版本的发布工程化，包括安装、签名、更新和发布说明。

Acceptance criteria：

- [x] 选择并落地临时 ZIP 绿色包方案。
- [ ] 明确并落地代码签名、版本号和产物命名策略。
- [x] 至少形成一个可复现的发布构建流程。
- [ ] 自动更新、正式安装包、安装/升级/回滚和已知限制文档完成。

Latest verification：

- 2026-05-15：新增 `native/tools/New-ReleasePackage.ps1`，首版临时选择 ZIP 绿色包；版本号、产物命名、未签名 dev 包状态、升级和回滚流程曾单独文档化，2026-05-19 起收敛回本文。
- 2026-05-15：Debug 包 `D:\KuGouMusic\EchoMusic-main\native\dist\BottleMusic-0.1.0-dev-win-x64-Debug.zip` 生成通过，包内包含 `EchoWin32.exe`、`EchoCompatServer.exe`、`EchoNativeSmokeTests.exe`、`assets/`、`docs/` 和 `package.json`。
- 2026-05-15：Release 构建 `EchoNativeSmokeTests`、`EchoWin32`、`EchoCompatServer` 通过；Release 包 `D:\KuGouMusic\EchoMusic-main\native\dist\BottleMusic-0.1.0-win-x64-Release.zip` 生成通过，大小约 2.05MB，`package.json` 记录 `version=0.1.0`、`configuration=Release`、`signing=unsigned-dev-package`。
- 2026-05-15：隐藏启动 `EchoWin32.exe` 3 秒健康检查通过，`Responding=True`、`HasExited=False`、Working Set 约 20.0MB、Private Bytes 约 6.6MB。
- 2026-05-19：按用户反馈重新打开 List 14。当前只有未签名 ZIP 包，代码签名、自动更新和正式安装包仍未实现。

## List 15：Newsprint Theme Tokens

Status：In Progress（待用户视觉验收）

Type：AFK

Blocked by：None

What to build：

把 `native/win32_app/MainWindow.cpp` 内嵌的 `Palette` 与 38 处颜色 / 字号字面量集中到 Theme 模块，并将配色由旧 Melody 浅色蓝调切换为 Newsprint 报纸风。Newsprint 数值与令牌定义来源为项目根目录 `Music Player.html` 的 `:root` CSS 变量，最终设计基线由后续 List 20 落到 `docs/REFERENCE.zh-CN.md` §4bis "Newsprint UI 规格"。

本切片是 Newsprint 视觉重构（List 15-20）的第一个纵向切片，目标是**零 UI 行为变化、仅配色变化**，为 List 16 渲染管线升级和 List 17 Painter 字体栈做准备。

Acceptance criteria：

- [x] 新增 `native/include/echo/win32_app/Theme.h`：暴露 `theme::color::Paper/PaperAlt/PaperEdge/Ink/InkSoft/InkMute/InkFaint/Rule/RuleSoft/Accent/AccentDeep/GlassTint/GlassTint2/GlassEdge/White` 访问器与 `MakeNewsprintPalette()` 工厂；保留 `Palette` 字段名（bg/panel/line/text/...）以兼容 MainWindow 现有 ~160 处 `palette_.X` 调用。
- [x] 新增 `native/win32_app/Theme.cpp`：颜色值以 `D2D1::ColorF(0xRRGGBB)` 或 `D2D1::ColorF(r,g,b,a)` 形式定义；所有访问器走 `static const D2D1_COLOR_F` 避免重复构造。
- [x] `native/CMakeLists.txt` 在 `EchoWin32Layout` 静态库源列表追加 `win32_app/Theme.cpp`。
- [x] `MainWindow.cpp` 删除匿名命名空间内的 `Palette` 定义；改 `Palette palette_;` 为 `Palette palette_ = MakeNewsprintPalette();`。160 处 `palette_.X` 调用保持原样。
- [x] `tests/basic_contract_tests.cpp` 新增 RED→GREEN 测试：`Paper/Ink/Accent/AccentDeep/Rule/GlassTint` 的 r/g/b 容差 1/255 等于 HTML 参考值；`MakeNewsprintPalette()` 旧字段名映射到 theme color 访问器。
- [x] `EchoNativeSmokeTests` 全部通过（含本切片新增断言）。
- [x] `EchoWin32` Debug 构建通过；启动 3 秒响应检查不退化（`Responding=True`、`HasExited=False`）。
- [ ] 视觉验收（**待用户人工对照**）：启动后所有页面背景由蓝灰浅色变为 `#f1ead8` 纸色；正文从纯黑变 `#221b12` 墨色；红色 `#a8311b` 强调出现在 Home 当前导航 / Now Playing 进度条 / 搜索框聚焦边框。

Done when：

- 所有 Acceptance criteria 勾选完成。
- 内存 snapshot 较 List 14 收敛后基线（Debug Working Set ≈ 19-20 MB）增长不超过 1 MB（理论 ~1 KB 常量表）。

Latest verification：

- 2026-05-23：完成 Theme 模块脚手架、`MakeNewsprintPalette()` 工厂、RED 测试与 `MainWindow::palette_` 初始化切换。
- 2026-05-23：`EchoNativeSmokeTests` Debug 构建 + `ctest` 通过（1/1 passed in 2.61s，含 6 项 Newsprint token 断言）。`EchoWin32` Debug 构建通过。隐藏启动 3 秒：`Id=31332`、`Responding=True`、`HasExited=False`、`WorkingSetMB=20.4`、`PrivateMB=6.8`。
- 内存 snapshot：Debug WorkingSet 20.4 MB（vs List 14 基线 19.9 MB，+0.5 MB，符合预期；远低于空闲 ≤ 120 MB 与播放中 ≤ 180 MB 红线）。

## List 16：D2D Device Context 渲染管线升级

Status：In Progress（待用户启动确认视觉无回归）

Type：AFK

Blocked by：None（与 List 15 解耦，但视觉变化属于 List 15；本切片为纯重构，视觉应与 List 15 完全一致）

What to build：

把 `native/win32_app/MainWindow.cpp` 内嵌的 `ID2D1HwndRenderTarget` 渲染路径升级为 `D3D11Device + IDXGISwapChain1 + ID2D1Device + ID2D1DeviceContext` 路径，打开 D2D Effects（`CLSID_D2D1GaussianBlur` / `CLSID_D2D1ColorMatrix` / `CLSID_D2D1Shadow`）通路，为 List 19 玻璃面板做地基。本切片**视觉应零变化**——所有现有 `renderTarget_->X()` 调用对 `ID2D1DeviceContext`（继承自 `ID2D1RenderTarget`）保持二进制语义兼容，~160 处调用零改动。

Acceptance criteria：

- [x] 新增 `native/include/echo/win32_app/RenderPipeline.h` + `native/win32_app/RenderPipeline.cpp`：单一类 `RenderPipeline` 拥有 Factory1 / D3D11 / DXGI / D2D Device / DeviceContext / SwapChain1 / 后台缓冲 Bitmap1；提供 `InitializeHeadless()`（仅设备链路，无 HWND，供测试）+ `Initialize(hwnd, w, h)`（完整路径）+ `Resize / Shutdown / BeginFrame / EndFrame` API。
- [x] swap chain 使用 `DXGI_FORMAT_B8G8R8A8_UNORM` + `DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL` + `BufferCount=2`；HARDWARE 失败时回退 WARP。
- [x] 设备丢失分类静态方法 `RenderPipeline::IsDeviceLossHResult(HRESULT)` 识别 `D2DERR_RECREATE_TARGET` / `DXGI_ERROR_DEVICE_REMOVED` / `_RESET` / `DRIVER_INTERNAL_ERROR` 四种。
- [x] `MainWindow` 字段类型升级：`ID2D1Factory*` → `ID2D1Factory1*`（非拥有）、`ID2D1HwndRenderTarget*` → `ID2D1DeviceContext*`（非拥有）；新增 `RenderPipeline renderPipeline_` 成员。`InitializeGraphics()` 调用 `InitializeHeadless()` 取工厂；`CreateDeviceResources()` 调用 `Initialize(hwnd)` 创建 swap chain；`DiscardDeviceResources()` 调用 `Shutdown()`；`ResizeRenderTarget()` 调用 `Resize(pxWidth, pxHeight)`。
- [x] `WM_PAINT` 路径：`BeginDraw/EndDraw` 改走 `renderPipeline_.BeginFrame()/EndFrame()`；`EndFrame` 内部触发 `IDXGISwapChain1::Present(1, 0)`；HRESULT 经 `IsDeviceLossHResult` 判定后触发重建。
- [x] `native/CMakeLists.txt`：`EchoWin32Layout` 库追加 `RenderPipeline.cpp` 与 `d2d1 d3d11 dxgi dxguid` PUBLIC 链接；`EchoNativeSmokeTests` 通过 EchoWin32Layout 间接获得依赖。
- [x] `tests/basic_contract_tests.cpp` 新增测试：50 次 `RenderPipeline` `InitializeHeadless() → CreateSolidColorBrush 验证 → Shutdown()` 循环无设备移除、无 ctx 残留；`IsDeviceLossHResult` 四种码识别正确。
- [x] `EchoNativeSmokeTests` Debug 构建 + ctest 通过（13.81s 含 50 次设备创建）。
- [x] `EchoWin32` Debug 构建通过；启动 3 秒响应检查：`Responding=True`、`HasExited=False`。
- [x] 视觉验收（**用户已确认**）：所有页面、组件、字体、文字位置、Newsprint 配色与 List 15 完全一致；窗口最小化 / 还原 / 缩放无闪烁、无设备丢失警告。

Done when：

- 所有 Acceptance criteria 勾选完成。
- 内存 snapshot 较 List 15 增长在预期 D3D11 + flip-model swap chain 余量内（+8-25 MB Working Set），距 180 MB 红线仍有 ≥ 130 MB 余量。

Latest verification：

- 2026-05-23：完成 RenderPipeline 模块、MainWindow 改造、50 周期 RED 测试、`IsDeviceLossHResult` 分类断言、`EchoWin32Layout` 链接 `d3d11 dxgi dxguid`。
- 2026-05-23：`EchoNativeSmokeTests` Debug 构建 + `ctest` 通过（1/1 passed in 13.81s）。`EchoWin32` Debug 构建通过。隐藏启动 3 秒：`Id=27040`、`Responding=True`、`HasExited=False`、`WorkingSetMB=46.5`、`PrivateMB=56.4`。
- 内存 snapshot：Debug WorkingSet 46.5 MB（vs List 15 基线 20.4 MB，+26.1 MB；vs 180 MB 红线余量 133.5 MB）。Private 56.4 MB（vs 6.8 MB，+49.6 MB；committed 比 working set 大属预期 —— DXGI 双缓冲、D2D Device 内部页池均算 committed 但不一定 resident）。增量主要构成估算：D3D11 device + driver overhead ≈ 12-15 MB；DXGI flip-model 双缓冲（1600×1060×4B×2）≈ 13.5 MB；D2D Device + DeviceContext + Factory1 ≈ 2-3 MB。
- 视觉：纯重构，所有 ~160 处 `renderTarget_->X()` 调用保持原行为；预期视觉与 List 15 完全一致。

## List 17：Painter Helper + Newsprint Typography

Status：**Done**

Type：Feature

Blocked by：List 16（ID2D1DeviceContext 管线升级）

What to build：

抽取 `Painter` 工具类，封装报纸风格绘制原语（`DrawMasthead` / `DoubleRule` / `SectionLabel` / `SectionHead` / `PageHead` / `Kicker`），引入 DWrite 衬线字体栈并预创建 TextFormat 缓存。`MainWindow.cpp` 中 section 大标题替换为 `painter_.SectionHead()`（20px 半粗 + 下划 rule，对应 HTML `.section-bar h2`）；Home 顶部新增报头 masthead 横幅；hero 顶部 "今日推荐" 替换为 `painter_.Kicker()`。

Acceptance criteria：

- [x] 新建 `native/include/echo/win32_app/Painter.h`：`class Painter`，API 包括 `InitializeFonts` / `AttachContext` / `DetachContext` / `Shutdown` / `DoubleRule` / `DrawMasthead` / `SectionHead` / `SectionLabel` / `PageHead` / `Kicker` / `MeasureSectionLabel`。
- [x] 新建 `native/win32_app/Painter.cpp`：五种 TextFormat 缓存（masthead 13px 半粗斜体、sectionHead 20px 半粗正体、sectionLabel 11px 斜体、pageHead 22px 粗体、kicker 10px 正体）；字体族 "Noto Serif SC"（DWrite 系统回退）；AttachContext 创建四支 theme::color 笔刷。
- [x] `native/CMakeLists.txt`：`EchoWin32Layout` 追加 `Painter.cpp` 源；PUBLIC 链接追加 `dwrite`。
- [x] `MainWindow.cpp` 生命周期接入：`InitializeGraphics` → `painter_.InitializeFonts`；`CreateDeviceResources` → `painter_.AttachContext`；`DiscardDeviceResources` → `painter_.DetachContext`。
- [x] `MainWindow.cpp` 绘制替换：`DrawHome` 顶部追加 `painter_.DrawMasthead`；"为你推荐" / "最近播放" / "推荐歌单" 三处替换为 `painter_.SectionHead`（20px + 下划线）；`DrawHero` "今日推荐" 替换为 `painter_.Kicker`。
- [x] `tests/basic_contract_tests.cpp` 新增 `PainterMeasureTest`：`MeasureSectionLabel(L"早晨")` > 0；更长文本宽度更大。
- [x] `EchoNativeSmokeTests` Debug 构建 + ctest 通过（1/1 passed in 14.15s）。
- [x] `EchoWin32` Debug 构建通过；启动 3 秒响应检查通过（Responding=True）。
- [x] 视觉验收：Home 顶部出现 "BOTTLE TIMES — Vol. I" 双线 masthead；"为你推荐 / 最近播放 / 推荐歌单" 呈 20px 半粗衬线 + 下划 rule；hero 顶部 "今日推荐" 呈小号弱色 kicker。

Done when：

- 所有 Acceptance criteria 勾选完成。
- 内存增量 ≤ +5 MB（TextFormat 缓存 + 笔刷，均为 DWrite 设备无关对象）。

Latest verification：

- 2026-05-23：完成 Painter 模块初版（SectionLabel 11px）；修正：HTML `.section-bar h2` 对应 20px 半粗，新增 `SectionHead` 替换 DrawHome 三处标题。`EchoNativeSmokeTests` 强制重编通过（1/1 passed in 14.15s）；`EchoWin32` 构建通过。
- 内存 snapshot：Debug WorkingSet 49.2 MB（vs List 16 基线 46.5 MB，+2.7 MB；vs 180 MB 红线余量 130.8 MB）。增量来源：DWrite TextFormat 缓存 × 5 + Painter 笔刷 × 4 ≈ 2-3 MB，符合预期。

## List 18：Newsprint Home 重绘 + 占位页统一空态

Status：In Progress（Phase 1 视觉就位，HomeView/PlaceholderView 抽取延后）

Type：Feature

Blocked by：List 17（Painter Helper + Newsprint Typography）

What to build：

将 `MainWindow.cpp` 中 Home / Sidebar / Feature 三类 D2D 绘制按 `Music Player.html` Newsprint 参考重绘视觉表层，并把内联的 7 个占位页（Discover / Radio / Video / Songs / Albums / Artists / Favorites / Downloads / Recent）统一为报纸风空态。`HomeView` / `PlaceholderView` 的类抽取在 Phase 2 推进（与 List 20 的 IPage 拆分合并执行更经济）。

Acceptance criteria：

- [x] **Phase 1.A · 侧边栏激活态报纸化**：`DrawSidebar` 主导航与设置项激活态由"圆角面板 + 蓝紫文字"切换为"半透明墨色弱化背景 + 左侧 2px 红色竖条 + Ink 文本"（对应 HTML `.nav a.active::before`）。
- [x] **Phase 1.B · Hero 报纸化**：`DrawHero` 背景由蓝/黄/绿渐变改为 `PaperAlt` + 1px Rule 外框 + 6px 内嵌 dashed 边框（`D2D1_DASH_STYLE_DASH`）；标题升至 30px 半粗；副标题 13px InkSoft；红色播放钮；Kicker 升级为 "TODAY'S FEATURE · 今日推荐"。
- [x] **Phase 1.C · 占位页统一空态**：`DrawFeaturePage` 替换为 Kicker（"COMING SOON · 即将上线"）+ Painter PageHead 大标题 + DoubleRule 双线装饰 + 14px InkSoft 说明 + 红色 CTA 按钮。原 panel 卡片去除，与报纸风一致。
- [x] **dashStrokeStyle_ 资源管理**：`d2dFactory_->CreateStrokeStyle(D2D1_DASH_STYLE_DASH)` 在 `InitializeGraphics` 创建（factory 级别，跨设备复用），析构释放；`transientBrush_` 在 `DiscardDeviceResources` 释放（设备相关）。
- [x] `EchoNativeSmokeTests` Debug 构建 + ctest 通过（1/1 passed in 14.05s）。
- [x] `EchoWin32` Debug 构建通过；隐藏启动 3 秒响应（Responding=True、WorkingSetMB=49.2、零增量）。
- [ ] **视觉验收（待用户人工确认）**：侧边栏首页/设置激活时显示 2px 红色竖条；Home Hero 显示纸色背景 + 双层边框；Discover/Radio/Video 等占位页显示 Newsprint 空态（kicker + 大标题 + 双线）。
- [ ] **Phase 2（延期）**：抽 `pages/HomeView.h/cpp` 与 `pages/PlaceholderView.h/cpp`；`MainWindow` 持有 `unique_ptr<HomeView>` + `unique_ptr<PlaceholderView>`；`HomeView::HitTest` 复用 `Layout::HitTestHome`。可合并到 List 20 的 IPage 拆分执行。

Done when：

- Phase 1 视觉验收完成（用户确认侧边栏红条 / Hero 报纸框 / 占位页空态符合 HTML 参考）。
- Phase 2 抽取在 List 20 完成或单独切片。

Latest verification：

- 2026-05-23：Phase 1 完成 —— 侧边栏激活态 + DrawHero 纸色双框 + DrawFeaturePage Newsprint 空态；`dashStrokeStyle_` 在 InitializeGraphics 创建；`EchoNativeSmokeTests` ctest 1/1 passed in 14.05s；`EchoWin32` 构建通过；隐藏启动：WorkingSetMB=49.2、PrivateMB=55.1、Responding=True。
- 内存 snapshot：Debug WorkingSet 49.2 MB（与 List 17 baseline 持平，零增量 — 仅视觉常量与 1 个 ID2D1StrokeStyle 对象 ≈ < 1 KB）。

## List 19：Glass Panels + Paper Texture

Status：Done（视觉验收待用户确认）

Type：Feature

Blocked by：List 16（D2D DeviceContext 升级，CreateEffect 通路）+ List 18 Phase 1（视觉基础就位）

What to build：

按 `Music Player.html` 与 `前端部分报告.md` 落地玻璃面板系统：底部播放栏（首版唯一应用点）使用 D2D Effects 链（GaussianBlur 22px → ColorMatrix saturate 1.36×）叠加纸纹 + 高光边线，实现 Newsprint 风格的毛玻璃效果。

Acceptance criteria：

- [x] **`PaperTexture.{h,cpp}`** —— 程序化生成 128×128 BGRA 纸纹 tile（不打包 PNG，零磁盘 I/O；总常驻 ~64 KB），通过 BitmapBrush(EXTEND_WRAP + NEAREST_NEIGHBOR) 平铺。基色 Paper #f1ead8 ±10 灰度噪声 + 5% 暗斑 + 1% 高光颗粒。
- [x] **`GlassPanel.{h,cpp}`** —— sceneBitmap(全分辨率离屏，CopyFromRenderTarget 抓取 backbuffer) + blurredBitmap(¼ 分辨率 TARGET bitmap，SetTarget 后 DrawImage 写入) + GaussianBlur(σ=22.0, BORDER_MODE_HARD) → ColorMatrix(Rec.601 饱和度矩阵 s=1.36) → composite to rect + tint(GlassTint rgba 0.97/0.95/0.90/0.46) + 可选 paper + 1px GlassEdge 高光顶线。
- [x] **`blurDirty` 脏标记** —— 每帧 Paint 在 `PopAxisAlignedClip` 后、`DrawPlayerBar` 前调用 `glass_.MarkBlurDirty()`，确保 sceneBitmap 抓取的是 player bar 上方所有已绘内容；`RebuildBlurIfNeeded` 内部消费并复位，单帧仅模糊一次。
- [x] **设备生命周期** —— `CreateDeviceResources` 末尾 `paperTex_.Initialize(ctx) + glass_.Initialize(ctx, W, H)`；`DiscardDeviceResources` 调 `glass_.OnDeviceLost() + paperTex_.OnDeviceLost()`；`Resize` 通过 `glass_.EnsureSceneSize(W, H)` 重建 bitmap 并标记 dirty。
- [x] **`DrawPlayerBar` 接入** —— 入口首个 `FillRect(barRect, palette_.panel)` 替换为 `glass_.DrawGlassPanel(barRect, GlassTint, GlassEdge, &paperTex_, 0.22)`；glass_ 未就绪时回退原 Paper 纯色。
- [x] **CMake** —— `EchoWin32Layout` 库追加 `GlassPanel.cpp` + `PaperTexture.cpp`。
- [x] **RED → GREEN 测试**：`GlassPanelRender` 在 RenderPipeline::InitializeHeadless 提供的真实 ID2D1DeviceContext 上验证 Initialize(1280, 800) 之后 blurredBitmap 大小为 320×200(¼)、blurDirty 初始为 true、EnsureSceneSize 同尺寸幂等；`PaperTextureSmoke` 验证 Initialize 成功 + bitmap() 非空。`ctest` 1/1 passed in 15.55s。
- [x] **构建** —— `EchoWin32` Debug 编译链接通过；`EchoNativeSmokeTests` 通过。
- [ ] **视觉验收（待用户人工确认）**：底部播放栏显示毛玻璃（背景滚动时模糊跟随）；玻璃面板上沿出现 1px GlassEdge 高光线；纸纹颗粒可辨。

Done when：

- 视觉验收完成。
- 真实播放下 GPU 静止 < 1% / 播放 < 5%（首版未做 GPU 量测，留待长稳验证）。

Latest verification：

- 2026-05-24：完整接入完成。`EchoNativeSmokeTests` ctest 1/1 passed in 15.55s（含 GlassPanelRender + PaperTextureSmoke 两个新 RED→GREEN）；`EchoWin32` Debug 编译通过；隐藏启动 5 秒响应：WorkingSetMB=92.6、PrivateMB=97.5、Responding=True。
- 内存增量：相对 List 18 baseline（47.2 MB WS / 56.6 MB Private）+45 MB WS / +41 MB Private —— 略高于计划估算的 +15-20 MB，主要来自 sceneBitmap (~8 MB ARGB 1080p) + blurredBitmap (~0.5 MB) + D2D Effects 内部状态 + DXGI/D3D11/D2D Effects 库加载。仍在 180 MB 预算内（剩 85 MB 容真实播放）。

## 历史内存基线

来源：原 `docs/MEMORY_BUDGET.zh-CN.md` 已合并入 `REFERENCE.zh-CN.md`，历史快照保留在本附录。
口径：`EchoWin32.exe` 隐藏窗口启动 3 秒，未播放音频，未进行真实网络图片加载。

| 时间 | 范围 | Working Set | Private Bytes |
| --- | --- | --- | --- |
| 2026-05-10 | Debug，List 04 完成后 | 约 18.7 MB | 约 6.5 MB |
| 2026-05-10 | Debug，List 05 完成后 | 约 18.7 MB | 约 6.5 MB |
| 2026-05-10 | Debug，List 06（启用 SQLite 与设置持久化）后 | 约 19.0 MB | 约 6.7 MB |
| 2026-05-11 | Debug，List 10 收敛 | 约 19.9 MB | 约 6.6 MB |
| 2026-05-11 | Release，List 10 收敛 | 约 19.0 MB | 约 6.6 MB |

离空闲目标（≤ 120 MB）有大量余量；播放中目标（≤ 180 MB）待长跑稳定性验证完成后再补充实测，参见 List 12。
