# BottleMusic

BottleMusic 是面向 Windows 10/11 x64 的 **酷狗概念版 PC 非官方客户端**，主打 Newsprint 报纸风视觉与 ≤ 220 MB 内存预算。

酷狗概念版没有官网、没有官方 PC 端；本项目目标是在 PC 上提供非官方的酷狗概念版体验。

- **前端**：Tauri 2.0（Rust + WRY WebView2）+ Vue 3 + Vanilla CSS（无 CSS 框架）
- **后端**：C++ EchoCAPI.dll（FFI 直注）+ EchoCore / EchoStorage / EchoDiagnostics
- **构建**：Vite 6 + pnpm 11 + CMake + MSVC C++20 + Cargo

## 目录结构

```
BottleMusic/
├── ui/         ← Tauri 2.0 + Vue 3 前端（src-tauri/ 含 Rust FFI 层）
├── native/     ← C++ EchoCAPI.dll 后端（EchoCore / EchoStorage / EchoDiagnostics）
├── server/     ← KuGouMusicApi 参考实现（接口对照来源，submodule）
└── docs/       ← 本地项目文档（不进 Git）
```

## 快速开始

```powershell
# 1. 先编译 C++ DLL（VS Developer PowerShell，仅首次或改 native/ 后需要）
cmake --build native/out/bottlemusic-check --target EchoCAPI

# 2. 前端开发（DLL 会由 build.rs 自动拷入 target/debug/）
cd ui
pnpm install
pnpm tauri dev          # 首跑 5-10 分钟，之后 < 30s
```

> **注**：`pnpm tauri dev` 的终端窗口即日志来源，C++ 诊断输出格式为 `[C++ debug][Tag] message`。

## 当前状态

**已打通的核心链路**：
- 扫码登录 → 用户信息 / VIP 状态 / 头像昵称
- 搜索 → 歌曲列表 → 解析播放 URL → 播放（含 VIP 歌曲）
- 用户歌单加载（收藏 / 自建歌单）
- 歌词获取与同步高亮
- 每日免费 VIP 领取（听歌/广告）→ 到期时间正确显示
- 播放队列与歌词防遮挡

**架构演进（已合并）**：

| 阶段 | 内容 |
|---|---|
| P1-P6 | 接口契约、诊断、内存预算、请求调度、日志脱敏 |
| M1 构建固化 | 删硬编码路径、清 sidecar 死代码、build.rs 确定性 DLL 拷贝（debug/release 分离） |
| M2 接缝固化 | `EchoInitializeWithPaths` 路径可控、`EchoSetLogCallback` 日志回调、`EchoSetEventCallback` ABI 预留、异常隔离 |
| M3 真并行 | `Database` 内置 mutex + `C_API` 换 `std::shared_mutex`（读并发/写独占）+ Rust `RwLock`；并发测试实测：20 线程 × 50 轮 × 2 请求（含 SQLite）全通过，无死锁 |
| M4 VIP | 设备指纹注入（`ResolveAndroidMid`）、`error_code` 130012 友好提示、前端成功判定修复、到期时间取最晚未过期的 `busi_vip[svip]`|

**已解决**（原"已知问题"）：
- ✅ VIP 领取链路：服务端本就正常发放，根因在前端把成功挂在响应里不存在的 `ad_vip_end_time` 上，已修正。
- ✅ 设备指纹：mid 经 `ResolveAndroidMid` 派生为 38–39 位 Android decimal，dfid 通过 `/register/dev` 正式注册。

**当前已知问题**：
- `PlaybackController`（Media Foundation）仅预留了 ABI 接口，播放核心尚未迁入 DLL；偶发切歌时 MFP 初始化失败。
- 图片缓存 `EchoImage` 磁盘/内存边界未加容量上限，长期运行可能膨胀。

## 技术参考

后端接口实现参照 [MakcRe/KuGouMusicApi](https://github.com/MakcRe/KuGouMusicApi)。
项目基线是酷狗概念版（appid=3116，Lite 盐），不把标准版参数作为默认事实。

## 免责声明

本项目仅用于个人学习和技术研究。音乐数据和版权归原平台及版权方所有，请尊重知识产权并支持正版音乐。
