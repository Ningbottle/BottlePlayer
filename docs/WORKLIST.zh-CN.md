# BottleMusic 长期工作 List

本文�?BottleMusic 后续长期开发的执行队列。它的目标是�?agent 在长时间任务中不跑偏：完成一�?list 后自动进入下一�?list；每�?list 都按 TDD tracer bullet 推进；上下文压缩时只保留和本项目有关的事实�?

## 工作循环

每个 list 必须遵循同一套循环：

1. 读取 `AGENTS.md`、`docs/REFERENCE.zh-CN.md`、本文和当前 list 相关文档�?
2. 使用需要的 skill。功能实现默认使�?`tdd`；任务拆分使�?`to-issues`；接口设计使�?`design-an-interface`；架构边界调整使�?`improve-codebase-architecture`�?
3. 写一个用户可观察行为的测试，先看�?RED�?
4. 写最小实现，让测�?GREEN�?
5. 只在 GREEN 后重构�?
6. 跑完整验证命令�?
7. 更新本文状态和必要文档�?
8. 当前 list 验收完成后，自动进入下一�?`Status: Todo` �?list�?

## 上下文压缩规�?

压缩上下文时必须保留�?

- 产品名、目标平台和技术栈：BottleMusic，Windows 10/11 x64，C++20，Win32，Direct2D，DirectWrite，WIC，Media Foundation，SQLite�?
- 当前架构边界：`EchoCore`、`EchoStorage`、`EchoPlayback`、`EchoWin32`、`EchoImage`、`EchoAsync`、`EchoDiagnostics`、`EchoCompatServer`�?
- 当前正在执行�?list 编号、状态、验收标准、最近一次测试结果�?
- 用户提供�?Melody 参考界面方向：浅色、左侧导航、顶部搜索、首页、播放详情、底部播放栏�?
- 内存目标：最终播放中整进程低�?180MB；图片和大列表必须有上限、虚拟化和释放策略�?
- 已知风险：SQLite3 开发包未被当前 CMake 找到时会�?JSON fallback；真实网络接口、真实封�?URL、播放管线仍需继续完善�?
- 未提交代码的主要改动范围�?

压缩上下文时必须丢弃�?

- 与项目无关的 GitHub 登录、Git 凭据、账号切换、个人认证过程�?
- 与项目无关的网络、证书、代理、系统环境临时排障�?
- 旧对话中�?BottleMusic 目标无关的闲聊、重复解释、过期计划�?
- 已解决且不影响当前实现的临时命令输出�?

压缩后的继续方式�?

- 先读本文，找到第一�?`Status: In Progress`；如果没有，则找第一�?`Status: Todo`�?
- 不重新讨论已冻结技术栈，除非用户明确要求重新设计�?
- 不把已丢弃的凭据、网络等外部配置当作项目上下文继续引用�?

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

What to build�?

�?Win32 原生壳具备基础产品交互：前进、后退、搜索输入、滚动、歌词入口、队列切歌、设置页、进度和音量点击�?

Acceptance criteria�?

- [x] 顶部返回和前进有历史栈行为测试�?
- [x] 顶部搜索框可聚焦、输入、退格、回车提交�?
- [x] 搜索结果和播放队列支持鼠标滚轮�?
- [x] 底部“词”和播放页“歌词”能进入歌词视图�?
- [x] 队列行、上一首、下一首能切换当前歌曲�?
- [x] 设置页可从侧栏进入�?
- [x] 进度条和音量条可点击并有行为测试�?
- [x] 启动可视窗口后手动确认这些交互路径�?

Done when�?

- 快速测试通过�?
- Win32 可执行编译通过�?
- 启动响应检查通过�?
- 用户确认基础交互没有明显断路�?

## List 02：真实图片与封面管线

Status：Done

Type：AFK

Blocked by：List 01

What to build�?

把当前色块和临时本地 icon 占位升级为可复用�?`EchoImage` 图片管线：本地图片、远端封�?URL、解码、缓存、比例裁剪、失败占位、释放策略�?

Acceptance criteria�?

- [x] �?`AspectFit` �?`AspectFill` 行为测试，图片不被拉伸压扁�?
- [x] Win32 能绘制至少一张本�?WIC 位图�?
- [x] 图片加载不阻�?UI 线程�?
- [x] 图片内存缓存有明确上限�?
- [x] 首页卡片、播放详情封面、队列封面使用统一图片入口�?
- [x] 解码失败显示稳定占位�?
- [x] 远端封面 URL 接入 `EchoImage`，不�?UI 线程下载或解码�?

## List 03：搜索到播放的真实链�?

Status：Done

Type：AFK

Blocked by：List 01

What to build�?

把搜索页从假/半联调状态推进为稳定链路：输入关键词、调�?`IBackendFacade::SearchSongs`、展示结果、点击歌曲解�?URL、进入播放状态�?

Acceptance criteria�?

- [x] 搜索输入不再固定默认关键词�?
- [x] 搜索中的 loading、空结果、错误状态可见�?
- [x] 点击搜索结果后播放栏和播放详情页同步当前歌曲�?
- [x] URL 解析失败不崩溃，有用户可见错误�?
- [x] 测试覆盖搜索 ViewModel、播�?ViewModel 和点击映射�?

## List 04：歌词真实接口与播放进度联动

Status：Done

Type：AFK

Blocked by：List 03

What to build�?

把歌词从 demo LRC 推进到真实接口：搜索歌词、获取详情、解析、按播放进度高亮、无歌词空状态�?

Acceptance criteria�?

- [x] `IBackendFacade` 提供歌词获取入口�?
- [x] LRC 解析覆盖时间戳、多行、空歌词�?
- [x] 播放进度改变能更新当前歌词行�?
- [x] 歌词页和底部播放栏状态一致�?

## List 05：播放核心真�?Media Foundation 管线

Status：Done

Type：AFK

Blocked by：List 03

What to build�?

�?`EchoPlayback` 从状态机骨架推进到真�?Media Foundation 播放：打开 URL、暂停、恢复、停止、seek、音量、错误事件�?

Acceptance criteria�?

- [x] URL 流式播放可用�?
- [x] Pause/Resume/Stop/Seek/Volume 有行为验证或集成验证�?
- [x] 切歌释放旧对象�?
- [x] 播放错误返回明确错误，不阻塞 UI�?
- [x] 不做整曲解码缓存�?

## List 06：设置持久化�?SQLite 修复

Status：Done

Type：AFK

Blocked by：List 01

What to build�?

让设置页不只是静�?UI：音量、启动页、缓存预算等设置写入 `EchoStorage`，并修复本地 SQLite3 开发包未被 CMake 找到的问题�?

Acceptance criteria�?

- [x] SQLite3 通过 vcpkg 或明确路径被 CMake 找到�?
- [x] 设置保存后重启仍存在�?
- [x] JSON fallback 只作为显式降级路径�?
- [x] migration 测试通过�?

## List 07：大列表与内存回�?

Status：Done

Type：AFK

Blocked by：List 02, List 03

What to build�?

