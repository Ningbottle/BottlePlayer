# BottleMusic 长期工作 List

本文是 BottleMusic 后续长期开发的执行队列。它的目标是让 agent 在长时间任务中不跑偏：完成一个 list 后自动进入下一个 list；每个 list 都按 TDD tracer bullet 推进；上下文压缩时只保留和本项目有关的事实。

## 工作循环

每个 list 必须遵循同一套循环：

1. 读取 `AGENTS.md`、`docs/README.zh-CN.md`、本文和当前 list 相关文档。
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

Status：Done

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
- [ ] 启动可视窗口后手动确认这些交互路径。

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
- [x] 内存目标和实测值写入 `MEMORY_BUDGET.zh-CN.md`。
- [x] 已知缺口清晰列出。

Latest verification：

- 2026-05-11：新增 `native/CMakePresets.json`，验证 `cmake -S native --preset bottlemusic-check` 可配置 Debug 构建。
- 2026-05-11：Debug 构建 `EchoNativeSmokeTests`、`EchoWin32`、`EchoCompatServer` 通过，`ctest` 1/1 通过。
- 2026-05-11：Release 构建 `EchoNativeSmokeTests`、`EchoWin32`、`EchoCompatServer` 通过，`ctest` 1/1 通过。
- 2026-05-11：空闲启动 3 秒内存采样：Debug Working Set 约 19.9MB / Private Bytes 约 6.6MB；Release Working Set 约 19.0MB / Private Bytes 约 6.6MB。
- 2026-05-11：README 已补充 Debug/Release 构建、运行兼容服务、启动 Win32 UI 和当前已知缺口。
