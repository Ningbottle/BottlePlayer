# BottleMusic

> Windows 上的酷狗概念版非官方客户端

[English](./README.en.md) | 中文

[隐私说明](./PRIVACY.md) | [安全政策](./SECURITY.md)

<!-- logo -->

![CI](https://img.shields.io/github/actions/workflow/status/Ningbottle/BottlePlayer/ci.yml?label=CI)
![Version](https://img.shields.io/github/v/release/Ningbottle/BottlePlayer)
![License](https://img.shields.io/github/license/Ningbottle/BottlePlayer)
![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11%20x64-blue)

BottleMusic 是面向 Windows 的非官方桌面客户端，不代表酷狗或任何第三方服务方，也不声称已获得酷狗的授权。项目提供双皮肤视觉（Aurora 沉浸式 + Newsprint 报纸风）以及播放、均衡器和统计功能。

> 重要：任何公开上架、分发或商业使用，都必须先取得相关第三方服务和音乐内容权利人的必要授权。免责声明不能替代授权；在未取得授权前，请勿据此公开分发或运营服务。

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
- 可选的 DeepSeek AI 听歌分析：只有用户主动点击 AI 分析时，才会把听歌摘要发送给 DeepSeek；API Key 只在当前页面会话中使用，不落盘

### 搜索
- 歌曲 / 歌手 / 专辑搜索，搜索结果直接播放或加入队列

### 歌单
- 加载用户歌单（收藏 / 自建），点击即以整列表为播放队列

### 登录
- 扫码登录、用户信息 / VIP 状态显示
- 账号相关功能和第三方服务入口以实际可用性及第三方服务条款为准

### 自动更新
- 内置 Tauri 更新器，启动时自动检查 GitHub Releases 新版本

## 下载安装

前往 [Releases 页面](https://github.com/Ningbottle/BottlePlayer/releases) 下载最新版本。

**系统要求**：Windows 10/11 x64

安装方式：NSIS 当前用户安装程序，仅写入当前 Windows 用户范围。安装后启动即可使用，后续版本更新将自动检测并提示安装。

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

BottleMusic 采用三层架构：Vue 3 前端负责 UI 与播放控制，Rust FFI 层桥接 Tauri 命令，C++ 核心处理 KuGou API 请求调度与 SQLite 统计存储。播放使用 HTML5 Audio + Web Audio API 均衡器（Media Foundation 播放栈已于 2026-07-17 移除）。

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
| C++ 核心 | MSVC C++20, WinHTTP, SQLite |
| CI/CD | GitHub Actions, CMake, vcpkg, CTest, Vitest, Cargo |

## 致谢

- 后端接口实现参照 [MakcRe/KuGouMusicApi](https://github.com/MakcRe/KuGouMusicApi)
- 项目基线为酷狗概念版（appid=3116，Lite 盐）

## 免责声明

本项目是非官方软件，与酷狗及其他第三方服务方没有隶属或代表关系，不声称已获得任何第三方服务或内容授权。项目仅用于个人学习和技术研究；音乐数据、曲目元数据、封面、歌词及音频内容的权利归相应平台和权利人所有。任何公开上架、分发或商业使用前，使用者必须自行取得必要的第三方服务和内容授权。本免责声明不能替代授权，也不构成对第三方服务条款的豁免。请遵守适用法律、平台规则和服务条款。

隐私说明见 [PRIVACY.md](./PRIVACY.md)，安全政策见 [SECURITY.md](./SECURITY.md)，安全与隐私架构详情见 [docs/wiki/security-and-privacy.md](./docs/wiki/security-and-privacy.md)。