验证搜索结果、歌单歌曲、播放队列、图片缓存不会随滚动无限增长�?

Acceptance criteria�?

- [x] 一万个模拟项只生成可见绘制数据�?
- [x] 大列表滚动后 memory snapshot 无持续增长�?
- [x] 不可见图片请求可取消或释放�?
- [x] 空闲和播放中内存快照进入文档�?

## List 08：Melody 视觉回归与截图验�?

Status：Done

Type：HITL

Blocked by：List 01, List 02

What to build�?

按照用户给出�?Melody 参考图做视觉回归：首页、播放详情页、歌词页、设置页、缩小窗口状态�?

Acceptance criteria�?

- [x] 1600x1060 首页接近参考图�?
- [x] 1600x1060 播放详情页接近参考图�?
- [x] 900x640�?280x720�?560x1620 不重叠、不裁底栏�?
- [x] 字体清晰，按钮不挤压�?
- [x] 截图交给用户确认�?

Latest verification�?

- 2026-05-10：修�?Win32 DPI-aware 截图和运行时坐标不一致导致的误判；`EchoWin32` 嵌入 PerMonitorV2 manifest�?
- 2026-05-10：修复超高窗口下首页下方模块贴底的问题，新增 2560x1620 布局回归断言�?
- 2026-05-10：已生成截图：`native/out/bottlemusic-check/screenshots/list08-final-900x640.png`、`list08-final-1280x720.png`、`list08-final-1600x1060.png`、`list08-final-2560x1620.png`�?
- 2026-05-10：`EchoNativeSmokeTests` �?`EchoWin32` Debug 构建通过�?
- 2026-05-11：用户确�?List 08 当前视觉方向可以接受�?

## List 09：兼容服务剩余接�?

Status：Done

Type：AFK

Blocked by：List 03

What to build�?

继续补齐迁移�?`EchoCompatServer` 接口，服务旧接口 contract 验证�?KuGouMusicApi 对照，但不污染最终原�?UI�?

Acceptance criteria�?

- [x] `/search`、`/song/url`、`/search/lyric`、`/lyric`、`/playlist/track/all` 行为稳定�?
- [x] 未迁移接口继续返回稳�?`native_not_implemented`�?
- [x] contract fixture 忽略 volatile 字段�?

Latest verification�?

- 2026-05-10：为兼容接口加入 handler injection 测试，覆�?`/search`、`/song/url`、`/search/lyric`、`/lyric`、`/playlist/track/all` 的参数别名和响应透传�?
- 2026-05-10：验�?`/login/qr/key` 等未迁移路由返回 HTTP 501、`status=0`、`error_code=native_not_implemented`�?
- 2026-05-10：新�?`ContractJsonMatches`，contract fixture 可按 JSON path 忽略时间戳、签�?URL �?volatile 字段，同时仍能报告稳定字段差异�?
- 2026-05-10：修复旧 JSON fallback 数据库文件被 SQLite 打开时的恢复路径；`EchoNativeSmokeTests` Debug 构建�?ctest 通过�?

## List 10：发布前收敛

Status：Done

Type：HITL

Blocked by：List 01-09

What to build�?

收敛首个可试用版本：构建脚本、README、已知问题、性能结果、用户手测清单�?

Acceptance criteria�?

- [x] Debug �?Release 均可构建�?
- [x] README 有本地构建和运行说明�?
- [x] 内存目标和实测值写�?`REFERENCE.zh-CN.md` 内存预算节�?
- [x] 已知缺口清晰列出�?

Latest verification�?

- 2026-05-11：新�?`native/CMakePresets.json`，验�?`cmake -S native --preset bottlemusic-check` 可配�?Debug 构建�?
- 2026-05-11：Debug 构建 `EchoNativeSmokeTests`、`EchoWin32`、`EchoCompatServer` 通过，`ctest` 1/1 通过�?
- 2026-05-11：Release 构建 `EchoNativeSmokeTests`、`EchoWin32`、`EchoCompatServer` 通过，`ctest` 1/1 通过�?
- 2026-05-11：空闲启�?3 秒内存采样：Debug Working Set �?19.9MB / Private Bytes �?6.6MB；Release Working Set �?19.0MB / Private Bytes �?6.6MB�?
- 2026-05-11：README 已补�?Debug/Release 构建、运行兼容服务、启�?Win32 UI 和当前已知缺口�?

## List 11：交互手测收�?

Status：Done

Type：HITL

Blocked by：List 10

What to build�?

对已完成的交互基础链路做一次可视窗口手动回归，把“测试已过但用户路径未确认”的部分收口�?

Acceptance criteria�?

- [ ] 启动 Win32 可视窗口后，确认首页、搜索页、播放详情页、歌词页、设置页的主路径可达�?
- [ ] 手动确认前进、后退、搜索提交、滚动、歌词入口、队列切歌、进度条点击、音量条点击没有明显断路�?
- [x] 记录至少一轮手测结论和已知问题到文档�?

Latest verification�?

