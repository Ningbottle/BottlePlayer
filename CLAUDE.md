# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

BottleMusic 是一个 Tauri 2.0 + Vue 3 + C++ EchoCompatServer HTTP sidecar 的音乐客户端。本文件专注于 `EchoMusic-tauri` 分支（Tauri 前端）。原生 Win32+D2D 路径的规则见 `AGENTS.md`，任务队列见 `docs/WORKLIST.zh-CN.md`。

---

## 📌 当前状态

> 截止 2026-05-25。

### 设备风控注册（2026-05-26 BREAKTHROUGH — 用户提供真实 dfid 解决）

**已确认根因**：KuGou 风控对随机生成的 dfid 一律降权；自己实现的 `/risk/v2/r_register_dev` 调用 KuGou 已修改签名不再接收（详见下方 8 种组合实验）。

**最终方案**：[settings/device](native/core/CompatApi.cpp) 路由让用户**直接输入手抓的真实 dfid/mid/uuid**，存进 DeviceRepository 标记 `registered=true`，后续 `/song/url`、`/user/playlist` 直接用这组真值。
- 前端 UI：[SettingsView.vue](ui/src/views/SettingsView.vue) "Device Fingerprint" 卡片
- 数据来源：浏览器 F12 抓 `m.kugou.com` 的 Network 面板，复制 `dfid=`、`mid=`、`uuid=`
- 实测以一组真实指纹（用户提供）替换后：**麦恩莉/VIP 歌从 `20028 needs verify` → `20018 needs VIP pkg`，意味着请求已被信任**。`F0A6BA24635A8560F96C2C2D603E8CA8` 风中芭蕾返回**完整 mp3 URL** `/full/...mp3`，VIP 歌完整播放跑通

**关键 bug 修复（与本次破解一并修通）**：
- `Crypto::SignatureAndroidParams` 之前按 appid 选 lite/regular 盐 → 改为始终用 regular 盐（MakcRe/helper.js 是按 `process.env.platform` 选，从不按 appid）
- `BuildV5Url` 之前 lite 模式默认 → 改回 1005/11430 regular，与 MakcRe 默认一致
- `SongUrlService` 之前去掉了 dfid/mid/uuid（"-/0" workaround）→ 改回用 device 的真值
- `PlaylistService::GetUserPlaylists` body 字段顺序改成插入顺序（与 JS axios.JSON.stringify 一致），userid 改为 number 而非 string

**症状对照（修复前 → 修复后）**：
- `/song/url` VIP 歌 → 之前 `errcode:20028`（设备未注册）→ 现在返回完整 mp3 URL（或个别歌曲返 `20018` 表"需要更高 VIP"，超出 SVIP 范围）
- `/user/playlist` → 之前 `error_code:20017`（信号未通过）→ 现在仍 20017 但**性质改变**：是 token 过期，需要重新扫码登录

