# 构建与验证

支持环境为 Windows 10/11 x64、MSVC x64、CMake 3.24+、Ninja、Python 3.12 和 Qt 6.8.3。Qt 安装需要 Quick、QuickControls2、Concurrent、Multimedia、Network、Svg、ShaderTools、Sql、Test，以及 Quick/Gui private headers。字体和图标已经在源码内。

## 安装后端依赖

使用 vcpkg 安装 `native/vcpkg.json` 中的 nlohmann-json 和 sqlite3。以下命令在仓库根目录执行，`VCPKG_ROOT` 指向已有 vcpkg 工具：

```powershell
& "$env:VCPKG_ROOT/vcpkg.exe" install --triplet=x64-windows --x-manifest-root=native --x-install-root=native/vcpkg_installed
```

## 编译

```powershell
./tools/build.ps1 -QtRoot C:/Qt/6.8.3/msvc2022_64
```

此命令先构建 `output/native-build` 的后端和测试，再构建 `output/qt-build` 的 Qt 应用与测试，并复制原生运行库、字体、图标和许可。也可用 `-NativeBuildDirectory`、`-QtBuildDirectory` 选择新的输出目录。

只构建后端时不需要 Qt SDK：

```powershell
./tools/build-native.ps1
./tools/test.ps1 -NativeOnly
```

CI 先运行仓库检查和 Native 构建/测试，再安装 Qt 并构建界面。这使后端无 Qt 依赖成为可执行约束。

脚本自动加载 MSVC 环境，并在 CMake 和 Ninja 启动前设置 UTF-8 控制台，防止本地化 `/showIncludes` 前缀损坏导致头文件依赖漏跟踪。遇到旧缓存前缀损坏时使用新的构建目录。`tools/build-qt.ps1` 是已具备原生运行库时的高级入口，可单独指定 `-ShaderToolsRoot` 和 `-NativeDirectory`。

## 自动测试

```powershell
python tools/check-repository.py
python tools/tests/test_native_boundary.py
./tools/test.ps1 -QtRoot C:/Qt/6.8.3/msvc2022_64
git diff --check
```

默认运行原生 CTest 和 Qt 契约测试，排除依赖现场音频设备的 `qt_audio_runtime`。契约测试使用临时目录，不访问日常 profile。
已经验证后端时可用 `-QtOnly` 单独运行 Qt 契约。`qt_lyric_bindings` 直接加载真实歌词组件，检查进度更新不再读取整首歌词，并覆盖逐字时间、末行回退、点击定位和时钟启停。`qt_music_flows` 检查队列投影、索引/模式/等待通知、歌词及元数据更新，并包含 2000 首队列 getter 微基准；它不代表实际窗口帧率。

`qt_render_bindings` 检查实际封面请求尺寸与列表非零原点下的渐隐；`qt_diagnostics` 用阻塞 sink 验证入队与 I/O 分离、队列上限、错误预留、轮转及故障恢复。`qt_settings_migration` 覆盖连续侧栏修改合并、退出保存、保存失败重试和导入/导出屏障。相关 QtTest 报告均位于构建目录 `Testing/`，失败时由测试脚本打印本次报告。

`qt_navigation_interactions` 使用真实页面、临时 profile 和 Native 夹具，检查个人歌单首次加载失败后从侧栏进入目录刷新、轮播卡片的 Tab 与 Space/Enter 激活，以及像素/角度滚轮输入取消首页磁吸。测试不访问真实账号。

`qt_quality_interactions` 使用真实菜单、临时 profile 和本地音频服务，检查未选歌时预选音质、标签和保存、侧面面板与播放页入口、暂停时切换保留进度，以及清空队列后恢复完整选项。此测试验证选择与加载，不代表线上歌曲提供所有音质。