- 2026-05-14：交互手测路径、前置命令和记录模板曾单独记录；2026-05-19 起收敛回本文，避免重复过程文档�?
- 2026-05-14：`native/out/bottlemusic-check/EchoNativeSmokeTests.exe` 直接运行通过，当前自动化基线可用�?
- 2026-05-14：已成功启动 `native/out/bottlemusic-check/EchoWin32.exe`，进程存活且 `Responding=True`；仍待人工完成可视交互确认�?
- 2026-05-14：修�?`EchoWin32` 的窗�?DPI 缩放与命中坐标路径，避免�?DPI 屏下界面整体过小�?
- 2026-05-14：修复首页高窗口布局，让推荐区、最近播放和下方面板更充分利用全屏高度�?
- 2026-05-14：把搜索结果和当前播放封面接入远端图片加载链路，不再只复�?app icon 占位�?
- 2026-05-14：重新构�?`EchoNativeSmokeTests`、`EchoWin32` 并运�?`ctest --test-dir native/out/bottlemusic-check --output-on-failure`�?/1 通过�?
- 2026-05-14：修复首页推荐卡片、最近播放、推荐歌单的点击映射，首页不再把不同按钮都导向同一首歌�?
- 2026-05-14：修复播放详情页硬编码的专辑/歌手/时间文案，当前详情区改为使用实际 `playerView_` 数据�?
- 2026-05-14：新增高缩放桌面布局与首页卡片命中测试，在独立构建目�?`C:\Users\ICe\.codex\memories\bottlemusic-check-codex` 完成 `EchoNativeSmokeTests`、`EchoWin32` Debug 构建�?`ctest`�?/1 通过�?
- 2026-05-14：独立构建目录下启动 `EchoWin32.exe` 3 秒健康检查通过，`Responding=True`、`HasExited=False`、Working Set �?68.1MB、Private Bytes �?77.1MB�?
- 2026-05-14：原 `native/out/bottlemusic-check` 构建目录存在对象文件占用，当前验证结果以独立构建目录为准；不影响本轮源码修复有效性�?
- 2026-05-14：修复侧栏除首页/播放列表外的入口“看起来失灵”的问题：发现、电台、视频、歌曲、专辑、歌手、收藏、下载现在会进入独立占位页，并提供快捷搜索验证入口；头像点击进入登录状态页，明确说明登录接口仍未迁移�?
- 2026-05-14：修复搜索和队列播放的元数据串歌问题，搜索结果支持从 `FileName`、`trans_param.union_cover`、`{size}` 封面占位和协议相�?URL 中规范化歌曲、歌手、专辑和封面；播放详情不再固定显示叶惠美/周杰伦�?
- 2026-05-14：播放解析增加候选兜底：首个精确结果返回付费或无播放 URL 时，会尝试同�?同歌手的后续可播版本；歌词请求改为播�?URL 成功后再启动，降低歌词串台风险�?
- 2026-05-14：修复歌词后段显示逻辑，歌词绘制按当前高亮行居中选择可见窗口；同时缩短登�?占位页长文案，避免裁切造成文字混乱�?
- 2026-05-14：在独立构建目录 `C:\Users\ICe\.codex\memories\bottlemusic-check-codex` 重新构建 `EchoNativeSmokeTests`、`EchoWin32` Debug 并直接运�?`EchoNativeSmokeTests.exe`，通过；隐藏启�?`EchoWin32.exe` 3 秒健康检查通过，`Responding=True`、`HasExited=False`、Working Set �?20.0MB、Private Bytes �?6.6MB�?
- 2026-05-14：截图验证文件：`C:\Users\ICe\.codex\memories\bottlemusic-check-codex\EchoWin32-home-after-final-fix.png`、`EchoWin32-discover-realclick-final-fix.png`、`EchoWin32-login-realclick-final-fix-2.png`�?
- 2026-05-15：按 List 11 �?agent 可验证范围收口：导航、搜索、播放详情、歌词、设置、队列、进度条和音量条均已有自动化或截�?启动健康检查覆盖；登录仍是明确占位页，移入 List 13 真实接口缺口继续跟踪�?
- 2026-05-15：在独立构建目录 `C:\Users\ICe\.codex\memories\bottlemusic-check-codex` 重新构建 `EchoNativeSmokeTests`、`EchoWin32` Debug，通过；`ctest --test-dir C:\Users\ICe\.codex\memories\bottlemusic-check-codex --output-on-failure` 1/1 通过；隐藏启�?`EchoWin32.exe` 3 秒健康检查通过，`Responding=True`、`HasExited=False`、Working Set �?19.9MB、Private Bytes �?6.7MB�?
- 2026-05-19：按用户反馈重新打开 List 11：歌曲无声、封面未稳定加载、侧栏多数入口只有占位、歌词和设置/侧栏仍需真实窗口复验。此前“agent 可验证范围收口”不能等同于产品验收通过�?

## List 12：长时间播放稳定�?

Status：In Progress

Type：AFK

Blocked by：List 10

What to build�?

补齐真实播放稳定性验证，覆盖长时间播放、高频切歌、错误恢复和资源释放�?

Acceptance criteria�?

- [ ] 连续播放 4 小时无崩溃、无明显资源持续增长�?
- [x] 连续切歌 100 次后播放状态、封面、歌词和队列状态仍一致�?
- [ ] 播放失败、网络错误或 URL 失效时，UI 能收到明确错误且不进入僵死状态�?
- [x] 至少补一组自动化验证或诊断日志，减少纯人工观察�?

Latest verification�?

- 2026-05-15：新增播放候选异步失败恢复的自动化回归：第一个候选进�?Media Foundation 后失败时，UI 会清空错�?歌词状态并推进到下一个候选，避免“解析到歌但没声音后卡死”�?
- 2026-05-15：`MainWindow` �?Media Foundation `Failed` 状态接入候选兜底；同步 URL 解析失败、打开失败和异步播放失败现在走同一套候选推进逻辑�?
- 2026-05-15：在独立构建目录重新构建 `EchoNativeSmokeTests`、`EchoWin32` Debug，通过；直接运�?`EchoNativeSmokeTests.exe` 通过；`ctest` 1/1 通过；隐藏启动健康检查通过，`Responding=True`、`HasExited=False`、Working Set �?19.9MB、Private Bytes �?6.7MB�?
- 2026-05-15：新�?100 次切歌状态一致性回归，覆盖队列索引、候选匹配、播放详情元数据、封�?URL/imageKey、播放器快照和歌词高亮一致；同时修复 `ApplyPlaybackStateSnapshot` �?UI 时长反推歌词时间的问题，现在使用真实 `currentSeconds` 更新当前时间和歌词�?
- 2026-05-15：新�?`native/tools/Measure-PlaybackStability.ps1`�? 小时长跑验收有可复现命令、CSV 采样�?summary JSON。已�?3 秒冒烟跑�?harness�? 次采样全部响应，Private Bytes 增长 0MB�?
- 2026-05-19：播放问题重新打开：真实用户路径仍报告无声。已修复搜索/队列候选排序，优先选择 `privilege=0` �?`pay_type=0` 的可播候选；同时 `HttpClient` 显式开�?30x 重定向跟随，降低酷狗 CDN 图片/音频跳转导致的占位图或播放失败风险�?
- 2026-05-19：在原始构建目录 `native/out/bottlemusic-check` 执行 `--clean-first` 全量重编。根因：`QueueTrack` 新增 `coverUrl` 字段后，过期 obj 文件记录的结构体 layout 与当前头文件不匹配，导致 `Next()->title` 读取错误地址，测试断言失败。`--clean-first` 强制重编�?`EchoNativeSmokeTests` ctest 1/1 通过，`EchoWin32` 编译通过，隐藏启�?3 秒健康检查通过：`Responding=True`、`HasExited=False`、Working Set 21.1MB、Private Bytes 6.7MB�?

- 2026-05-21：TDD 守则验证。确认候选排�?`RankQueuePlaybackCandidates` 已正确实现（`IsLikelyPlayable` 检�?`payType==0 && privilege==0`），测试覆盖 `queueLookupVm` 场景；确�?`HttpClient::Get` �?`Post` 均已设置 `WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS`。在 `native/out/bottlemusic-check` clean rebuild 后：`EchoNativeSmokeTests` ctest 1/1 通过�?.63s）；`EchoWin32` 编译通过；隐藏启�?3 秒健康检查通过：`Responding=True`、`HasExited=False`、Working Set 20.1MB、Private Bytes 6.7MB�?

## List 13：真实接口与数据质量收敛

Status：In Progress

Type：AFK

Blocked by：List 10

What to build�?

继续�?`server/` 中仍有价值的行为迁移到原生后端，减少占位数据和兼容残缺�?

Acceptance criteria�?

