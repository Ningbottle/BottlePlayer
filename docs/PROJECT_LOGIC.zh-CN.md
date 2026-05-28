# BottleMusic 项目逻辑

本文回答「项目是什么、怎么运行、哪些事实不能混淆」。Agent 工作流程见根目录 `AGENTS.md`。

## 1. 项目定位

BottleMusic 是面向 Windows 10/11 x64 的 **酷狗音乐概念版 PC 非官方客户端**。

- 酷狗音乐概念版没有官网。
- 酷狗音乐概念版没有官方 PC 端。
- 本项目目标是开放/补齐酷狗音乐概念版在 PC 上的非官方体验。
- 本项目不是酷狗标准版 PC、不是官方客户端、不是 m 站壳。

## 2. 技术栈与架构

| 层 | 技术 |
| --- | --- |
| 壳 | Tauri 2.0 + WRY WebView2 |
| UI | Vue 3 Composition API + Vanilla CSS |
| 后端 | C++20 EchoCompatServer sidecar |
| 通信 | HTTP loopback `127.0.0.1:6609` |
| 播放 | 前端 HTML5 Audio 使用后端解析出的 CDN URL |

```text
Vue 3 UI
  └─ ui/src/api/backend.ts
      └─ HTTP 127.0.0.1:6609
          └─ EchoCompatServer.exe
              ├─ EchoCore        酷狗接口、签名、DTO、错误模型
              ├─ EchoStorage     SQLite、登录态、设备信息
              ├─ EchoPlayback    播放状态机
              ├─ EchoImage       图片解码与缓存
              ├─ EchoAsync       后台任务
              └─ EchoDiagnostics 日志与内存快照
```

Tauri 只负责窗口和 sidecar 生命周期，不承载酷狗业务逻辑。播放中整进程目标 ≤ 220 MB。

## 3. 关键目录

| 路径 | 含义 |
| --- | --- |
| `ui/src/api/backend.ts` | 前端唯一后端入口 |
| `ui/src/api/userStore.ts` | 登录态、VIP 状态 |
| `ui/src/api/playerStore.ts` | 播放队列、HTML5 Audio |
| `native/compat_server/CompatServer.cpp` | HTTP sidecar |
| `native/core/CompatApi.cpp` | 兼容路由分发 |
| `native/core/Crypto.cpp` | MD5、签名、RSA/AES |
| `native/core/DeviceService.cpp` | 本地设备信息 |
| `native/core/UserService.cpp` | 用户、VIP、签到接口 |
| `native/core/SongUrlService.cpp` | 歌曲 URL 解析 |
| `server/` | MakcRe/KuGouMusicApi submodule，完整 API 参考 |

## 4. 参考仓库事实与命名边界

`server/` 是 MakcRe/KuGouMusicApi 的本地参考仓库，不是 BottleMusic 的运行时后端。`develop202/kgcheckin` 是 VIP/签到链路的补充参考。两者里的名称不能直接搬进本项目语义：

- MakcRe 的 `platform=lite` 表示参考仓库的平台开关；在 BottleMusic 中应落为 **酷狗音乐概念版**，不是另建一个 “Lite Edition” 业务身份。
- `appid=1005/clientver=20489` 是酷狗标准版参考值，不是项目默认。
- `liteAppid=3116/liteClientver=11440` 与 kgcheckin 的 `clientver=11436` 都是概念版链路参考值，具体接口应以抓包/参考模块为准。
- m.kugou.com Cookie-only GET 不是当前确认的 VIP 领取事实。

### MakcRe/KuGouMusicApi

- `util/helper.js`：签名函数和 lite/standard 盐选择。
- `util/request.js`：默认参数注入；无 `uuid` 实际常用 `"-"`；`mid` 主要来自 `KUGOU_API_MID`。
- `util/util.js`：`calculateMid(str)` 是 `MD5(str)` 的 hex digest 按 base16 大整数转十进制字符串。
- `server.js`：`KUGOU_API_MID = calculateMid(KUGOU_API_GUID ?? guid)`，即 Android/概念版 mid 的血缘来自 guid，不是来自 dfid。
- `util/config.json`：`appid=1005`，`liteAppid=3116`，`clientver=20489`，`liteClientver=11440`。
- `module/song_url.js`：概念版 `page_id`、`pid`、`ppage_id` 参数。
- `module/user_vip_detail.js`：`get_union_vip` 只传 `busi_type: "concept"`，不传 `plat`。
- `module/youth_day_vip.js` / `module/youth_day_vip_upgrade.js`：是广告 VIP 相关旧端点，可能需要官方 App 广告 SDK 凭证；纯 HTTP 不得伪造成功。
- `module/youth_vip.js`：`POST /youth/v1/ad/play_report`，Android 签名，body 携带广告播放时间。
- `module/youth_listen_song.js`：`POST /youth/v2/report/listen_song`，Android 签名，body 携带 `mixsongid`。
- lite/standard 切换由 `process.env.platform === "lite"` 控制；appid、clientver、盐、RSA key 都是下游。

