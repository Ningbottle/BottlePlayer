# BottleMusic

BottleMusic 是面向 Windows 10/11 x64 的轻量音乐客户端，主打 Newsprint 报纸风视觉与 ≤ 220 MB 内存预算。

- **前端**：Tauri 2.0（Rust + WRY WebView2）+ Vue 3 + Vanilla CSS（无 CSS 框架）
- **后端**：C++ EchoCompatServer HTTP sidecar（loopback `127.0.0.1:6609`）+ EchoCore / EchoStorage / EchoPlayback / EchoImage
- **构建**：Vite 6 + pnpm 11 + CMake + MSVC C++20

## 目录结构

```
EchoMusic-tauri/
├── ui/         ← Tauri 2.0 + Vue 3 前端
├── native/     ← C++ EchoCompatServer 后端
├── server/     ← KuGouMusicApi 参考实现（接口迁移来源）
├── docs/       ← REFERENCE / WORKLIST 文档
└── CLAUDE.md   ← 长期 agent 入口（含当前 TODO）
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

## 当前状态（2026-05-27）

**已打通的核心链路**：
- 扫码登录 → 用户信息 / VIP 状态 / 头像昵称显示
- 搜索 → 歌曲列表 → 解析播放 URL → 播放（含 VIP 歌曲）
- 用户歌单加载（默认收藏 / 我喜欢 / 自建歌单）
- 歌词获取与同步高亮
- 每日 VIP 领取（需官方 App 广告 SDK 凭证，HTTP 端无法伪造）

**设备风控方案**：用户从浏览器 F12 抓取真实 `dfid` / `mid` / `uuid`，通过 `/settings/device` 接口写入。详见 `CLAUDE.md`。

**技术参考**：后端接口实现参照 [MakcRe/KuGouMusicApi](https://github.com/MakcRe/KuGouMusicApi)，使用标准模式（appid=1005, clientver=20489）。

## 文档导览

- [`CLAUDE.md`](CLAUDE.md) — **入口文档**，含架构概览、构建命令、当前 TODO 列表
- [`AGENTS.md`](AGENTS.md) — 通用 agent 工作规则（适用于 C++ 端，UI 端规则见 CLAUDE.md）
- [`docs/REFERENCE.zh-CN.md`](docs/REFERENCE.zh-CN.md) — 技术栈、架构边界、内存预算、迁移注意事项
- [`docs/WORKLIST.zh-CN.md`](docs/WORKLIST.zh-CN.md) — 长期任务队列与验收记录

## 免责声明

本项目仅用于个人学习和技术研究。音乐数据和版权归原平台及版权方所有，请尊重知识产权并支持正版音乐。
