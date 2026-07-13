# BottleMusic

> Windows 上的酷狗概念版非官方客户端

[English](./README.en.md) | 中文

<!-- logo -->

![CI](https://img.shields.io/github/actions/workflow/status/Ningbottle/BottlePlayer/ci.yml?label=CI)
![Version](https://img.shields.io/github/v/release/Ningbottle/BottlePlayer)
![License](https://img.shields.io/github/license/Ningbottle/BottlePlayer)
![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11%20x64-blue)

酷狗概念版没有官网、没有官方 PC 端。BottleMusic 致力于在 PC 上提供非官方的酷狗概念版体验，主打双皮肤视觉（Aurora 沉浸式 + Newsprint 报纸风）与完整的播放/均衡器/统计功能。

## 截图

> 截图即将补充

<!-- screenshot: Aurora 主界面 -->
<!-- screenshot: Aurora 全屏歌词 -->
<!-- screenshot: Newsprint 主界面 -->
<!-- screenshot: 统计仪表盘 -->
<!-- screenshot: 均衡器面板 -->

## 功能特性

### 播放
- HTML5 Audio 播放引擎，支持播放队列、单曲循环 / 列表循环 / 随机播放
- 拖拽进度条跳转、音质切换、切歌立即停旧曲

### 均衡器
- 10 频段 Web Audio API 均衡器（31Hz / 62Hz / 125Hz / 250Hz / 500Hz / 1kHz / 2kHz / 4kHz / 8kHz / 16kHz）
- 6 种内置预设，本地音频代理自动处理跨域 CDN 媒体
- 代理不可用时显示降级提示

### 双皮肤
- **Aurora**：沉浸式粒子动效、渐变光晕、全屏歌词沉浸模式
- **Newsprint**：报纸风排版、极简编辑风格、暗色模式支持

### 歌词
- 自动跟随播放进度（3 秒空闲后自动恢复跟随）
- 全屏沉浸模式、点击歌词行跳转播放进度

### 统计
- 播放历史仪表盘：总播放次数、实际听歌时长、完成率、独立歌曲/歌手数
- Top 榜单：最常听的歌曲 / 歌手 / 专辑（按 album_id 分组）
- 时间线图表：每日播放次数
- DeepSeek AI 听歌分析：基于本地听歌数据的个性化分析报告

### 搜索
- 歌曲 / 歌手 / 专辑搜索，搜索结果直接播放或加入队列

### 歌单
- 加载用户歌单（收藏 / 自建），点击即以整列表为播放队列

### 登录
- 扫码登录、用户信息 / VIP 状态显示
- 每日免费 VIP 领取（听歌 / 广告）

### 自动更新
- 内置 Tauri 更新器，启动时自动检查 GitHub Releases 新版本

## 下载安装

前往 [Releases 页面](https://github.com/Ningbottle/BottlePlayer/releases) 下载最新版本。

**系统要求**：Windows 10/11 x64

安装方式：NSIS 安装程序（支持当前用户 / 所有用户安装）。安装后启动即可使用，后续版本更新将自动检测并提示安装。

## 皮肤展示

### Aurora
沉浸式设计，配有粒子动效和渐变光晕。全屏歌词模式下支持封面展示、进度条、3D 队列货架。

<!-- screenshot: Aurora 皮肤展示 -->

### Newsprint
报纸风排版，极简编辑风格。支持暗色模式切换。

<!-- screenshot: Newsprint 皮肤展示 -->

## 架构概览

```
Vue 3 前端 (ui/src/)
    │  Tauri IPC
Rust FFI 层 (ui/src-tauri/src/)
    │  extern "C" FFI
C++ 核心 (native/) -> EchoCAPI.dll
```

BottleMusic 采用三层架构：Vue 3 前端负责 UI 与播放控制，Rust FFI 层桥接 Tauri 命令，C++ 核心处理 KuGou API 请求调度、SQLite 统计存储与 Media Foundation 接口。播放使用 HTML5 Audio + Web Audio API 均衡器。

完整架构文档请参考 [CONTEXT.md](./CONTEXT.md)。

## 开发

| 工具 | 版本 |
|---|---|
| Node.js | ≥ 22 |
| pnpm | 11 |
| Rust | stable |
| CMake + MSVC | C++20 |

```powershell
git clone --recurse-submodules https://github.com/Ningbottle/BottlePlayer.git
cd ui
pnpm install
pnpm tauri dev
```

完整开发文档请参考 [CONTEXT.md](./CONTEXT.md)。

## 技术栈

| 层 | 技术 |
|---|---|
| 前端 | Vue 3, Vite 6, Vanilla CSS, GSAP, Web Audio API |
| Rust FFI | Tauri 2.0, reqwest, tokio |
| C++ 核心 | MSVC C++20, WinHTTP, Media Foundation, SQLite |
| CI/CD | GitHub Actions, CMake, vcpkg, CTest, Vitest, Cargo |

## 致谢

- 后端接口实现参照 [MakcRe/KuGouMusicApi](https://github.com/MakcRe/KuGouMusicApi)
- 项目基线为酷狗概念版（appid=3116，Lite 盐）

## 免责声明

本项目仅用于个人学习和技术研究。音乐数据和版权归原平台及版权方所有，请尊重知识产权并支持正版音乐。