### develop202/kgcheckin

- `youth_listen_song.js`：实际端点是 `POST /youth/v2/report/listen_song`。
- `youth_vip.js`：实际端点是 `POST /youth/v1/ad/play_report`。
- `youth_union_vip.js`：`get_union_vip` 需要 `opt_product_types` 和 `product_type`。
- `config.json`：`liteAppid=3116`，`liteClientver=11436`。

重要纠正：不是 `m.kugou.com` Cookie-only GET；实际 VIP 相关端点走 `gateway.kugou.com` + Android 签名。

## 5. 酷狗音乐概念版参数基线

酷狗音乐概念版优先使用：

- `appid = 3116`
- `clientver = 11440`；部分 kgcheckin 链路可见 `11436`
- `busi_type = concept`

不要把标准版 `appid=1005/clientver=20489` 当作项目默认。标准版参数只能作为个别接口兼容或对照实验。

签名盐选择不要简单按 appid 猜，应回到参考仓库的 platform/lite 逻辑，并在代码中由 `KuGouSaltKind` 表达：

- Android signature 盐：概念版/lite 使用 `LnT6xpN3khm36zse0QzvmgTZ3waWdRSA`。
- 标准版使用 `OIlwieks28dk2k092lksi2UIkp`。
- song URL `key` 盐：概念版/lite 使用 `185672dd44712f60bb1736df5a377e82`。
- 标准版使用 `57ae12eb6890223e355ccfcb74edf70d`。
- 业务代码不得直接散落这些字面量，应经 `GetKuGouProfile()` 和 `profile.saltKind` 派生。

例外参数：

- 扫码登录 `/v2/qrcode` 可使用 `QrLoginAppId = 1001`。
- `/v5/url` 在 MakcRe `song_url.js` 中显式使用 `clientver=11430`，不能误套全局 `11440`。

## 6. 设备指纹与风控

酷狗风控依赖 `dfid`、`mid`、`uuid`、`guid` 的一致性。这里最容易混淆，需要区分两条血缘：

```text
MakcRe 概念版 Android mid:
  mid = decimal_bigint(MD5(guid))

kgcheckin / 某些签到链路可见的 dfid 派生:
  mid  = MD5(dfid) + MD5(dfid)[0:7]
  uuid = MD5(dfid + mid)
```

当前 BottleMusic 的项目基线应优先按 MakcRe 概念版 Android 链路理解：`KUGOU_API_MID` 来自 `guid`，`register_dev` 的 AES body 里 `imei/uuid` 也应与这个 guid 血缘一致。不要把 dfid 派生公式无条件当作所有概念版接口的全局事实。

风险点：

- 随机 `dfid`、随机 `mid`、固定占位 `dfid="-"` 派生出的固定 mid，或错误血缘的 `uuid` 都容易触发风控。
- `guid`、注册 body 里的 `imei/uuid`、query 里的 `mid` 若不是同一设备血缘，容易出现签名通过但风控失败。
- `/v5/url` 与 `/v6/priv_url` 的 `mid` 形态必须与 Android/概念版一致；不能一个用十进制、一个用 hex。
- `/risk/v2/r_register_dev` 复刻旧实现后仍可能被酷狗拒绝。
- 真实设备指纹可能有有效期，可能随登录、设备、风控周期失效。

当前安全路线：允许用户输入真实抓包得到的 `dfid/mid/uuid`，后端保存为受信设备信息。

## 7. 登录逻辑

1. `GET /login/qr/key`
2. `GET /login/qr/create`
3. 轮询 `GET /login/qr/check`
4. 成功后保存 `userid/token/nickname/pic`
5. 调 `/user/detail`、`/user/vip/detail` 刷新状态

鉴权字段只留在 EchoCore/EchoStorage，不暴露给 Vue 组件。

## 8. VIP 逻辑

VIP 查询：

