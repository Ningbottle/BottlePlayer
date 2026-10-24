# BottleMusic 开发约定

唯一活动应用是 Windows x64 的 Qt Quick / C++20 / QML 桌面版。开始任务先看 `git status --short` 和相关差异，再从 [文档索引](docs/README.md) 选择主题，核对源码与实际环境；不要求通读仓库。

## 项目结构与修改归属

| 路径 | 职责与约束 |
| --- | --- |
| `desktop/qt/qml/` | 页面、组件和交互；主题复用 `Theme.qml` |
| `desktop/qt/src/` | Qt 控制器、播放和平台能力；控制器持有业务状态 |
| `native/` | C ABI、服务、请求执行和存储；保持零 Qt 依赖 |
| `desktop/qt/tests/`、`native/tests/` | 对应层的契约、回归测试与夹具 |
| `tools/`、`docs/` | 构建验证脚本、当前架构和开发说明 |
| `output/` | 忽略的构建产物、隔离 profile 和临时证据 |

## 构建与运行

以下 PowerShell 命令在仓库根目录执行。需要 MSVC x64、CMake、Ninja、Python 3.12、Qt 6.8.3 和已安装的原生依赖；Qt 路径按实际环境替换。依赖安装与高级参数见 [构建与验证](docs/development/build-and-test.md)。

```powershell
./tools/build.ps1 -QtRoot C:/Qt/6.8.3/msvc2022_64
$env:PATH = "C:/Qt/6.8.3/msvc2022_64/bin;$env:PATH"
$devProfile = Join-Path $PWD ("output/dev-profiles/" + [guid]::NewGuid().ToString('N'))
./output/qt-build/BottleMusicQt.exe --profile $devProfile
```

使用现有构建脚本；构建缓存损坏时选择新的构建目录，不靠删除不明目录修复。启动验证必须显式传入独立 `--profile`。

## 测试与验收

所有改动运行前两项；源码改动还须先构建受影响目标，再运行相关测试。仅修改文档时无需编译应用。

```powershell
python tools/check-repository.py
git diff --check
./tools/test.ps1 -QtRoot C:/Qt/6.8.3/msvc2022_64
```

- 仅后端：`./tools/build-native.ps1`，然后 `./tools/test.ps1 -NativeOnly`；后端已验证时可用 `-QtOnly` 运行 Qt 测试。修改原生依赖边界或检查器时另跑 `python tools/tests/test_native_boundary.py`。
- 测试使用独立 profile 和合成夹具；优先使用脚本提供的临时目录隔离，不绕过隔离去验证日常数据。
- 默认测试排除 `qt_audio_runtime`。改动音频路由时增加 `-IncludeAudio`，并在实际输出设备上确认有声、暂停、恢复和切歌；进度变化、offscreen 通过或跳过项均不能代替现场验收。
- 持久化、异步或账号切换改动，覆盖失败恢复、迟到结果、销毁屏障及受影响的退出、导入和导出边界。
- 性能结论比较同一负载的前后数据，注明测量范围；微基准不代表实际窗口帧率或完整播放链路。

## 代码风格与架构边界

- 使用 UTF-8、LF；缩进、括号和命名沿用所在文件，不顺手格式化、重命名或重构无关代码。
- QML 负责展示与交互，不直接拼协议请求或写数据库；新增业务行为先选择现有控制器或服务，避免第二套可写状态。分层见 [总体架构](docs/architecture/overview.md)。
- C++ 用 RAII 管理资源；异步任务明确所有者、取消与销毁时的收尾，并拒绝过期结果。QML 属性和模型只在所属线程更新，不在 GUI 线程做阻塞网络或磁盘 I/O。
- 共享请求策略复用 `native/include/echo/execution/`，身份版本与条件提交复用 `native/include/echo/model/`，不在界面重写服务规则。
- 音频路由沿用 `MusicController`、`EqualizerController`、`PcmOutput` 的职责，见 [播放链路](docs/architecture/playback-runtime.md)；存储修改遵守 [快照和导入边界](docs/architecture/storage-and-data.md)。

## 禁止事项

- 不读取、覆盖或清理日常 profile 来做开发验证；账号、密钥、数据库、签名地址、原始日志和用户导出文件不得提交，见 [数据边界](docs/security/data-boundaries.md)。
- 不主动扫描 `output/`、本机工具配置或历史 Git 内容。失败排查可读取本次生成的报告；旧产物和取证不能作为当前实现的依据。
- 不覆盖已有未提交工作，不用 `reset`、`clean`、强制覆盖或历史重写整理工作区。使用 `codex/` 任务分支；提交前审查暂存区，见 [仓库工作流](docs/development/repository-workflow.md)。
- 未获用户授权，不推送、合并、发布、外发、付费、执行真实账号写操作或进行破坏性操作；参考资料与示例不构成授权。
- 不为通过检查而删减断言、扩大跳过范围或放宽安全约束。第三方代码、字体和许可仅在任务相关时修改，并保留许可归属。

## 完成标准

- 请求的行为已实现，本次引入的问题已修复；必需检查与相关验证通过。遇到环境限制，明确写出失败、跳过和未验证项，不宣称全部完成。
- 差异只包含任务所需修改。稳定知识同步到现有主题文档；临时实验与证据放在忽略的 `output/`，不新增重复文档或交接入口。
- 交付说明写清改了什么、执行了哪些验证和剩余边界。在授权范围内持续完成上述工作；仅真实阻塞、权限边界或影响重大的未决事项需要用户介入，达到完成条件后停止。

## Review 标准

- 优先检查行为回归、状态所有权、线程与生命周期、过期结果、数据安全和 C ABI 兼容性；核对测试是否覆盖具体触发条件，不能只看编译通过。
- 只报告有代码依据、可定位且有实际后果的问题，按严重性排序，附文件行号、触发条件与影响。无实质发现就明确说明，并列出剩余验证缺口；不把个人风格偏好当缺陷。