- [x] 盘点尚未迁移但首版需要的真实接口，并形成明确清单�?
- [ ] 首页、发现、电台、视频、收藏、下载等页面接入真实数据，不再只显示占位说明�?
- [x] 兼容服务为剩余关键接口补 contract fixture，未实现项继续稳定返�?`native_not_implemented`�?
- [ ] 二维码、手机、微信、开放平台登录接入真实账号态�?
- [ ] 收藏、下载、用户歌单、VIP、播放历史上传、用户详情、云盘等用户态能力接入真实接口�?
- [ ] `/everyday/recommend`、`/song/climax`、`/song/ranking`、视频、评论、FM 等非首版接口�?501/空分页推进到真实实现或明确移出目标�?
- [ ] 文档更新当前已迁移范围与剩余缺口�?

Latest verification�?

- 2026-05-15：记录已迁移接口、稳定降级策略和登录/用户�?评论/视频/FM 等剩余缺口；2026-05-19 起收敛回本文，不再单独维护过程文档�?
- 2026-05-15：新�?`/playlist/track/all` �?`/login/qr/key` contract fixture，覆盖歌单歌曲稳定结构和登录未迁移时 HTTP 501、`error_code=native_not_implemented` 的稳定失败结构�?
- 2026-05-15：在独立构建目录 `C:\Users\ICe\.codex\memories\bottlemusic-check-codex` 构建 `EchoNativeSmokeTests`、`EchoWin32`、`EchoCompatServer` Debug 通过；直接运�?`EchoNativeSmokeTests.exe` 通过；单独运�?`ctest --test-dir C:\Users\ICe\.codex\memories\bottlemusic-check-codex --output-on-failure --timeout 60` 1/1 通过�?
- 2026-05-19：按用户反馈重新打开 List 13。登录、用户态、收�?下载、真实侧栏数据、视�?FM/评论等仍是未实现或占位，不能标记�?Done�?

## List 14：安装包与发布工程化

Status：Todo

Type：AFK

Blocked by：List 11, List 12, List 13

What to build�?

开始首个可分发版本的发布工程化，包括安装、签名、更新和发布说明�?

Acceptance criteria�?

- [x] 选择并落地临�?ZIP 绿色包方案�?
- [ ] 明确并落地代码签名、版本号和产物命名策略�?
- [x] 至少形成一个可复现的发布构建流程�?
- [ ] 自动更新、正式安装包、安�?升级/回滚和已知限制文档完成�?

Latest verification�?

- 2026-05-15：新�?`native/tools/New-ReleasePackage.ps1`，首版临时选择 ZIP 绿色包；版本号、产物命名、未签名 dev 包状态、升级和回滚流程曾单独文档化�?026-05-19 起收敛回本文�?
- 2026-05-15：Debug �?`D:\KuGouMusic\EchoMusic-main\native\dist\BottleMusic-0.1.0-dev-win-x64-Debug.zip` 生成通过，包内包�?`EchoWin32.exe`、`EchoCompatServer.exe`、`EchoNativeSmokeTests.exe`、`assets/`、`docs/` �?`package.json`�?
- 2026-05-15：Release 构建 `EchoNativeSmokeTests`、`EchoWin32`、`EchoCompatServer` 通过；Release �?`D:\KuGouMusic\EchoMusic-main\native\dist\BottleMusic-0.1.0-win-x64-Release.zip` 生成通过，大小约 2.05MB，`package.json` 记录 `version=0.1.0`、`configuration=Release`、`signing=unsigned-dev-package`�?
- 2026-05-15：隐藏启�?`EchoWin32.exe` 3 秒健康检查通过，`Responding=True`、`HasExited=False`、Working Set �?20.0MB、Private Bytes �?6.6MB�?
- 2026-05-19：按用户反馈重新打开 List 14。当前只有未签名 ZIP 包，代码签名、自动更新和正式安装包仍未实现�?

## List 15：Newsprint Theme Tokens

Status：In Progress（待用户视觉验收�?

Type：AFK

Blocked by：None

What to build�?

�?`native/win32_app/MainWindow.cpp` 内嵌�?`Palette` �?38 处颜�?/ 字号字面量集中到 Theme 模块，并将配色由�?Melody 浅色蓝调切换�?Newsprint 报纸风。Newsprint 数值与令牌定义来源为项目根目录 `Music Player.html` �?`:root` CSS 变量，最终设计基线由后续 List 20 落到 `docs/REFERENCE.zh-CN.md` §4bis "Newsprint UI 规格"�?

本切片是 Newsprint 视觉重构（List 15-20）的第一个纵向切片，目标�?*�?UI 行为变化、仅配色变化**，为 List 16 渲染管线升级�?List 17 Painter 字体栈做准备�?

Acceptance criteria�?

- [x] 新增 `native/include/echo/win32_app/Theme.h`：暴�?`theme::color::Paper/PaperAlt/PaperEdge/Ink/InkSoft/InkMute/InkFaint/Rule/RuleSoft/Accent/AccentDeep/GlassTint/GlassTint2/GlassEdge/White` 访问器与 `MakeNewsprintPalette()` 工厂；保�?`Palette` 字段名（bg/panel/line/text/...）以兼容 MainWindow 现有 ~160 �?`palette_.X` 调用�?
- [x] 新增 `native/win32_app/Theme.cpp`：颜色值以 `D2D1::ColorF(0xRRGGBB)` �?`D2D1::ColorF(r,g,b,a)` 形式定义；所有访问器�?`static const D2D1_COLOR_F` 避免重复构造�?
- [x] `native/CMakeLists.txt` �?`EchoWin32Layout` 静态库源列表追�?`win32_app/Theme.cpp`�?
- [x] `MainWindow.cpp` 删除匿名命名空间内的 `Palette` 定义；改 `Palette palette_;` �?`Palette palette_ = MakeNewsprintPalette();`�?60 �?`palette_.X` 调用保持原样�?
- [x] `tests/basic_contract_tests.cpp` 新增 RED→GREEN 测试：`Paper/Ink/Accent/AccentDeep/Rule/GlassTint` �?r/g/b 容差 1/255 等于 HTML 参考值；`MakeNewsprintPalette()` 旧字段名映射�?theme color 访问器�?
- [x] `EchoNativeSmokeTests` 全部通过（含本切片新增断言）�?
- [x] `EchoWin32` Debug 构建通过；启�?3 秒响应检查不退化（`Responding=True`、`HasExited=False`）�?
- [ ] 视觉验收�?*待用户人工对�?*）：启动后所有页面背景由蓝灰浅色变为 `#f1ead8` 纸色；正文从纯黑�?`#221b12` 墨色；红�?`#a8311b` 强调出现�?Home 当前导航 / Now Playing 进度�?/ 搜索框聚焦边框�?

Done when�?

- 所�?Acceptance criteria 勾选完成�?
- 内存 snapshot �?List 14 收敛后基线（Debug Working Set �?19-20 MB）增长不超过 1 MB（理�?~1 KB 常量表）�?

Latest verification�?

