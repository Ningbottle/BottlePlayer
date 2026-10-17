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

脚本自动加载 MSVC 环境，并在 CMake 和 Ninja 启动前设置 UTF-8 控制台，防止本地化 `/showIncludes` 前缀损坏导致头文件依赖漏跟踪。遇到旧缓存前缀损坏时使用新的构建目录。`tools/build-qt.ps1` 是已具备原生运行库时的高级入口，可单独指定 `-ShaderToolsRoot` 和 `-NativeDirectory`。

## 自动测试

```powershell
python tools/check-repository.py
./tools/test.ps1 -QtRoot C:/Qt/6.8.3/msvc2022_64
git diff --check
```

默认运行原生 CTest 和 Qt 契约测试，排除依赖现场音频设备的 `qt_audio_runtime`。契约测试使用临时目录，不访问日常 profile。
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
