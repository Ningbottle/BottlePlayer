# BottleMusic

BottleMusic 是面向 Windows 10/11 x64 的 **酷狗概念版 PC 非官方客户端**，主打 Newsprint 报纸风视觉与 ≤ 220 MB 内存预算。

酷狗概念版没有官网、没有官方 PC 端；本项目目标是在 PC 上提供非官方的酷狗概念版体验。

- **前端**：Tauri 2.0（Rust + WRY WebView2）+ Vue 3 + Vanilla CSS（无 CSS 框架）
- **后端**：C++ EchoCompatServer HTTP sidecar（loopback `127.0.0.1:6609`）+ EchoCore / EchoStorage / EchoPlayback / EchoImage
- **构建**：Vite 6 + pnpm 11 + CMake + MSVC C++20

## 目录结构

```
BottleMusic/
├── ui/         ← Tauri 2.0 + Vue 3 前端
├── native/     ← C++ EchoCompatServer 后端
├── server/     ← KuGouMusicApi 参考实现（接口迁移来源）
├── docs/       ← 项目逻辑文档
└── AGENTS.md   ← agent 工作指南
```

## 快速开始

```powershell
# 前端开发
cd ui
pnpm install
pnpm tauri dev          # 首跑 5-10 分钟，之后 < 30s

# 后端构建（VS Developer PowerShell）
pnpm backend:build      # 等价于 cmake + sync 二进制到 ui/src-tauri/binaries/
pnpm backend:sync       # 只同步已有产物
```

## 当前状态

**已打通的核心链路**：
- 扫码登录 → 用户信息 / VIP 状态 / 头像昵称显示
- 搜索 → 歌曲列表 → 解析播放 URL → 播放（含 VIP 歌曲）
- 用户歌单加载（默认收藏 / 我喜欢 / 自建歌单）
- 歌词获取与同步高亮
- VIP 查询与领取链路仍受酷狗概念版风控、广告 SDK 凭证和设备指纹约束

**设备风控方案**：用户从真实环境抓取 `dfid` / `mid` / `uuid`，通过 `/settings/device` 接口写入。详见 `docs/PROJECT_LOGIC.zh-CN.md`。

**技术参考**：后端接口实现参照 [MakcRe/KuGouMusicApi](https://github.com/MakcRe/KuGouMusicApi) 与 develop202/kgcheckin。项目基线是酷狗概念版，不把标准版参数作为默认事实。

## 文档导览

- [`AGENTS.md`](AGENTS.md) — agent 工作流程、skills、约束、验证规则
- [`docs/PROJECT_LOGIC.zh-CN.md`](docs/PROJECT_LOGIC.zh-CN.md) — 项目逻辑、架构、酷狗概念版 API 事实

## 免责声明

本项目仅用于个人学习和技术研究。音乐数据和版权归原平台及版权方所有，请尊重知识产权并支持正版音乐。