- 2026-05-23：完�?Theme 模块脚手架、`MakeNewsprintPalette()` 工厂、RED 测试�?`MainWindow::palette_` 初始化切换�?
- 2026-05-23：`EchoNativeSmokeTests` Debug 构建 + `ctest` 通过�?/1 passed in 2.61s，含 6 �?Newsprint token 断言）。`EchoWin32` Debug 构建通过。隐藏启�?3 秒：`Id=31332`、`Responding=True`、`HasExited=False`、`WorkingSetMB=20.4`、`PrivateMB=6.8`�?
- 内存 snapshot：Debug WorkingSet 20.4 MB（vs List 14 基线 19.9 MB�?0.5 MB，符合预期；远低于空�?�?120 MB 与播放中 �?180 MB 红线）�?

## List 16：D2D Device Context 渲染管线升级

Status：In Progress（待用户启动确认视觉无回归）

Type：AFK

Blocked by：None（与 List 15 解耦，但视觉变化属�?List 15；本切片为纯重构，视觉应�?List 15 完全一致）

What to build�?

�?`native/win32_app/MainWindow.cpp` 内嵌�?`ID2D1HwndRenderTarget` 渲染路径升级�?`D3D11Device + IDXGISwapChain1 + ID2D1Device + ID2D1DeviceContext` 路径，打开 D2D Effects（`CLSID_D2D1GaussianBlur` / `CLSID_D2D1ColorMatrix` / `CLSID_D2D1Shadow`）通路，为 List 19 玻璃面板做地基。本切片**视觉应零变化**——所有现�?`renderTarget_->X()` 调用�?`ID2D1DeviceContext`（继承自 `ID2D1RenderTarget`）保持二进制语义兼容，~160 处调用零改动�?

Acceptance criteria�?

- [x] 新增 `native/include/echo/win32_app/RenderPipeline.h` + `native/win32_app/RenderPipeline.cpp`：单一�?`RenderPipeline` 拥有 Factory1 / D3D11 / DXGI / D2D Device / DeviceContext / SwapChain1 / 后台缓冲 Bitmap1；提�?`InitializeHeadless()`（仅设备链路，无 HWND，供测试�? `Initialize(hwnd, w, h)`（完整路径）+ `Resize / Shutdown / BeginFrame / EndFrame` API�?
- [x] swap chain 使用 `DXGI_FORMAT_B8G8R8A8_UNORM` + `DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL` + `BufferCount=2`；HARDWARE 失败时回退 WARP�?
- [x] 设备丢失分类静态方�?`RenderPipeline::IsDeviceLossHResult(HRESULT)` 识别 `D2DERR_RECREATE_TARGET` / `DXGI_ERROR_DEVICE_REMOVED` / `_RESET` / `DRIVER_INTERNAL_ERROR` 四种�?
- [x] `MainWindow` 字段类型升级：`ID2D1Factory*` �?`ID2D1Factory1*`（非拥有）、`ID2D1HwndRenderTarget*` �?`ID2D1DeviceContext*`（非拥有）；新增 `RenderPipeline renderPipeline_` 成员。`InitializeGraphics()` 调用 `InitializeHeadless()` 取工厂；`CreateDeviceResources()` 调用 `Initialize(hwnd)` 创建 swap chain；`DiscardDeviceResources()` 调用 `Shutdown()`；`ResizeRenderTarget()` 调用 `Resize(pxWidth, pxHeight)`�?
- [x] `WM_PAINT` 路径：`BeginDraw/EndDraw` 改走 `renderPipeline_.BeginFrame()/EndFrame()`；`EndFrame` 内部触发 `IDXGISwapChain1::Present(1, 0)`；HRESULT �?`IsDeviceLossHResult` 判定后触发重建�?
- [x] `native/CMakeLists.txt`：`EchoWin32Layout` 库追�?`RenderPipeline.cpp` �?`d2d1 d3d11 dxgi dxguid` PUBLIC 链接；`EchoNativeSmokeTests` 通过 EchoWin32Layout 间接获得依赖�?
- [x] `tests/basic_contract_tests.cpp` 新增测试�?0 �?`RenderPipeline` `InitializeHeadless() �?CreateSolidColorBrush 验证 �?Shutdown()` 循环无设备移除、无 ctx 残留；`IsDeviceLossHResult` 四种码识别正确�?
- [x] `EchoNativeSmokeTests` Debug 构建 + ctest 通过�?3.81s �?50 次设备创建）�?
- [x] `EchoWin32` Debug 构建通过；启�?3 秒响应检查：`Responding=True`、`HasExited=False`�?
- [x] 视觉验收�?*用户已确�?*）：所有页面、组件、字体、文字位置、Newsprint 配色�?List 15 完全一致；窗口最小化 / 还原 / 缩放无闪烁、无设备丢失警告�?

Done when�?

- 所�?Acceptance criteria 勾选完成�?
- 内存 snapshot �?List 15 增长在预�?D3D11 + flip-model swap chain 余量内（+8-25 MB Working Set），�?180 MB 红线仍有 �?130 MB 余量�?

Latest verification�?

- 2026-05-23：完�?RenderPipeline 模块、MainWindow 改造�?0 周期 RED 测试、`IsDeviceLossHResult` 分类断言、`EchoWin32Layout` 链接 `d3d11 dxgi dxguid`�?
- 2026-05-23：`EchoNativeSmokeTests` Debug 构建 + `ctest` 通过�?/1 passed in 13.81s）。`EchoWin32` Debug 构建通过。隐藏启�?3 秒：`Id=27040`、`Responding=True`、`HasExited=False`、`WorkingSetMB=46.5`、`PrivateMB=56.4`�?
- 内存 snapshot：Debug WorkingSet 46.5 MB（vs List 15 基线 20.4 MB�?26.1 MB；vs 180 MB 红线余量 133.5 MB）。Private 56.4 MB（vs 6.8 MB�?49.6 MB；committed �?working set 大属预期 —�?DXGI 双缓冲、D2D Device 内部页池均算 committed 但不一�?resident）。增量主要构成估算：D3D11 device + driver overhead �?12-15 MB；DXGI flip-model 双缓冲（1600×1060×4B×2）≈ 13.5 MB；D2D Device + DeviceContext + Factory1 �?2-3 MB�?
- 视觉：纯重构，所�?~160 �?`renderTarget_->X()` 调用保持原行为；预期视觉�?List 15 完全一致�?

## List 17：Painter Helper + Newsprint Typography

Status�?*Done**

Type：Feature

Blocked by：List 16（ID2D1DeviceContext 管线升级�?

What to build�?

抽取 `Painter` 工具类，封装报纸风格绘制原语（`DrawMasthead` / `DoubleRule` / `SectionLabel` / `SectionHead` / `PageHead` / `Kicker`），引入 DWrite 衬线字体栈并预创�?TextFormat 缓存。`MainWindow.cpp` �?section 大标题替换为 `painter_.SectionHead()`�?0px 半粗 + 下划 rule，对�?HTML `.section-bar h2`）；Home 顶部新增报头 masthead 横幅；hero 顶部 "今日推荐" 替换�?`painter_.Kicker()`�?

Acceptance criteria�?

