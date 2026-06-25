# 播放器修复设计 — S4 HTML5/Web Audio 路径 + S5 Stats 会话

日期:2026-06-25
状态:已批准,进入实现
来源:/code-review 在工作树 diff(ui/src/api/playerStore.ts + Music Player.html)上发现的 15 个问题

## 背景

播放器当前有意取舍:native MFS 后端已注释,默认强制 HTML5;EQ 改走 Web Audio API BiquadFilter 链。
这降低了 S4 native 上线风险,但 HTML5 + Web Audio 这条新生产路径踩中若干已知坑,且本 diff 新增的
S5 stats 播放会话追踪在多个边界下错记/漏记。本次修复覆盖全部 15 个发现。

## 范围

全部 15 个发现,分三类:
- HTML5 + Web Audio EQ 生产路径(#1 #4 #9 #10)
- S5 stats 会话追踪正确性(#5 #6 #7 #8 #12)
- 事件归属与零散问题(#2 #3 #11 #13 #14 #15)

## 架构(整体策略 Approach 2:收拢事件归属 + 抽出模块)

`playerStore.ts`(619 行,职责过多)拆出两个可独立测试的模块,TDD 主战场:

```
ui/src/api/
  playerStore.ts          ← 收窄为协调器:状态 + 队列 + 调用 ↓
  playSessionTracker.ts   ← 新:stats 会话状态机 + listened 累积器(纯逻辑,易测)
  webAudioEq.ts           ← 新:EQ 图控制器(构建顺序/close/resume 降级)
  html5Backend.ts         ← 不变(已是正确的事件源)
  __tests__/
    playSessionTracker.test.ts   ← 新:纯单元,不依赖 jsdom audio
    playerStore.test.ts          ← 新:事件路由集成(jsdom + AudioContext mock + invoke mock)
    webAudioEq.test.ts           ← 新:图构建顺序/close/suspended resume(AudioContext mock)
```

`Music Player.html`:第 673 行单行语法修复。
`ui/src-tauri/`:仅当 CORS 验证表明酷狗 CDN 不发 CORS 头时,才评估同源流代理。

## Section 2 — 事件归属(修 #2 #15)

`Html5AudioBackend.onEvent` 成为播放事件唯一来源(play/pause/timeupdate/ended/error)。
`initPlayer` 只保留 EQ 无关监听(durationchange/loadedmetadata);`play` 里的 resumeAudioContext
挪进 webAudioEq(Section 4)。

删除:
- `initPlayer` 中与 onEvent 重叠的直接 play/pause/timeupdate/ended/error 监听 → 消除 double-ended(#2)。
- playTrack/togglePlay/seek 里的 legacy 直放 `else` 分支 —— native 已禁用,activeBackend 必定存在,该分支为死代码。
- 独立 `handleEnded()` 合并进 `handlePlaybackEvent` 的 ended 分支,单曲重启逻辑一并搬过去。ended 单一入口。

#15 eventUnsub 泄漏:initPlayer 僵尸清理段扩展为先 `eventUnsub?.()` + `activeBackend?.shutdown()` + `audioContext.close()`,再建新 audio/backend。

## Section 3 — PlaySessionTracker(修 #5 #6 #7 #8 #12)

事件驱动状态机,不靠乐观 play():

```
IDLE ──intend(track)──▶ PENDING(track)
PENDING ──onPlay──▶ PLAYING(累积) ──onEnded──▶ 完成(recorded,回 IDLE)
        └─skip/intend 新──▶ 先 finalize(未完成)──▶ PENDING
PLAYING ──onPause──▶ PAUSED ──onPlay──▶ PLAYING
        └─skip──▶ finalize(未完成)──▶ PENDING
```

- #5 seek 虚高 → 算法 B 累积:onTimeUpdate(t) 累加 Δ=t-lastT;0<Δ<2s 计入,跳变视为 seek 不计,回跳不计。seek/循环/后台挂起免疫。
- #6 单曲循环幽灵会话:session 只在 onPlay(真正的 play 事件)时 start。play() 拒绝不触发 onPlay → 不开幽灵会话。
- #7 togglePlay 漏记:resume 触发 onPlay;若当前 session 已 finalized(ended 过),自动开新 session。
- #8 setQuality 漏记:切音质前 tracker.skip()(记旧音质未完成)+ tracker.intend(同曲);新 URL 起播 onPlay 开新 session,quality 字段实时读。
- #12 completed=0:completed 的 listened 直接用累积值(≈全曲时长),不依赖 playerStore.duration。累积为 0 即真没播。

## Section 4 — Web Audio EQ 控制器(修 #1 #4 #9 #10)

webAudioEq.ts 把 EQ 从 playerStore 抽出:

- #4 顺序:先建完整图(filter[0..4]→gain→destination 连好),最后才 createMediaElementSource 接 filter[0]。任一步抛错时图要么没建、要么已通 destination,不会把 audio 挂进断图致永久静音。
- #9 不 close:暴露 close()(audioContext.close()+置 null),僵尸清理时调用;HMR 不泄漏。close 后整体可重建(对应新 audio)。
- #10 suspended/swallowed resume:resume() 失败不再静默 .catch(()=>{}),改记日志 + eqActive=false(降级直放提示)。play 监听驱动 resume。

#1 CORS(验证驱动):
- 步骤1:curl -I 真实酷狗媒体 URL 的 Access-Control-Allow-Origin。
- 有 CORS 头 → 保留 crossOrigin='anonymous',EQ 正常(配合 #4 顺序自然满足"失败不 reroute")。
- 无 CORS 头 → 评估 Tauri register_uri_scheme_protocol 流代理 + Range;若过重,走降级:对跨域非 CORS 源不建 EQ 图(直放),EQ UI 提示"该源不支持 EQ"。播放永远正常。
- 两条路径各有单测(AudioContext mock)。

## Section 5 — 零散修复 + 测试(修 #3 #11 #13 #14)

- #3 Music Player.html:673:去转义反斜杠 → `url('${cover}')`。测试:抽内联 <script> 用 new Function() 解析,断言不抛。
- #11 next() 单曲耦合:Section 2 合并 ended 后,handlePlaybackEvent 单曲分支自己原地重启(不调 next());next() 删 `single && isPlaying` 判断,始终前进。auto-loop 与 next() 解耦。
- #13 playTrack 先持久化:saveQueue() 移到播放成功分支内;取链失败回滚 currentIndex 到 prevIndex 并重新 saveQueue,失败曲不成为重启 currentTrack。
- #14 localStorage 解析:加 loadJSON(key, fallback) 安全解析 helper,包住 player_queue/player_eq_bands 等模块级 JSON.parse,损坏回退默认值不崩。

## 测试策略(TDD,先红后绿)

- playSessionTracker.test.ts — 纯单元:状态机转移、累积算法(含 seek 跳变/回跳)、completed 用累积值、setQuality skip+intend、resume 重开。TDD 主战场(最可测)。
- webAudioEq.test.ts — AudioContext mock:图构建顺序断言(source 最后连)、close 调 close()、suspended resume 失败降级。
- playerStore.test.ts — jsdom + AudioContext mock + invoke mock:ended 单次触发(不跳两歌)、僵尸清理调 close+eventUnsub、取链失败回滚 index、loadJSON 损坏回退。
- Music Player.html — 抽 script 解析测试。

验收:`pnpm test` 全绿;`pnpm build`(vue-tsc)无类型错误。

## 发现→文件映射

| # | 发现 | 归属 |
|---|------|------|
| 1 | crossOrigin 致无声 | Section 4(验证驱动) |
| 2 | double-ended 跳歌 | Section 2 |
| 3 | HTML 语法错误 | Section 5 |
| 4 | createMediaElementSource 顺序 | Section 4 |
| 5 | seek 虚高 listened | Section 3 |
| 6 | 单曲循环幽灵会话 | Section 3 |
| 7 | togglePlay 漏记 | Section 3 |
| 8 | setQuality 漏记 | Section 3 |
| 9 | AudioContext 不 close | Section 4 |
| 10 | suspended/swallowed resume | Section 4 |
| 11 | next() 单曲耦合 | Section 5 |
| 12 | completed listened=0 | Section 3 |
| 13 | playTrack 先持久化 | Section 5 |
| 14 | localStorage 解析 | Section 5 |
| 15 | eventUnsub 泄漏 | Section 2 |