**已实现的破解尝试**：[DeviceRegisterService](native/core/DeviceRegisterService.cpp) 完整复刻了 [MakcRe/KuGouMusicApi register_dev.js](https://github.com/MakcRe/KuGouMusicApi/blob/main/module/register_dev.js) 流程——AES-CBC + RSA-PKCS1-v1.5 + android 签名 + Xiaomi Redmi 设备指纹 POST 到 `https://userservice.kugou.com/risk/v2/r_register_dev`。

**实测尝试过的签名/参数组合（全部失败）**：
| 配置 | KuGou 返回 |
|---|---|
| appid=1005 + clientver=20489 + signatureAndroidParams + Content-Type=application/json | `errcode:20010 (签名错误)` |
| 同上但 Content-Type=text/plain | `errcode:20010` |
| 同上但去掉 Content-Type | `errcode:20010` |
| 同上但去掉 plat 参数 | `errcode:20010` |
| 同上但 RSA hex 改成小写 | `errcode:20010` |
| appid=1014 + clientver=20000 + signatureAndroidParams (lite salt) | `errcode:20010` |
| appid=1005 + signatureRegisterParams (md5("1014"+sorted_values+"1014")) | `errcode:20006` (签名过了，但 appid 被拒) |
| appid=1014 + signatureRegisterParams | `errcode:20010` |

**结论**：KuGou 自上一次 MakcRe 更新以来肯定改了 `/risk/v2/r_register_dev` 的签名算法或对端校验逻辑。继续突破需要：
1. **抓 KuGou Android App 真包**：用 mitmproxy/Charles 拦截 official KuGou App 启动时的 `/risk/v2/r_register_dev` 请求，对比签名值；或
2. **逆向 KuGou App 的 libkugou.so**：找到设备注册函数的真实签名生成代码；或
3. **从手机端导出真实 dfid**：让用户在手机 KuGou App 抓自己的 dfid，硬编码到 DeviceService

**触发时机**（即使失败也会运行）：
- QR 登录成功后立刻在 [CompatApi.cpp `/login/qr/check`](native/core/CompatApi.cpp) 调一次
- `/register/dev?force=1` 可手动触发并通过 `register_error` 字段查看 KuGou 错误
- 前端 [userStore.checkLoginStatus](ui/src/api/userStore.ts) 每次启动调一次 `/register/dev` 兜底

### 已知遗留

- **少数 buy-only 单曲不解锁**：例如 `90B8469459CBA58A5DDEDD9350286DD8`（林俊杰"十面埋伏"）即使 SVIP+注册设备也返 `fail_process:["pkg","buy"]`。KuGou 单曲付费策略，超出代码可解决范围
- **20.8 Title Bar 内存显示**：已实现（用户自完成）
- **EchoNativeSmokeTests PlaybackController 段挂死 ~70s**：测试用 `https://example.invalid/audio.mp3` 触发 MF 异步打开 → `Stop()` 等 TCP 超时。不影响线上 EchoCompatServer
- **MSVC 注意**：designated initializer 对省略字段不会用 NSDMI 默认值——必须显式 `.field = value` 写全所有字段，否则 bool 等会取未初始化的垃圾值（参见 [DeviceService::CreateDeviceInfo](native/core/DeviceService.cpp)）
- **nlohmann::json 注意**：`{"key", boolValue}` 初始化列表对 bool 解析有歧义——用 `j["key"] = boolValue` 显式赋值（参见 [JsonHelpers::ToJson(DeviceInfo)](native/core/JsonHelpers.cpp)）

---

## 构建与开发

所有命令在 `ui/` 目录下运行：

```powershell
cd ui
pnpm install          # 安装前端依赖
pnpm tauri dev        # 首次约 5-10 min（Rust 编译），后续 <30s
pnpm build            # 类型检查 + Vite 构建
```

C++ sidecar 构建（需要 VS Developer 环境）：

```powershell
# 完整构建 + 同步
pnpm backend:build    # 等价于 cmake --preset bottlemusic-check + build + sync

# 仅同步已有产物到 src-tauri/binaries/
pnpm backend:sync

# 手动 CMake（在 VS Developer PowerShell）
cmake -S native --preset bottlemusic-check
cmake --build native/out/bottlemusic-check --target EchoCompatServer --config Debug
```

快速验证（C++ 测试）：

```powershell
cmd /s /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && "D:\QT\Tools\CMake_64\bin\cmake.exe" --build native\out\bottlemusic-check --config Debug --target EchoNativeSmokeTests && "D:\QT\Tools\CMake_64\bin\ctest.exe" --test-dir native\out\bottlemusic-check --output-on-failure'
```

---

## 架构概览

```
Vue 3 组件 (ui/src/)
  └─ apiGet() / ui/src/api/backend.ts ──HTTP fetch──► EchoCompatServer (C++ sidecar @ 127.0.0.1:6609)
                                                         ├── EchoCore    (酷狗 API、DTO)
                                                         ├── EchoStorage (SQLite、登录 token)
                                                         ├── EchoPlayback (Media Foundation)
                                                         └── EchoImage   (WIC + LRU 缓存)
```

- **唯一 HTTP 入口**：`ui/src/api/backend.ts` 的 `apiGet()`，禁止组件直接 `fetch`。
- **Tauri sidecar 生命周期**：`ui/src-tauri/src/lib.rs` 在 `setup()` 中启动 `EchoCompatServer`，窗口关闭时 `kill()`；端口固定 `6609`，由 `backend_base_url` invoke 返回。
- **HTML5 Audio**：播放不经 Tauri invoke，`playerStore.ts` 直接把 `/song/url` 返回的 CDN URL 赋给 `audio.src`。
- **响应式状态**：`userStore`（登录态）和 `playerStore`（播放状态、队列）均为 Vue `reactive` 单例，`localStorage` 持久化队列/音量/循环模式。

---

## 关键文件速查

| 目的 | 路径 |
|------|------|
| HTTP 网关 | `ui/src/api/backend.ts` |
| 播放状态 & 队列 | `ui/src/api/playerStore.ts` |
| 登录态 & VIP | `ui/src/api/userStore.ts` |
| Track 字段规范化 | `ui/src/api/normalizer.ts` |
| 登录视图 | `ui/src/views/LoginView.vue` |
| 应用根 & 路由 | `ui/src/App.vue` |
| Newsprint CSS 变量 | `ui/src/style.css` |
| Tauri Rust 壳 | `ui/src-tauri/src/lib.rs` |
| C++ 路由注册 | `native/compat_server/CompatServer.cpp` |
| C++ 业务接口 | `native/core/CompatApi.h` / `BackendFacade.cpp` |
| 设计参考 | `Music Player.html`（Newsprint 参考 UI） |
| 任务队列 | `docs/WORKLIST.zh-CN.md` |
| 技术栈文档 | `docs/REFERENCE.zh-CN.md` |

---

## Newsprint 设计 token

CSS 变量定义在 `ui/src/style.css`，对应 C++ `Theme.h` 常量：

| CSS 变量 | 值 | 用途 |
|----------|-----|------|
| `--paper` | `#f1ead8` | 主背景 |
| `--paper-alt` | `#ebe2cb` | 卡片/面板 |
| `--ink` | `#221b12` | 主文字 |
| `--ink-soft` | `#4a3f2f` | 次文字 |
| `--accent` | `#a8311b` | 红色强调（进度条、激活态、按钮） |
| `--rule` | `rgba(34,27,18,0.14)` | 分割线 |

字体：`'Noto Serif SC', 'EB Garamond', 'Songti SC', serif`。

---

## 登录流程（QR Code）

`LoginView.vue` + `userStore.ts`：

1. `GET /login/qr/key` → `data.qrcode`（key）+ `data.qrcode_img`（base64 二维码图片）
2. 每 2 秒 `GET /login/qr/check?key=...` 轮询，`data.status===4` 表示成功
3. 成功后调 `checkLoginStatus()` → `GET /user/detail` + `GET /user/vip/detail`
4. 两者返回 `status===1` 时才设 `userStore.isLoggedIn = true`

**已知问题**：若 `/user/detail` 返回 `native_not_implemented`（status=0），`checkLoginStatus()` 将 `isLoggedIn` 保持 false，`emit('navigate', 'home')` 不触发，登录后界面卡在 QR success 覆盖层。修复路径：在 `EchoCompatServer` 中实现 `/user/detail` 路由，或让 `checkLoginStatus()` 在 `status===0` 且 `error_code=native_not_implemented` 时给出降级处理。

---

## 歌曲 URL 获取

`playerStore.ts:playTrack()` 流程：

1. `GET /song/url?hash={FileHash}&album_id={AlbumID}&album_audio_id={AlbumAudioID}`
2. 返回 `{ status: 1, url: "https://..." }` → 赋给 `audio.src` 播放
3. 失败时 `playerStore.errorMsg` 设为错误文案

**已知问题**：若 C++ 后端 `/song/url` 返回 `status≠1` 或 `url` 为空，播放会静默失败并显示 "获取歌曲链接失败"。调试时先用 `http://127.0.0.1:6609/song/url?hash=XXX` 直接验证后端响应，再查 `EchoCore` 中的 URL 解析逻辑。需 VIP token 才能获取高质量音源；免费 token 可能返回空 URL。

---

## 实现约束

- 前端所有后端请求必须经 `apiGet()`，禁止组件直接 `fetch`
- UI 线程不做网络、SQLite、图片解码调用
- `dfid`、`mid`、`uuid`、`token` 等鉴权字段只在 `EchoCore` 内部使用，不暴露到前端
- 内存目标：播放中整进程 ≤ 220 MB（WebView2 基线约 60-80 MB）
- 默认用中文写文档，代码标识符用英文