`qt_theme_interactions` 点击真实色调选项，验证浅色与深色下页面、侧栏和播放器同步变色，以及共享按钮、播放按钮在普通、悬停、按下状态的文字对比度。测试另覆盖自选黑白和高亮颜色、默认色调恢复、幕布搜索与沉浸控件，以及主题偏好的保存和重新加载。

`bottle_qt_smoke` 的 `render-review` 模式使用本机图片服务验证封面动画的尺寸稳定、导航取消、切歌取消、迟到图片及减少动画选项；可通过 `QT_SCALE_FACTOR=1.25` 验证分数缩放。字体视觉比较仍应在 Windows 图形平台下观察，offscreen 通过不代表实际 DirectWrite 和多屏环境已验收。

Windows 图形平台默认将原生文字绘制为灰度抗锯齿，使它与图层内文字保持一致；`BOTTLE_TEXT_AA=cleartype` 可临时恢复系统 ClearType 作同机对照。报刊界面的标题、正文及封面占位字都使用原生文字渲染：Qt 6.8 的曲线渲染在捆绑的中文衬线字形上会产生断笔，例如「山」的左竖。

`qt_text_raster` 使用独立 profile 和夹具，检查 28、37、48、64px 的「山」在深浅底色下保持笔画连通，并保存原生、曲线及距离场的 12–64px 对照图。它使用 Windows 图形平台和 [D3D 软件适配器](https://doc.qt.io/qt-6.8/windows-graphics.html)，让无独立 GPU 的 CI 也能截取实际 RHI 像素；空白或缺失截图会失败。可在 Windows 图形平台手动运行 `bottle_qt_smoke <fixture.dll> <新证据目录> text-raster` 复核实际显示效果。`font-layout` 模式截取报刊首页和圆环，并检查卡片尺寸与纵向留白。改变 `QT_SCALE_FACTOR` 时应核对报告中的实际 `dpr`；系统缩放与该变量会相乘。

Qt 的属性引用序列赋给局部变量后仍可能反复调用 C++ getter。歌词在内容更新时用 `slice()` 分离快照；测试允许此时逐项读取，约束后续时钟更新不再读取整首歌词。相关新测试把 QtTest 文本保存到 Qt 构建目录的 `Testing/qt_music_flows.txt` 和 `Testing/qt_lyric_bindings.txt`；测试脚本在失败时输出本次生成的报告，保留断言位置和微基准结果。
测试入口把 `TEMP`、`TMP` 和 `ECHO_NATIVE_DATA_DIR` 统一指向新的 `output/test-profiles/` 子目录，避免继承受限临时路径或默认应用数据目录；退出后恢复调用者环境。

连接目标输出设备后增加 `-IncludeAudio`。真实网络音频和推荐测试还需显式设置 `BOTTLE_LIVE_NATIVE_DLL`，这些检查不计入普通 CI 的已验证范围。查看测试输出中的跳过项，不能将“通过但跳过现场项目”描述为耳机或在线链路已验收。

QML 页面交互用 `bottle_qt_smoke`：

```powershell
output/qt-build/bottle_qt_smoke.exe output/qt-build/bottle_native_fixture.dll output/ui-feedback-check user-fixes
```

证据目录必须是新的隔离目录。夹具交互不应调用真实账户写操作；在线或真实账号验证需要相应授权。无声排查见 [播放链路](../architecture/playback-runtime.md)。

## 便携包

```powershell
./tools/package.ps1 -QtRoot C:/Qt/6.8.3/msvc2022_64
```

默认输出 `output/packages/BottleMusicQt-1.1.0-qt-preview-x64.zip` 与 SHA-256。脚本使用 `windeployqt` 部署运行库和 QML 插件，只复制程序、运行库、字体、图标与许可，不复制 profile。再次打包时选择新的 `-PackageDirectory`。

将包解压到全新目录，使用独立 `--profile` 验证启动、关闭、媒体输出和页面交互。构建成功不代表便携包已验收。GitHub 的手动打包工作流只保留 Actions 产物，不自动创建或发布 Release。