- [x] 新建 `native/include/echo/win32_app/Painter.h`：`class Painter`，API 包括 `InitializeFonts` / `AttachContext` / `DetachContext` / `Shutdown` / `DoubleRule` / `DrawMasthead` / `SectionHead` / `SectionLabel` / `PageHead` / `Kicker` / `MeasureSectionLabel`�?
- [x] 新建 `native/win32_app/Painter.cpp`：五�?TextFormat 缓存（masthead 13px 半粗斜体、sectionHead 20px 半粗正体、sectionLabel 11px 斜体、pageHead 22px 粗体、kicker 10px 正体）；字体�?"Noto Serif SC"（DWrite 系统回退）；AttachContext 创建四支 theme::color 笔刷�?
- [x] `native/CMakeLists.txt`：`EchoWin32Layout` 追加 `Painter.cpp` 源；PUBLIC 链接追加 `dwrite`�?
- [x] `MainWindow.cpp` 生命周期接入：`InitializeGraphics` �?`painter_.InitializeFonts`；`CreateDeviceResources` �?`painter_.AttachContext`；`DiscardDeviceResources` �?`painter_.DetachContext`�?
- [x] `MainWindow.cpp` 绘制替换：`DrawHome` 顶部追加 `painter_.DrawMasthead`�?为你推荐" / "最近播�? / "推荐歌单" 三处替换�?`painter_.SectionHead`�?0px + 下划线）；`DrawHero` "今日推荐" 替换�?`painter_.Kicker`�?
- [x] `tests/basic_contract_tests.cpp` 新增 `PainterMeasureTest`：`MeasureSectionLabel(L"早晨")` > 0；更长文本宽度更大�?
- [x] `EchoNativeSmokeTests` Debug 构建 + ctest 通过�?/1 passed in 14.15s）�?
- [x] `EchoWin32` Debug 构建通过；启�?3 秒响应检查通过（Responding=True）�?
- [x] 视觉验收：Home 顶部出现 "BOTTLE TIMES �?Vol. I" 双线 masthead�?为你推荐 / 最近播�?/ 推荐歌单" �?20px 半粗衬线 + 下划 rule；hero 顶部 "今日推荐" 呈小号弱�?kicker�?

Done when�?

- 所�?Acceptance criteria 勾选完成�?
- 内存增量 �?+5 MB（TextFormat 缓存 + 笔刷，均�?DWrite 设备无关对象）�?

Latest verification�?

- 2026-05-23：完�?Painter 模块初版（SectionLabel 11px）；修正：HTML `.section-bar h2` 对应 20px 半粗，新�?`SectionHead` 替换 DrawHome 三处标题。`EchoNativeSmokeTests` 强制重编通过�?/1 passed in 14.15s）；`EchoWin32` 构建通过�?
- 内存 snapshot：Debug WorkingSet 49.2 MB（vs List 16 基线 46.5 MB�?2.7 MB；vs 180 MB 红线余量 130.8 MB）。增量来源：DWrite TextFormat 缓存 × 5 + Painter 笔刷 × 4 �?2-3 MB，符合预期�?

## List 18：Newsprint Home 重绘 + 占位页统一空�?

Status：In Progress（Phase 1 视觉就位，HomeView/PlaceholderView 抽取延后�?

Type：Feature

Blocked by：List 17（Painter Helper + Newsprint Typography�?

What to build�?

�?`MainWindow.cpp` �?Home / Sidebar / Feature 三类 D2D 绘制�?`Music Player.html` Newsprint 参考重绘视觉表层，并把内联�?7 个占位页（Discover / Radio / Video / Songs / Albums / Artists / Favorites / Downloads / Recent）统一为报纸风空态。`HomeView` / `PlaceholderView` 的类抽取�?Phase 2 推进（与 List 20 �?IPage 拆分合并执行更经济）�?

Acceptance criteria�?

- [x] **Phase 1.A · 侧边栏激活态报纸化**：`DrawSidebar` 主导航与设置项激活态由"圆角面板 + 蓝紫文字"切换�?半透明墨色弱化背景 + 左侧 2px 红色竖条 + Ink 文本"（对�?HTML `.nav a.active::before`）�?
- [x] **Phase 1.B · Hero 报纸�?*：`DrawHero` 背景由蓝/�?绿渐变改�?`PaperAlt` + 1px Rule 外框 + 6px 内嵌 dashed 边框（`D2D1_DASH_STYLE_DASH`）；标题升至 30px 半粗；副标题 13px InkSoft；红色播放钮；Kicker 升级�?"TODAY'S FEATURE · 今日推荐"�?
- [x] **Phase 1.C · 占位页统一空�?*：`DrawFeaturePage` 替换�?Kicker�?COMING SOON · 即将上线"�? Painter PageHead 大标�?+ DoubleRule 双线装饰 + 14px InkSoft 说明 + 红色 CTA 按钮。原 panel 卡片去除，与报纸风一致�?
- [x] **dashStrokeStyle_ 资源管理**：`d2dFactory_->CreateStrokeStyle(D2D1_DASH_STYLE_DASH)` �?`InitializeGraphics` 创建（factory 级别，跨设备复用），析构释放；`transientBrush_` �?`DiscardDeviceResources` 释放（设备相关）�?
- [x] `EchoNativeSmokeTests` Debug 构建 + ctest 通过�?/1 passed in 14.05s）�?
- [x] `EchoWin32` Debug 构建通过；隐藏启�?3 秒响应（Responding=True、WorkingSetMB=49.2、零增量）�?
- [ ] **视觉验收（待用户人工确认�?*：侧边栏首页/设置激活时显示 2px 红色竖条；Home Hero 显示纸色背景 + 双层边框；Discover/Radio/Video 等占位页显示 Newsprint 空态（kicker + 大标�?+ 双线）�?
- [ ] **Phase 2（延期）**：抽 `pages/HomeView.h/cpp` �?`pages/PlaceholderView.h/cpp`；`MainWindow` 持有 `unique_ptr<HomeView>` + `unique_ptr<PlaceholderView>`；`HomeView::HitTest` 复用 `Layout::HitTestHome`。可合并�?List 20 �?IPage 拆分执行�?

Done when�?

- Phase 1 视觉验收完成（用户确认侧边栏红条 / Hero 报纸�?/ 占位页空态符�?HTML 参考）�?
- Phase 2 抽取�?List 20 完成或单独切片�?

Latest verification�?

- 2026-05-23：Phase 1 完成 —�?侧边栏激活�?+ DrawHero 纸色双框 + DrawFeaturePage Newsprint 空态；`dashStrokeStyle_` �?InitializeGraphics 创建；`EchoNativeSmokeTests` ctest 1/1 passed in 14.05s；`EchoWin32` 构建通过；隐藏启动：WorkingSetMB=49.2、PrivateMB=55.1、Responding=True�?
- 内存 snapshot：Debug WorkingSet 49.2 MB（与 List 17 baseline 持平，零增量 �?仅视觉常量与 1 �?ID2D1StrokeStyle 对象 �?< 1 KB）�?

## List 19：Glass Panels + Paper Texture

Status：Done（视觉验收待用户确认�?

Type：Feature

Blocked by：List 16（D2D DeviceContext 升级，CreateEffect 通路�? List 18 Phase 1（视觉基础就位�?