- 以 `get_union_vip` 为核心。
- 酷狗音乐概念版必须关注 `busi_type=concept`。
- kgcheckin 还要求 `opt_product_types`、`product_type`。
- MakcRe `user_vip_detail.js` 只显式传 `busi_type=concept`；`youth_union_vip.js` 使用 `opt_product_types=dvip,qvip`、`product_type=svip`。如果代码使用其他组合，必须标记为待验证，而不能写成已确认事实。

VIP 领取：

- MakcRe 的 `receive_vip_listen_song` / `upgrade_vip_reward` 可能返回 `51002` 或广告凭证相关失败，纯 HTTP 不应伪造绕过。
- kgcheckin 发现的实际端点是 `gateway.kugou.com` 下的 POST + Android 签名：`/youth/v2/report/listen_song` 与 `/youth/v1/ad/play_report`。
- 若上游明确要求广告 SDK 凭证，应向 UI 返回可解释失败，不伪造成功。

## 9. 歌曲 URL 与播放

前端只请求：

```text
GET /song/url?hash=...&album_id=...&album_audio_id=...
```

后端负责酷狗音乐概念版参数组装、签名、设备指纹、登录 token、VIP/风控/试听/匿名 fallback。

概念版 song URL 关键点：

- `/v5/url` 使用 MakcRe `song_url.js` 的概念版参数：`appid=3116`、`clientver=11430`、`page_id=967177915`、`pid=411`、默认 `ppage_id=356753938,823673182,967485191`。
- `/v5/url` 的 `mid` 应为 Android/概念版十进制形态，即 `decimal_bigint(MD5(guid))`。
- `/v6/priv_url` 若使用同一 appid/salt，也应保持同一 mid 形态；不能直接发送 32/39 位 hex mid。
- `SignKey(hash, mid, userid, appid, saltKind)` 中的 `mid` 必须与请求参数里的 `mid` 同形态。

UI 只使用返回的 `url/play_url/playUrl` 设置 HTML5 Audio。

预览判定：

- 明确 VIP 拒绝：`fail_process` 含 `pkg/buy/vip`。
- URL 路径含 `/full/` 通常表示完整音频。
- 匿名 fallback 拿到 `/full/` 不应错误标为试听。

## 10. UI 与视觉

视觉方向：Newsprint 报纸风。

- 纸色：`#f1ead8`
- 红强调：`#a8311b`
- 主文字：`#221b12`

控件状态变化不能导致布局跳动。列表、队列、歌单、搜索结果、评论都按未来虚拟化设计。

## 11. 不可混淆事项

- 酷狗音乐概念版 ≠ 酷狗标准版。
- 酷狗音乐概念版没有官网、没有官方 PC 端。
- `server/` 是参考实现，不是本项目运行时后端。
- `EchoCompatServer` 是当前 UI 到 C++ 的兼容通道，不代表最终内部 Interface。
- m 站 Cookie-only GET 不是当前已确认的 VIP 领取事实。
- 不能把上游风控或广告 SDK 凭证失败伪造成成功。

## 12. 当前代码审查发现（待修复，不是目标事实）

以下是 2026-05 对照 MakcRe/KuGouMusicApi 与 kgcheckin 后发现的实现风险。它们用于指导后续修复，不应被当作正确事实继续传播。

1. `DeviceService` 当前用 `dfid` 派生 `mid = MD5(dfid) + MD5(dfid)[0:7]`，但 MakcRe 概念版 `KUGOU_API_MID` 来自 `calculateMid(guid)`。这会让 `guid`、注册 body identity 与 query mid 血缘不一致。
2. 未注册设备使用 `dfid="-"` 后再派生 mid/uuid，会让所有默认未注册设备得到相同指纹，应避免把占位符派生成稳定设备身份。
3. `SongUrlService` 的 `/v5/url` 已接近 MakcRe：`clientver=11430`、概念版 page/pid、Android decimal mid；但 `/v6/priv_url` 仍有使用原始 hex mid 的风险。
4. `UserService::BuildSignedQueryString` 依赖 `SignatureAndroidParams` 默认 Lite 盐，应该显式传 `profile.saltKind`，避免未来默认值变更造成隐性回归。
5. `ClaimVip` / `UpgradeVipReward` 仍保留真实上游请求实现；即使当前路由层禁用，也应在 public Interface 层返回稳定错误或标记 deprecated，避免绕过路由误调用。
6. `GetUserVip` 的 `opt_product_types` 当前实现与本地参考 `youth_union_vip.js` 不一致，需要抓包确认后再固化。