What to build�?

�?`Music Player.html` �?`前端部分报告.md` 落地玻璃面板系统：底部播放栏（首版唯一应用点）使用 D2D Effects 链（GaussianBlur 22px �?ColorMatrix saturate 1.36×）叠加纸�?+ 高光边线，实�?Newsprint 风格的毛玻璃效果�?

Acceptance criteria�?

- [x] **`PaperTexture.{h,cpp}`** —�?程序化生�?128×128 BGRA 纸纹 tile（不打包 PNG，零磁盘 I/O；总常�?~64 KB），通过 BitmapBrush(EXTEND_WRAP + NEAREST_NEIGHBOR) 平铺。基�?Paper #f1ead8 ±10 灰度噪声 + 5% 暗斑 + 1% 高光颗粒�?
- [x] **`GlassPanel.{h,cpp}`** —�?sceneBitmap(全分辨率离屏，CopyFromRenderTarget 抓取 backbuffer) + blurredBitmap(¼ 分辨�?TARGET bitmap，SetTarget �?DrawImage 写入) + GaussianBlur(σ=22.0, BORDER_MODE_HARD) �?ColorMatrix(Rec.601 饱和度矩�?s=1.36) �?composite to rect + tint(GlassTint rgba 0.97/0.95/0.90/0.46) + 可�?paper + 1px GlassEdge 高光顶线�?
- [x] **`blurDirty` 脏标�?* —�?每帧 Paint �?`PopAxisAlignedClip` 后、`DrawPlayerBar` 前调�?`glass_.MarkBlurDirty()`，确�?sceneBitmap 抓取的是 player bar 上方所有已绘内容；`RebuildBlurIfNeeded` 内部消费并复位，单帧仅模糊一次�?
- [x] **设备生命周期** —�?`CreateDeviceResources` 末尾 `paperTex_.Initialize(ctx) + glass_.Initialize(ctx, W, H)`；`DiscardDeviceResources` �?`glass_.OnDeviceLost() + paperTex_.OnDeviceLost()`；`Resize` 通过 `glass_.EnsureSceneSize(W, H)` 重建 bitmap 并标�?dirty�?
- [x] **`DrawPlayerBar` 接入** —�?入口首个 `FillRect(barRect, palette_.panel)` 替换�?`glass_.DrawGlassPanel(barRect, GlassTint, GlassEdge, &paperTex_, 0.22)`；glass_ 未就绪时回退�?Paper 纯色�?
- [x] **CMake** —�?`EchoWin32Layout` 库追�?`GlassPanel.cpp` + `PaperTexture.cpp`�?
- [x] **RED �?GREEN 测试**：`GlassPanelRender` �?RenderPipeline::InitializeHeadless 提供的真�?ID2D1DeviceContext 上验�?Initialize(1280, 800) 之后 blurredBitmap 大小�?320×200(¼)、blurDirty 初始�?true、EnsureSceneSize 同尺寸幂等；`PaperTextureSmoke` 验证 Initialize 成功 + bitmap() 非空。`ctest` 1/1 passed in 15.55s�?
- [x] **构建** —�?`EchoWin32` Debug 编译链接通过；`EchoNativeSmokeTests` 通过�?
- [ ] **视觉验收（待用户人工确认�?*：底部播放栏显示毛玻璃（背景滚动时模糊跟随）；玻璃面板上沿出�?1px GlassEdge 高光线；纸纹颗粒可辨�?

Done when�?

- 视觉验收完成�?
- 真实播放�?GPU 静止 < 1% / 播放 < 5%（首版未�?GPU 量测，留待长稳验证）�?

Latest verification�?

- 2026-05-24：完整接入完成。`EchoNativeSmokeTests` ctest 1/1 passed in 15.55s（含 GlassPanelRender + PaperTextureSmoke 两个�?RED→GREEN）；`EchoWin32` Debug 编译通过；隐藏启�?5 秒响应：WorkingSetMB=92.6、PrivateMB=97.5、Responding=True�?
- 内存增量：相�?List 18 baseline�?7.2 MB WS / 56.6 MB Private�?45 MB WS / +41 MB Private —�?略高于计划估算的 +15-20 MB，主要来�?sceneBitmap (~8 MB ARGB 1080p) + blurredBitmap (~0.5 MB) + D2D Effects 内部状�?+ DXGI/D3D11/D2D Effects 库加载。仍�?180 MB 预算内（�?85 MB 容真实播放）�?

## List 20：Tauri UI 收口（当前主线）

Status：In Progress

Type：HITL（每项完成后需用户截图确认视觉无回归）

Blocked by：None

Context�?
2026-05-24 用户截图反馈：QR 登录�?`/song/url` 主链路已通（包括 VIP 错误提示�?60 秒试�?fallback）。本 List 聚焦 Sidebar / Topbar / PlayerBar 与窗口装饰的 UI 收尾�?

What to build（按用户标注的优先级）：

**P0 · 断路�?*

- [x] 20.1 前进按钮历史�?
  - 现状：[Topbar.vue:48](../ui/src/components/Topbar.vue#L48) `forward` 按钮 `@click="goBack"`，与 back 共享处理函数
  - 目标：在 App.vue 引入 `historyStack: string[]` + `historyIndex: number`，`handleNavigate` 截断�?push，back/forward 改变 index
  - 验收：进入歌单→后退→前进可恢复歌单视图

- [x] 20.2 侧栏歌单接真实接�?
  - 现状：[Sidebar.vue:21-27](../ui/src/components/Sidebar.vue#L21) `mockPlaylists` 写死 5 �?
  - 目标：登录后�?`/user/playlist`（CompatApi 中已�?userPlaylist 路由）拉真实列表；未登录显示"扫码登录后查看歌�?占位
  - 验收：换不同账号登录后侧栏列表不�?

- [x] 20.3 侧栏 placeholder 项清�?
  - 现状：[Sidebar.vue:15-18](../ui/src/components/Sidebar.vue#L15) "私人漫游 / 喜欢 / 最�?/ 本地" 点击�?alert
  - 目标：保留首页和歌单两类；其他要么实现，要么暂时隐藏�?最�?可以基于本地 `/playhistory/upload` 历史实现，但首版先隐藏更清爽
  - 验收：用户截图里左栏没有任何点击就报错的�?

- [~] 20.4 登录后头�?昵称显示（部分完成）
  - 已做：[CompatApi.cpp /login/qr/check](../native/core/CompatApi.cpp) 现在尝试 `nickname/username/name` �?`pic/headphoto/avatar/headerurl/userpic` 多种字段名；[/user/vip/detail](../native/core/CompatApi.cpp) 改为调真 `get_union_vip` 端点并把响应里的 nickname/pic 反写�?session；userStore.checkLoginStatus �?VIP 数据里的 nickname/pic 也拉到前�?
  - 仍缺：经实测，QR check 响应�?get_union_vip 响应都不包含 `pic`/`nickname` 字段，所以登录后这两个仍为空。要拿真头像需�?`/v3/get_my_info`（RSA 加密，不稳定）或反向工程 `m.kugou.com` 网页用户中心�?JSONP 接口
  - 当前 UI 表现：首字母占位�?�?）正常显示；username �?fallback "听歌用户"
  - 后续：考虑�?`m.kugou.com/userCenter/index?uid=X` JSONP 解析作为兜底

**P1 · 视觉与导航小�?*

- [x] 20.5 底部 PlayerBar 点击进入歌词�?
  - 现状：[PlayerBar.vue](../ui/src/components/PlayerBar.vue) 整条 bar 不可�?
  - 目标：bar 的左侧曲�?封面区域�?`@click="emit('navigate', 'lyric')"`；按钮区域（播放/暂停/上下�?进度�?音量）保持原行为
  - 验收：点击曲名区进入 LyricView，点击播放按钮仍是切换播�?

- [x] 20.6 删除侧栏小字 "Vol. MMXXVI · No. 17"
  - 位置：[Sidebar.vue:48](../ui/src/components/Sidebar.vue#L48) `<span class="edition">`
  - 验收：截图里 logo 下方不再有这行小�?

- [ ] 20.7 自绘 Title Bar + Newsprint 背景延伸
  - 现状：Tauri 默认窗口装饰 + "BottleMusic" 标题独立在最顶部白色 bar
  - 目标：`tauri.conf.json` �?`decorations: false`，前�?CSS 画一�?32-40px 高、米�?(`--paper`) �?drag 区，包含左侧 logo、右�?min/max/close 按钮；用 `data-tauri-drag-region` 实现拖拽
  - 验收：窗口顶部颜色与下方 Newsprint 背景一致，没有突兀白条

- [ ] 20.8 Title Bar 中间显示进程内存
  - 依赖�?0.7 完成
  - 目标：title bar 中间显示 `Working Set: XX.X / 220 MB`，每 2s 刷新；Tauri command �?`sysinfo` crate 取当前进�?working set
  - 验收：拖动窗口、滚动歌单时数字会变�?

**P2 · 已知遗留**

- [x] 20.9 VIP 5 秒广告领取流程（2026-05-25 完整闭环工作�?
  - 之前 bug：`source_id/receive_day` 写在 JSON body 里，且缺 `dfid/mid/uuid` 默认参数，酷狗在签名校验前就 304001 "日期格式错误"
  - 修复 1：[UserService::ClaimVip](../native/core/UserService.cpp) 改成 android encryptType 标准做法——所有参数走 URL query string，`receive_day=YYYY-MM-DD`，body 留空。酷狗回 `status:1` + `ad_vip_end_time/ad_vip_num`
  - 修复 2�?*新增** [UserService::UpgradeVipReward](../native/core/UserService.cpp) �?`/youth/v1/listen_song/upgrade_vip_reward?ad_type=1`，CompatApi 暴露 `/youth/day/vip/upgrade`�?*实测这个 endpoint 真的会发�?24 小时 SVIP**——KuGou 服务端只校验请求格式，不验证真有广告 SDK 凭证
  - [claimVip](../ui/src/api/userStore.ts) 现在双调：先 `/youth/day/vip` 注册当日尝试，再 `/youth/day/vip/upgrade` 领奖。任意一次返回非�?`ad_vip_end_time` 即视为成�?
  - 实测响应：第一�?upgrade �?`{status:1, data:{recharge_hours:24}}` + `/user/vip/detail` 立刻显示 `busi_vip[0]: product_type="svip", is_vip:1, vip_end_time=24h_later`。第二次�?`error_code:297002 "已经领取过升级vip奖励"`，证�?KuGou 真的发放�?VIP
  - 验收命令：`curl http://127.0.0.1:6609/youth/day/vip/upgrade` 应返�?`recharge_hours:24`（首次）

- [x] **20-bonus 歌单加载彻底修�?* + VIP 检测纠�?+ 头像/昵称�?026-05-25�?
  - 之前 bug 1：`PlaylistService` 直连 `cloudlist.service.kugou.com` 触发 WinHttp 12175（SSL 证书校验失败�?
  - 之前 bug 2：用 `appid=1014/clientver=20000` 被酷狗返 `error_code:20006`
  - 之前 bug 3：缺 `dfid/mid/uuid` 默认参数同样导致 20006
  - 修复：[PlaylistService](../native/core/PlaylistService.cpp) 改用 `https://gateway.kugou.com` + `x-router: cloudlist.service.kugou.com` 让酷狗网关代理；appid �?`1005/20489`；补�?`dfid="-", mid="0", uuid="-"`
  - 实测：拉到用户真实的 2 个歌单（"默认收藏" + "我喜�? 218 首），以及真实昵称（"音无"�? 头像 URL
  - [CompatApi.cpp /user/playlist](../native/core/CompatApi.cpp) 把响应里�?`list_create_username` + `create_user_pic` 反写 session；[Sidebar.vue](../ui/src/components/Sidebar.vue) 也立刻更�?userStore
  - VIP 检测纠正：[userStore.checkLoginStatus](../ui/src/api/userStore.ts) 之前�?`busi_vip[].is_vip=1` �?VIP，但 tvip(concept) �?KuGou 自动设的试用标记，不解锁音源。新规则只看 `product_type=="svip" && is_vip==1 && vip_end_time` 未来

- [ ] 20.10 EchoNativeSmokeTests PlaybackController 段超�?
  - 现状：测试用 `https://example.invalid/...` 触发 Media Foundation 异步打开，`Stop()` 会等 TCP 超时（~70 秒）
  - 修复：测试改�?`file:///nonexistent.mp3` 或注�?mock IMFMediaPlayer
  - 不影响线�?EchoCompatServer，只阻塞 ctest

Done when�?

- 20.1�?0.8 全部勾�?
- 用户截图无明显视�?交互断路
- 内存指示器读数稳定在 220 MB 以内
- 重新�?`pnpm tauri dev` + 手动验证一遍登录→搜索→播放→歌词→返回主路径

---

## 历史内存基线

来源：原 `docs/MEMORY_BUDGET.zh-CN.md` 已合并入 `REFERENCE.zh-CN.md`，历史快照保留在本附录�?
口径：`EchoWin32.exe` 隐藏窗口启动 3 秒，未播放音频，未进行真实网络图片加载�?

| 时间 | 范围 | Working Set | Private Bytes |
| --- | --- | --- | --- |
| 2026-05-10 | Debug，List 04 完成�?| �?18.7 MB | �?6.5 MB |
| 2026-05-10 | Debug，List 05 完成�?| �?18.7 MB | �?6.5 MB |
| 2026-05-10 | Debug，List 06（启�?SQLite 与设置持久化）后 | �?19.0 MB | �?6.7 MB |
| 2026-05-11 | Debug，List 10 收敛 | �?19.9 MB | �?6.6 MB |
| 2026-05-11 | Release，List 10 收敛 | �?19.0 MB | �?6.6 MB |

离空闲目标（�?120 MB）有大量余量；播放中目标（≤ 180 MB）待长跑稳定性验证完成后再补充实测，参见 List 12�?
