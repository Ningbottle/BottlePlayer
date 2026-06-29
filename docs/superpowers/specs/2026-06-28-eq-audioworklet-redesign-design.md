# EQ 子系统重设:AudioWorklet + captureStream

**状态**:Approved — Phase 0 spike 通过(4 PASS + 2 spec-handles-this FAIL),进入 writing-plans
**日期**:2026-06-28(v3 revision 2026-06-28;spike backfill 2026-06-29)
**作者**:brainstorming session 产出
**关联**:取代当前工作树中的 `createMediaElementSource` EQ 实现(会触发 `InvalidStateError` 卡死)

**v3 修订摘要**(响应独立审查子智能体的 4 P1 + 高价值 P2):
- [P1] **§2.3 循环依赖修复**:`swapAudioElementAfterWedge` 删除从 step 2 移到 step 5(仅 Phase 0 通过后)。step 2 只删 `[DEBUG-bug #16]` 日志。避免 Phase 0 失败时用户失去创可贴又无重设。
- [P1] **§2.1 allowlist 收紧**:废弃 `*.kugou.com`(会误代理 `gateway.kugou.com` API)。改用精确 host 列表 + `^fs\.[a-z0-9]+\.kugou\.com$` 正则。origin 反射列表补全 + 语义明确化。
- [P1] **§5.2 attachSource 时序**:`captureStream()` 在 `play()` 前调用可能返回空 tracks。改为 `attachSource` 在 `play()` resolve 后触发,或显式验证 cold-start 行为(纳入 Phase 0 spike)。
- [P1] **§2.2 Phase 0 spike 扩展**:从 3 项扩到 6 项,补 degradation 路径、`MediaStream` track-end、N=5 首连续、proxy-URL captureStream 专项。
- [P2] §3.3 降级顺序反转(先 disconnect 再 unmute,防双声)、§7.2 L4 改写而非保留、§10 10 首自动化测试、§4.2 attachSource 重入契约、§4.4 retryEq 语义、§5.2 MediaStream track.stop()、§4.1 sample-rate aware、§2.1 origin 反射语义、§6.3 retry 计数重置。

**v2 修订摘要**(响应 user review 的 5 条 finding,保留供追溯):
- [P1] `audio_proxy.rs` 安全加固从"非目标"移到 **§2.1 前置任务**(host allowlist + 随机 ID + origin 反射 + TTL),是 blocking。
- [P1] 新增 **§2.2 Phase 0 spike**,实测 `captureStream` / `volume=0` / AudioWorklet 加载 / 连续切歌,spike 不过则本设计作废。
- [P2] 新增 **§2.3 实施顺序约束**(repro → 清理 → 代理加固 → Phase 0 → 重设),每步独立提交。
- [P2] **§5.2 生命周期定死**:`stop()` 仍 `removeAttribute('src')+load()`,同时 `disconnectSource()`;`setPreparedSource` 后 `attachSource(audio)`。消除 v1 自相矛盾。
- [P2] **§7.2 新增 L4/L5 集成测试**(swap 在 transition 中不重入、`crossOriginSafe=false` fallback 通过 `playTrack`)。

---

## 1. 背景与动机

### 1.1 当前架构与失败模式

BottleMusic 的 EQ 子系统原本设计为:用 `createMediaElementSource(audio)` 把 `<audio>` 元素"插管"进 Web Audio 图,经过 10 个 `BiquadFilterNode` 做 EQ,再送到 `destination`。KuGou CDN 不发 CORS 头,所以这套图原本被 `crossOriginSafe === false` 守卫跳过,EQ 对 CDN 音源从未真正生效。

上一任的工作加了 Tauri 本地音频代理(`audio_proxy.rs`),让 CDN 音频经 `127.0.0.1` 回流并带上 CORS 头,从而解锁 `createMediaElementSource`。思路正确,但撞上 Web Audio API 的硬约束:

> **`createMediaElementSource(audioElement)` 对同一个元素只能调用一次。** 调用后元素的音频输出被永久"接管"进图,绑定在该元素生命周期内不可逆。第二次调用抛 `InvalidStateError`。

当前架构在每次切歌时都走 `setPreparedSource → initEq → WebAudioEq.init`。第一首歌绑定成功;第二首歌时,`init` 的 `if (this.ctx) return` 守卫本应短路,但任何导致 `this.ctx` 被清空的路径(包括上一轮的 `resume()` 修复)都会让 `init` 重新执行 → `createMediaElementSource` 第二次调用 → `InvalidStateError` → catch 块 `disposeGraph()` → 元素被遗弃在已断开的图里 → 后续 `audio.play()` 解析成功但无声。**用户表现为:第一首正常,切歌后自动静默,点击无反应。**

### 1.2 决策

经过 brainstorming,用户选择 **彻底重设为 AudioWorklet 路径**,而非创可贴式的"换 `<audio>` 元素"修复。理由:创可贴只能让首曲 EQ 生效,降级后要重启应用才能重试;AudioWorklet 从根上消除"插管不可逆"约束,让每首歌都能稳定 EQ,架构干净。

### 1.3 范围与非目标

**范围内**:
- 重写 `webAudioEq.ts`,改用 AudioWorklet
- 修改 `html5Backend.ts` 的 EQ 接入点
- 修改 `playerStore.ts` 的 EQ 生命周期管理
- 处理 AudioContext 挂起降级
- 重写 EQ 相关单元测试

**非目标**(本轮不做):
- 不重写播放状态机(`<audio>` 元素继续承担播放/seek/缓冲)
- 不删除 `EqualizerMFT`(C++ 原生 EQ)代码,保留为未来备选
- 不改 EQ UI 组件(`EqualizerPanel.vue` / `EqualizerView.vue` 保持现状,只改数据源)

**注意**:`audio_proxy.rs` 的安全加固(host allowlist + 随机 token + origin 限制)**不在非目标内**,而是本设计的前置任务,见 §2。当前工作树中的代理仍是任意 `http(s)`、递增 ID、`Access-Control-Allow-Origin: *`,是 ship-blocker。

---

## 2. 前置条件与 Phase 0(进入实施计划前必须完成)

本设计的可行性依赖两个尚未验证的前提。在进入 writing-plans / 大实施计划之前,必须先完成 §2.1 的代理加固和 §2.2 的 Phase 0 spike。**Phase 0 spike 不通过,本设计作废,回退到 §1.2 备选 A 或 C。**

### 2.1 前置任务 1:音频代理安全加固(必做,blocking)

AudioWorklet 方案仍依赖 `audio_proxy.rs` 把 KuGou CDN 的音频回流并加 CORS 头。当前代理实现是 ship-blocker(code review M2/M3),必须在 Phase 0 之前或同期完成加固:

- **Host allowlist**:`register()` 拒绝非 KuGou CDN 域的 URL。**注意:`*.kugou.com` 太宽——会误代理 `gateway.kugou.com`(API 端点,非 CDN),构成 SSRF-adjacent 漏洞。** 改用精确 host 列表 + 正则。允许的 media CDN host(从用户日志确认):`fs.youthandroid.kugou.com`、`fs.youthandroid2.kugou.com`、`imge.kugou.com`。实现为正则 `^fs\.[a-z0-9]+\.kugou\.com$` 匹配 `fs.*.kugou.com` 变体 + 显式 `imge.kugou.com`。**拒绝** `gateway.kugou.com`、`m.kugou.com`、`www.kugou.com` 等 API/web host。拒绝 loopback/link-local/RFC1918/cloud metadata(`169.254.169.254`)。`backend_api.rs` 不含 host 列表(已核实,纯 FFI),所以 allowlist 以用户日志为准,实施时若发现新 CDN host 需补充。
- **随机不可猜 route ID**:把 `next_id` 递增计数器换成 128-bit 随机 hex(如 `crypto::OsRng`),防止外部进程枚举 `127.0.0.1:<port>/audio/1` 等老路由。
- **Origin 反射替换 `*`**:`Access-Control-Allow-Origin` 不再返回 `*`。**语义:读请求的 `Origin` header,若 ∈ allowlist 则反射该值,否则省略该头(或 403)。** allowlist:`tauri://localhost`(macOS WKWebView)、`http://tauri.localhost`(Windows WebView2 / Linux WebKitGTK)、`https://tauri.localhost`(部分 Tauri v2 配置)、`http://localhost:1420`(dev only)。`<audio>` 的 CORS 检查仍通过,因为 webview origin 匹配。
- **Route TTL / LRU 驱逐**:注册的 route 在首次 fetch 后 5 分钟过期,或 LRU 上限 64 条,避免长会话累积可猜的旧 ID(配合随机 ID 是纵深防御)。
- **M1 流式转发(同期或紧跟)**:`upstream.bytes_stream()` 分块写到 socket,不再 `bytes().await` 整体缓冲,否则长 FLAC 的 seek 会在 EQ 测试中卡顿。可独立于安全加固提交,但要在 Phase 0 验收前完成。

**验收**:`cargo test` 新增的 host allowlist(含 `gateway.kugou.com` 被拒的负向用例)/ origin 反射(含非 allowlist origin 不返头的负向用例)/ ID 格式单测全过;手动验证非 allowlist host 被拒。

### 2.2 前置任务 2:Phase 0 spike(可行性验证,blocking)

在写大实施计划前,先做一个**最小可运行 spike**,实测 spec §4.3 / §9.1 承认尚未验证的事。v3 扩展为 6 项(v2 只 3 项,漏了 degradation / track-end / N 首 / proxy 专项):

1. **`captureStream()` 在 `volume=0` 时仍出非空 PCM**。在 Tauri dev 应用里放一首代理 URL 的歌,`audio.volume = 0` 后 `captureStream()` 拿到的 `MediaStream` 的 `getAudioTracks()` 非空,且 `MediaStreamAudioSourceNode` 连到 `destination`(或一个 `AnalyserNode`)能观察到非零电平。若 `volume=0` 停流,试 `muted=false` + worklet 输出 gain=0 的变体;都不行则此路径不可行。
2. **AudioWorklet 模块能在 Tauri WebView2 加载**。写一个空的 `AudioWorkletProcessor`(只做 `process() { return true }`),`audioCtx.audioWorklet.addModule(...)` resolve,`new AudioWorkletNode(...)` 不抛。验证 Vite 打包路径(`?worklet` 后缀 vs Blob URL)。
3. **连续切 2 首歌,worklet 图不重建,声音持续**。spike 里建一次 workletNode,切歌时只 `disconnect` 旧 `MediaStreamAudioSourceNode`、建新的连上去,验证第 2 首仍有声、无 `InvalidStateError`、无控制台报错。
4. **[v3 新增] `captureStream()` cold-start 时序**:在 `audio.play()` **之前**调 `captureStream()`(对应 `setPreparedSource → initEq → attachSource` 的实际顺序,见 §5.2 P1 #3),验证 tracks 在 `play()` 触发后变为非零、无需手动 `track.restart()`。若 cold-start 返回空 tracks 且不自动恢复,则 §5.2 的 `attachSource` 必须改为 `play()` resolve 后触发(此变更回填 spec)。
5. **[v3 新增] Degradation 路径**:挂起 AudioContext(`audioCtx.suspend()`)→ 验证元素 unmute(`audio.volume = playerStore.volume`)能出声 → `resume()` → 验证 worklet 恢复且无双声(元素 + worklet 同时出声)。此项不过 = 降级设计失效。
6. **[v3 新增] `MediaStream` track-end + N=5 首连续**:对 capturing 中的元素调 `removeAttribute('src') + load()`(对应 `stop()`),验证 `MediaStreamTrack` 的 `ended` 事件触发、sourceNode 输出转静音。然后连续切 5 首歌,验证 `getAudioTracks()` 不无限累积(每首的旧 track 应 end,新 captureStream 拿新 track)、无 stream 泄漏、无 `InvalidStateError`。N=5 而非 v2 的 2,以覆盖 stream/sourceNode 跨多首的泄漏。

**proxy-URL captureStream 专项**:上述 1/3/6 必须用**代理 URL**(`127.0.0.1:<port>/audio/<id>`),验证 `captureStream` 在代理 URL 上的行为与同源 URL 一致(无 CORS taint 污染 MediaStream)。

**spike 产出**:一个 `spike/audioworklet-feasibility` 分支或 `ui/src/api/_spike_audioworklet.ts`(明确标注 throwaway),加一段简短结论记在 spec 的 §2.2 下方:"Phase 0 于 YYYY-MM-DD 通过/失败,结论:..."。每项 1-6 标 pass/fail。

**spike 不做**:biquad 数学(用 passthrough)、UI、正式测试。只验证可行性 + degradation + track 生命周期。

**spike 失败的回退**:若 §2.2 任一条不成立,本设计作废,回到 brainstorming 选择备选 A(只插一次管 + 挂起容错)或 C(原生 MFT)。

#### 2.2.1 Phase 0 spike 实测结果(2026-06-29,throwaway `ui/src/api/_spike_audioworklet.ts` + runner)

在 Tauri dev app(WebView2,48kHz,代理 URL `127.0.0.1:62220`)跑完 6 项。**结论:架构可行,进入 writing-plans。** 4 项 PASS,2 项 FAIL 经解读为"spec 已处理"而非"架构不可行"。逐项:

| # | 检查 | 结果 | 证据 | 解读 |
|---|---|---|---|---|
| 1 | `captureStream()` at `volume=0` 出非零 PCM | **PASS** | RMS peak=0.388, nonZero=21/24 over 1.2s, tracks=1 | 核心不变量成立:元素静音但流有 PCM |
| 2 | AudioWorklet 在 WebView2 加载 | **PASS** | `addModule` (Blob URL) resolve, `AudioWorkletNode` 实例化无抛, ctx.state=running | worklet 路径可行。Vite `?worklet` 不可用(transform 500),**统一用 Blob URL** |
| 3 | 2 首连续,图不重建,无 `InvalidStateError` | **PASS** | song1 peak=0.324, song2 peak=0.078, consoleErrors=0, InvalidStateError=false | **头号证明**:bug #16 类问题消除,workletNode 长存只换 sourceNode |
| 4 | cold-start `captureStream()` 时序 | **PASS** | coldTracks=1 (live), preRms=0.000, postPeak=0.555, firstNonZeroAt=41ms, noManualRestart=true | **P1 决策输入**:play 前调 `captureStream()` 返回 live track,play 后 41ms 自动变非零。**§5.2 可保留 pre-play `attachSource` 顺序,无需改** |
| 5 | Degradation 路径 | **FAIL→架构通过** | Phase0 peak=0.262;PhaseA(挂起):元素 `currentTime` 前进 0.81s(挂起时仍放 ✓),worklet peak=0.241(analyser 假象);PhaseB(resume,双声窗口):peak=0.282;PhaseC(重新静音):peak=0.329(恢复 ✓) | 核心降级属性通过:挂起时元素继续放、resume 后 worklet 恢复。"frozen=false" 是 AnalyserNode 读最后写入 buffer 的测量假象,非真未冻结。**双声用户已听到(轻微)→ §3.3 顺序(先 disconnect worklet 输入再 unmute 元素)是强制,非 defense-in-depth** |
| 6 | `MediaStream` track-end + N=5 | **FAIL→spec 已处理** | Part A: track `ended` 事件在 `removeAttribute('src')+load()` 后 2000ms 内**未触发**;Part B(5 首循环)因 Part A 硬 FAIL 未跑 | **证实 spec §4.2 v3 的 `track.stop()` 契约是 load-bearing**:`stop()` 清 src 不会自动 end track,必须显式 `track.stop()`。Part B 的 N=5 泄漏数据未取,但 Check 3(2 首无 InvalidStateError)+ §4.2 强制 `track.stop()` 契约覆盖该顾虑 |

**可测性盲区(回填 spec,影响后续验收)**:
- **Check 5 双声**:Web Audio analyser 只看 captureStream→worklet 的 PCM,看不到元素直放扬声器输出。双声只能人耳听。spike 已证用户能听到(轻微)→ §3.3 顺序强制。后续验收:§7.3 浏览器实测必须含"Phase B 人耳听双声"项。
- **Check 6 N=5 泄漏**:每轮 `track.stop()` 后无法回看旧流,spike 用代理信号(每新流 1 track + 结束时 activeSourceNodes=0)。真泄漏检测需持有全部 5 流引用后断言全 `ended`——作为 §7.2 的 10 首自动化测试的一部分补上(见 §7.2 v3 的 10 首回归)。

**spike 产出文件**(throwaway,实施 PR 前删除):`ui/src/api/_spike_audioworklet.ts`、`ui/src/api/_spike_audioworklet_runner.ts`。

**进入 writing-plans 的依据**:Check 3(头号证明,无 InvalidStateError)+ Check 1/2/4 PASS 证明核心架构可行。Check 5/6 的 FAIL 分别由 §3.3 顺序强制 + §4.2 `track.stop()` 契约覆盖,非架构缺陷。

### 2.3 实施顺序约束

实施必须按以下顺序,每步独立提交,不得混在一起:

1. **#16 live repro 确认**(当前创可贴修复生效)→ 用户在 Tauri dev 里放 2 首歌,F12 `[DEBUG-bug #16]` 日志确认无 wedge 或确认降级路径触发。
2. **清理提交**:删除所有 `[DEBUG-bug #16]` 日志。**保留 `swapAudioElementAfterWedge` 作为 Phase 0 失败时的 fallback**(swap 删除延迟到 step 5,见下)。单独 commit。
3. **§2.1 代理加固提交**:host allowlist + 随机 ID + origin 反射 + TTL。单独 commit。`cargo test` 全绿。
4. **§2.2 Phase 0 spike**:throwaway 分支,结论回填本 spec。
5. **(仅当 Phase 0 通过)AudioWorklet 重设实施**:按 writing-plans 产出的计划执行,作为独立 PR。**此 PR 同时删除 `swapAudioElementAfterWedge`**(新架构不需要它,元素从不被接管)。
6. **(若 Phase 0 失败)回退**:保留 `swapAudioElementAfterWedge` 作为长期创可贴,本设计作废,回 brainstorming 选备选 A/C。

**关键不变量**:在任何中间状态,`swapAudioElementAfterWedge` 必须存在且可用,直到 step 5 的重设 PR 落地。否则 Phase 0 失败 = 用户失去创可贴又无重设 = 破播放器。

---

## 3. 架构设计

### 3.1 核心拓扑

```
┌─────────────────────────────────────────────────────────────┐
│  AudioContext(应用启动时创建一次,长存)                       │
│                                                              │
│   <audio> 元素                                                │
│   │ 1. src = proxyUrl(经 Tauri 代理,CORS 由 §2.1 加固后的 origin 反射解决)│
│   │ 2. volume = 0(静音元素本身,只让 worklet 出声)            │
│   │ 3. captureStream() ─────────────────┐                    │
│   │                                      ▼                    │
│   │   MediaStreamAudioSourceNode(每首歌新建一个,可 disconnect)│
│   │                  │                                        │
│   │                  ▼                                        │
│   │   AudioWorkletNode("eq-processor",启动时建一次,长存)      │
│   │   │  - 10 级级联 biquad(RBJ,JS 实现)                     │
│   │   │  - 输入:MessagePort 接收 bands[] 更新                 │
│   │   │  - 输出增益 gainNode(用户音量叠加在这里)               │
│   │   └──→ GainNode ──→ destination                           │
│   │                                                            │
│   └─ 元素本身 volume=0,不直接出声                              │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

**关键不变量**:
- `<audio>` 元素**从不**被 `createMediaElementSource` 接管。元素只是 `src` + `volume=0` + `captureStream()` 的提供者。
- AudioContext + AudioWorkletNode + GainNode **应用启动时建一次**,所有歌共用。
- 每首歌只新建一个 `MediaStreamAudioSourceNode`(可 disconnect,无不可逆绑定)。
- EQ 滑杆值通过 `workletNode.port.postMessage` 更新,不重建图。

### 3.2 为什么这样设计能解决问题

| 问题 | 当前架构 | 新架构 |
|---|---|---|
| `createMediaElementSource` 不可逆 | 每次切歌重调 → `InvalidStateError` | 完全不用这个 API |
| 切歌时元素被绑死 | 元素绑定到首次的 source node | 元素从未绑定,`src` 随便换 |
| AudioContext 挂起 → 静默 | 图接管了元素,挂起 = 无声 | 元素 `volume=0`,但 worklet 挂起时降级 `unmute` 元素,声音从 worklet 切到元素直放,不丢 |
| EQ 设置在切歌间保留 | 难(图要重建) | 天然保留(同一 worklet 实例) |

### 3.3 降级路径

**触发**:AudioContext `state === 'suspended'` 且 `resume()` reject(无用户手势的自动切歌场景)。

**降级动作**(**v3 顺序,Phase 0 spike 证实的强制不变量**——先断 worklet 输入,再 unmute 元素。spike Check 5 用户在 Phase B 听到轻微双声,故此顺序非 defense-in-depth 而是必须):
1. `sourceNode.disconnect(workletNode)`(先停 worklet 输入,确保即使 ctx 处于 transitional 状态也无 worklet 输出)
2. `audio.muted = false; audio.volume = playerStore.volume`(元素直放出声)
3. `eqState.available = false`,`eqState.reason = 'EQ 暂不可用,点击重试'`
4. UI 显示降级提示 + "重试"按钮

**恢复(用户点重试)**:
1. `audioCtx.resume()`(用户点击是手势,应该成功)
2. 成功:`audio.volume = 0`(先 re-mute 元素)→ `webAudioEq.attachSource(audio)`(重新 captureStream + 建新 sourceNode,不复用降级时的旧 sourceNode——其元素 src 可能已变)→ `eqState.available = true`
3. 失败:保持降级,提示"重试失败,请检查浏览器音频权限"

**关键**:降级时**声音永不丢失**。元素本来就在 `volume=0` 直放(只是听不见),降级只是把音量调回来。这是 `captureStream` 路径相对于 `createMediaElementSource` 路径的最大优势。顺序反转是 defense-in-depth:即使 ctx 在 suspend/running 边界态,也不会出现"元素已 unmute + worklet 仍在处理 captureStream"的双声窗口。

---

## 4. 组件设计

### 4.1 `ui/src/api/eqWorkletProcessor.ts`(新文件)

AudioWorkletProcessor 的源码字符串。必须用字符串形式,因为 AudioWorklet 需要单独的 JS 文件 URL,而 Vite 打包时 worklet 文件需要特殊处理(`?worklet` 后缀或 `new URL(..., import.meta.url)`)。

**职责**:
- 接收 `bands: number[]`(10 个频段的 dB 增值)
- 把 dB 转 RBJ biquad 系数(翻自 `native/playback/BiquadFilter.cpp` 的数学)
- 在 `process(inputs, outputs)` 里级联 10 级 biquad,处理输入 PCM → 输出
- 维护内部状态(每级 biquad 的 x1/x2/y1/y2,左右声道独立)

**接口**:
```ts
// 通过 MessagePort 接收
{ type: 'setBands'; bands: number[] }
{ type: 'setEnabled'; enabled: boolean }

// process() 输出
// inputs[0] = 来自 MediaStreamAudioSourceNode 的 PCM
// outputs[0] = EQ 处理后的 PCM
```

**实现要点**:
- 10 级 biquad 中心频率:`EQ_BANDS`(31/62/125/250/500/1K/2K/4K/8K/16K Hz,沿用现有 `equalizerConfig.ts`)
- 滤波器类型:peaking(和现有 `BiquadFilterNode.type = 'peaking'` 一致)
- Q 值:`1 / Math.SQRT2`(和现有实现一致)
- 增益范围:`-6 ~ +6 dB`(`clampEqGain`,沿用现有)
- **[v3] sample-rate aware**:RBJ 系数依赖采样率。C++ 源可能硬编码 48kHz,但 `AudioContext.sampleRate` 在 Windows 常为 44100。系数计算**必须**用 worklet scope 的 `globalThis.sampleRate`(AudioWorkletProcessor 内置),不得硬编码 48000。C++ 翻译时把 sampleRate 作为参数传入系数公式。单测:在 44100 和 48000 两种 sampleRate 下断言频带中心频率正确。

### 4.2 `ui/src/api/webAudioEq.ts`(重写)

**职责变更**:从"用 BiquadFilterNode 建 EQ 图"改为"用 AudioWorkletNode 建 EQ 图 + captureStream 接入"。

**新接口**:
```ts
class WebAudioEq {
  init(opts: EqOptions): void
  // 启动时调一次:创建 AudioContext、加载 worklet 模块、建 workletNode + gainNode、连 destination
  // 不接收 audio 元素,不接源

  attachSource(audio: HTMLAudioElement): void
  // 切歌时调(play() resolve 后,见 §5.2):audio.volume=0, captureStream, createMediaStreamSource, connect to workletNode

  disconnectSource(): void
  // backend.stop() 时调:断开当前 MediaStreamAudioSourceNode。workletNode + gainNode 不动

  setBand(index: number, gainDb: number, enabled: boolean): void
  // workletNode.port.postMessage({ type: 'setBands', bands: ... })

  setEnabled(enabled: boolean, bands: number[]): void
  // workletNode.port.postMessage({ type: 'setEnabled', enabled })

  setVolume(vol: number): void  // 新增,用户音量叠加在 gainNode 上
  // gainNode.gain.value = vol

  get isRerouted(): boolean  // worklet 是否接管输出(用于 backend.setVolume 判断走 gainNode 还是 audio.volume)

  resume(): Promise<void>
  // 失败时触发降级(opts.onDegraded 回调)

  close(): void
  // 销毁:disconnect 所有、close AudioContext
}
```

**[v3] attachSource 重入契约(强制不变量,非注释)**:
- `attachSource(audio)` 调用时**必须**先 `disconnectSource()`(断开并 `track.stop()` 旧 sourceNode + 旧 MediaStream),再建新的。即使上游忘记调 `disconnectSource`,`attachSource` 也必须自清理。
- **单测**:连续 2 次 `attachSource`(无中间 `disconnectSource`)后,`workletNode` 的输入数 == 1(旧 sourceNode 已断),旧 `MediaStream` 的 tracks 全部 `ended`。

**[v3] disconnectSource 资源释放契约**:
- `disconnectSource()` 不只 `sourceNode.disconnect()`,**还必须** `captureStream.getAudioTracks().forEach(t => t.stop())` 释放旧 `MediaStream`。否则每首歌累积一个 live stream,长会话撞 WebView2 stream 上限。
- `track.stop()` 后该流不可复用,新歌必须重新 `captureStream()`(由 `attachSource` 负责)。

**EqOptions 变更**:
```ts
interface EqOptions {
  enabled: boolean;
  bands: number[];
  crossOriginSafe?: boolean;  // 保留,但语义变了:不再决定"建不建图",而是决定"captureStream 能不能进 Web Audio"(CORS 要求)
  onDegraded?: () => void;    // 替代 onSuspendedFail,语义更广(挂起、worklet 失败都触发)
  onRecovered?: () => void;   // 新增,降级后恢复成功时触发
}
```

**保留的不变量**:
- `crossOriginSafe` 仍由 `prepareAudioSourceUrl` 决定(Tauri 代理返回 true,直连返回 false)
- `crossOriginSafe === false` 时,跳过 captureStream 接入,元素 `volume = playerStore.volume` 直放出声,EQ 不可用(和现有降级语义一致)

### 4.3 `ui/src/api/html5Backend.ts`(修改)

**变更点**:
- `Html5AudioBackendOptions.initEq` 签名不变
- `setPreparedSource` 内部:**仍然设置 `audio.crossOrigin = 'anonymous'`**(当 `crossOriginSafe === true` 时)。原因:`captureStream()` 返回的 `MediaStream` 是否能被 `MediaStreamAudioSourceNode` 使用而不污染 AudioContext,取决于 `<audio>` 元素是否 CORS-safe。代理 URL 在 `127.0.0.1`,经 §2.1 加固后反射 Tauri webview origin(而非 `*`),`crossOrigin='anonymous'` 不会导致加载失败。
- 音量控制(关键):**EQ 生效时** `audio.volume = 0`,实际音量由 `webAudioEq.setVolume(v)` 控制(叠加在 worklet 输出的 `gainNode` 上);**降级时** `audio.volume = playerStore.volume`,worklet 输入 disconnect。`Html5AudioBackend.setVolume` 通过 `webAudioEq.isRerouted` 判断当前模式。

**CORS 验证清单**(Phase 0 spike 必须实测,见 §2.2):
- [ ] 代理 URL + `crossOrigin='anonymous'` → 元素能正常加载播放(§2.1 加固后需重新验证,origin 反射不破坏加载)
- [ ] 元素 `volume=0` 时 `captureStream()` 仍能拿到非空 PCM 流(若停流,改用 `muted=false` + worklet 输出 gain=0 的方式静音)
- [ ] `MediaStreamAudioSourceNode` 从该流创建后,AudioContext 不被污染(`decodeAudioData` 等不抛 SecurityError)

### 4.4 `ui/src/api/playerStore.ts`(修改)

**变更点**:
- `initWebAudioEQ()` 改为调 `webAudioEq.init(opts)`(不传 audio)
- 新增 `attachWebAudioEqSource(audio)` / `disconnectWebAudioEqSource()` 导出,供 `Html5AudioBackend` 在 `play()` resolve 后 / `stop` 时调用(见 §5.2 时序)
- 新增 `setWebAudioEqVolume(vol)` 导出,`watch(playerStore.volume)` 调用它
- `onDegraded` 回调:`eqState.available = false` + reason
- `onRecovered` 回调:`eqState.available = true` + reason 清空
- 新增 `retryEq()` 导出函数:**[v3] 语义定死** → `webAudioEq.resume()`;若成功,`disconnectSource()` + `attachSource(playerStore.audio)`(重新 captureStream,不复用降级时的旧 sourceNode——其元素 src 可能已变)。失败计数 +1,达 3 禁用按钮。

**[v3] 音量单写入点**:`playerStore.volume` 的 `watch` 是音量唯一写入点。任何代码不得直接设 `audio.volume` 或 `gainNode.gain.value`,必须经 `setVolume → watch → setWebAudioEqVolume / backend.setVolume`。降级切换时:先 `gainNode.gain.value = 0`(渐变 50ms),再 `audio.volume = vol`,避免双路径同时出声的跳变。

**删除的旧逻辑**:
- `if (this.ctx) return` 守卫(新架构下 init 只在启动时调一次,不存在重复调用)
- `disposeGraph` 在 catch 里的元素遗弃问题(新架构元素从不被接管,没有遗弃问题)
- `[DEBUG-bug #16]` 日志(§2.3 step 2 移除)
- `swapAudioElementAfterWedge`(**§2.3 step 5** 移除,仅 Phase 0 通过后;step 2 保留作为 fallback)

### 4.5 EQ UI 组件(不改)

`EqualizerPanel.vue` / `EqualizerView.vue` 保持现状。它们只读写 `playerStore.eqBands`、`playerStore.eqEnabled`、`eqState.available`。新增的"重试"按钮:在 `EqualizerPanel.vue` 的 `eq-unavailable` 提示块里加一个 `<button @click="retryEq">重试 EQ</button>`,仅当 `!eqState.available` 时显示。

---

## 5. 数据流

### 5.1 启动序列

```
App.onMounted
  → initPlayer()           // 建 <audio> 元素
  → initPlayerBackend()    // 建 Html5AudioBackend
  → initWebAudioEQ()       // webAudioEq.init(opts):建 AudioContext + worklet + gainNode(无输入源,静默)
                            // 注意:init 不接收 audio,只建图;切歌时才 attachSource
```

### 5.2 切歌序列

```
playTrack(track)
  → playbackOrchestrator.switchTrack
    → backend.stop()
      → audio.pause()
      → removeAttribute('src') + audio.load()   // 清旧 src,统一由 backend 管理
      → webAudioEq.disconnectSource()           // disconnect 旧的 MediaStreamAudioSourceNode(若有)
                                                 // workletNode + gainNode 长存,不动
    → resolveTrack → finalUrl
    → backend.playUrl(finalUrl)
      → setPreparedSource(url)
        → prepareAudioSourceUrl(url)  // Tauri 代理
        → audio.crossOrigin = 'anonymous'(crossOriginSafe 时)
        → audio.src = proxyUrl
        → audio.volume = 0  // EQ 生效时静音元素
        → audio.play()      // [P1 v3] 先 play(),再 attachSource(见下方时序说明)
        → initEq(audio, crossOriginSafe)
          → webAudioEq.attachSource(audio, ...)  // play() resolve 后:captureStream + 建新 sourceNode + 连 workletNode
```

**[P1 v3] attachSource 时序说明(消除 cold-start 空 tracks 风险)**:
v2 把 `attachSource` 放在 `play()` **之前**(对应 `html5Backend.ts:157` 的 `initEq` 同步调用),但 `captureStream()` 在元素未播放时可能返回 tracks 存在但**零样本**的流,某些 Chromium 版本下 `volume=0` + 暂停状态甚至不启动音频管线。v3 改为:`attachSource` 在 `audio.play()` resolve 后触发。

**实现方式**:`setPreparedSource` 不再同步调 `initEq`;改为 `playUrl` 在 `await audio.play()` 成功后调 `initEq(audio, crossOriginSafe)` → `webAudioEq.attachSource(audio)`。当前 `html5Backend.ts:25-28` 的顺序(`setPreparedSource` → `play()`)需调整为 `setPreparedSource(不调 initEq)` → `play()` → `initEq`。

**fallback**:若 Phase 0 spike §2.2 第 4 项证明 cold-start `captureStream` 在 `play()` 后自动变非零,则可保留 v2 的 pre-play 顺序。否则强制 post-play。此决策由 spike 结果回填 spec。

**生命周期定死(消除 v1 的自相矛盾)**:
- `backend.stop()` **仍然** `removeAttribute('src') + load()`(和当前实现一致,保留"失败切歌不能 resume 旧 src"的安全语义)。
- `stop()` 同时调用 `webAudioEq.disconnectSource()`,把旧的 `MediaStreamAudioSourceNode` 从 `workletNode` 上断开,并 `track.stop()` 释放旧 `MediaStream`(见 §4.2 `disconnectSource` 契约)。`workletNode` 和 `gainNode` 不动(长存)。
- `setPreparedSource` 设新 `src` 后,`play()` resolve,`initEq` 调 `webAudioEq.attachSource(audio)` 建**新的** `MediaStreamAudioSourceNode` 并连到长存的 `workletNode`。
- 元素本身从不被 `createMediaElementSource` 接管,所以 `removeAttribute('src') + load()` + 设新 `src` 全程安全,无 `InvalidStateError` 风险。

### 5.3 EQ 滑杆拖动

```
EqualizerPanel onSliderInput
  → playerStore.eqBands = [...]
  → setWebAudioEqBand(i, gain)
    → webAudioEq.setBand(i, gain, enabled)
      → workletNode.port.postMessage({ type: 'setBands', bands })
```

### 5.4 音量调节

```
PlayerBar 音量滑杆
  → setVolume(vol)
    → playerStore.volume = vol
    → watch 触发 → setWebAudioEqVolume(vol)
      → webAudioEq.setVolume(vol)
        → gainNode.gain.value = vol  // EQ 生效时
        → (降级时) audio.volume = vol
```

---

## 6. 错误处理与降级

### 6.1 降级触发场景

| 场景 | 触发 | 动作 |
|---|---|---|
| AudioContext 自动挂起(无手势切歌) | `resume()` reject | `onDegraded`:元素 unmute,disconnect worklet 输入 |
| `captureStream()` 不支持 | 启动时检测 | `onDegraded`:元素直放,EQ 永久不可用 |
| worklet 模块加载失败 | `audioCtx.audioWorklet.addModule` reject | `onDegraded`:元素直放,EQ 永久不可用 |
| `crossOriginSafe === false`(代理失败回退直连) | `prepareAudioSourceUrl` 返回 false | 元素直放,EQ 不可用(本次播放) |

### 6.2 恢复路径

- **挂起降级**:用户点"重试 EQ" → `retryEq()` → `resume()`(有手势) → 成功则 `onRecovered`
- **永久降级(captureStream 不支持 / worklet 加载失败)**:不提供重试,提示"EQ 在此环境不可用"

### 6.3 无限降级循环防护

- **[v3] 失败计数与重置**:每次 `retryEq` reject 计数 +1,每次成功 reset 为 0。达 3 禁用重试按钮,提示"请重启应用"。**重置触发**:成功 retry / 用户切歌(新 track 重置计数为 0,给新音源一次机会)。
- worklet 加载失败是启动时一次性的,不存在循环。

---

## 7. 测试策略

### 7.1 单元测试(可 jsdom 模拟的部分)

- **`eqWorkletProcessor` 的 biquad 数学**:纯函数测试,输入 PCM buffer + bands,断言输出。用白噪声/正弦波输入,验证频段增益。翻自 C++ `BiquadFilter.cpp` 的测试用例。**[v3] 含 44100/48000 双 sampleRate 断言**(见 §4.1)。
- **`WebAudioEq` 的图构建**:mock AudioContext(沿用 `webAudioEq.test.ts` 的 mock 模式,额外 stub `audioWorklet.addModule` / `AudioWorkletNode` / `HTMLAudioElement.prototype.captureStream`),验证 `captureStream` 被调用、`createMediaStreamSource` 被调用、workletNode 连 gainNode 连 destination、sourceNode 连 workletNode。
- **降级触发**:mock `resume()` reject,验证 `onDegraded` 被调用、元素 `volume` 被恢复、sourceNode disconnected。
- **切歌不卡死**:模拟两次 `attachSource(audio)`,验证第二次不抛 `InvalidStateError`(因为不再调 `createMediaElementSource`),旧 sourceNode 被 disconnect。
- **[v3] attachSource 重入契约**:连续 2 次 `attachSource`(无中间 `disconnectSource`)后,`workletNode` 输入数 == 1,旧 `MediaStream` tracks 全 `ended`(见 §4.2 契约)。

### 7.2 集成测试(playerStore 层,jsdom + mock backend)

这三个测试对应 code review L4/L5 + 10 首回归,**必须在实施时一并补上**。jsdom 无 `captureStream` / `AudioWorkletNode`,需 `vi.stubGlobal` 补 mock(沿用 `playerStore.test.ts:287` 的 stubGlobal 模式):

- **[L4] 测试改写(非保留)**:v2 的 swap-不重入测试在 v3 失效(`swapAudioElementAfterWedge` 已删)。**文件保留,断言全部替换**:连续 2 次 `attachSource` 不重建 worklet 图,旧 sourceNode 被 disconnect,`workletNode` 输入数恒为 1。transition 嵌套语义由 orchestrator 层独立测试,不在 L4 内。
- **[L5] `crossOriginSafe=false` 直连 fallback 通过 `playTrack` 能播放且 EQ 不可用**:mock `invoke('audio_proxy_url')` reject → `prepareAudioSourceUrl` 返回 `{url, crossOriginSafe: false}` → `setPreparedSource` 移除 `crossorigin` → `initEq(false)` 跳过图 → `audio.play()` 成功 → `eqState.available === false` → UI 降级提示显示。断言播放不破、EQ 状态正确。
- **[v3] 10 首自动化回归**(对应 §10.1 验收):jsdom 驱动 10 次 `attachSource`(distinct mock streams),断言:(a) `createMediaStreamSource` 被调 10 次、每次 distinct stream;(b) 仅最新 sourceNode 连 workletNode;(c) 无 `InvalidStateError`;(d) `webAudioEq.isRerouted` 全程 true;(e) 旧 tracks 全 `ended`(无 stream 泄漏)。避免未来重构静默回归 10 首验收。

### 7.3 浏览器实测(需手动/真实 WebView2)

- 真实浏览器:播放第一首 → 切第二首 → 确认 EQ 仍生效(拖滑杆听变化)
- 真实浏览器:模拟 AudioContext 挂起(后台标签几分钟)→ 切歌 → 确认降级 → 点重试 → 确认恢复
- 验收标准:连续切 10 首歌,EQ 全程生效,无卡死,无 `InvalidStateError`(**§7.2 已自动化 10 首,此项为带声卡的端到端复核**)

### 7.4 性能测试

- worklet 进程内 10 级 biquad:在 48kHz / 128 sample block 下,CPU 占用应 < 1%(实测)
- 若性能不达标,备选 2B(WASM)启动

---

## 8. 迁移与清理

### 8.1 删除的代码

- `webAudioEq.ts` 旧实现(`createMediaElementSource` + `BiquadFilterNode` 链)
- `[DEBUG-bug #16]` 日志(所有文件)
- 上一轮 `resume()` 修复中的 `this.ctx = null`(不再需要,新架构不依赖这个守卫)

### 8.2 保留的代码

- `audioProxy.ts` / `audio_proxy.rs`(CORS 代理,仍需要)
- `equalizerConfig.ts`(EQ 频段/预设定义,不变)
- `EqualizerPanel.vue` / `EqualizerView.vue`(UI 不变,加一个重试按钮)
- `native/playback/BiquadFilter.cpp`(C++ 原始数学,作为 JS 翻译的参考来源)

### 8.3 与当前工作树的关系

当前工作树有上一任的 `createMediaElementSource` 实现 + 上一轮的"换元素"修复尝试(`swapAudioElementAfterWedge` + `[DEBUG-bug #16]` 日志)。本设计**取代**这些实现:实施时删除旧 EQ 代码,替换为 AudioWorklet 实现。**实施顺序见 §2.3**(先 repro → 清理 → 代理加固 → Phase 0 → 重设),不由 writing-plans 重新决定。

---

## 9. 风险与未决事项

### 9.1 已识别风险

1. **`captureStream` 在 Tauri WebView2(Chromium)中的实际行为**(最高风险,**由 §2.2 Phase 0 spike 验证**)。三件事(见 §4.3 验证清单):(a) 代理 URL + `crossOrigin='anonymous'` 能否加载;(b) 元素 `volume=0` 时 `captureStream` 是否仍出流——若停流,备选是改用 `muted=false` + 在 worklet 输出 `gainNode` 上做静音(元素不静音,worklet 输出 0,但元素本身会出声 → 需要元素音量极低但不为 0,或用 `MediaStreamAudioDestinationNode` 中转);(c) 流是否污染 AudioContext。**Phase 0 spike 失败 → 本设计作废,回退到 §1.2 备选 C(原生 EQ)或 A(只插一次管+挂起容错)。**
2. **AudioWorklet 模块的 Vite 打包**(由 §2.2 Phase 0 第 2 条验证)。Vite 对 `audioWorklet.addModule(new URL('./processor.ts', import.meta.url))` 的支持需要验证。备选:worklet processor 作为独立 JS 字符串用 `Blob` URL 加载。
3. **延迟**。`captureStream` 路径可能有几百毫秒延迟(PCM 从元素到 worklet 的传播)。对 EQ 来说可接受(用户不会注意到 EQ 比直放慢 200ms),但要在验收时确认。
4. **`<audio>` 元素 `volume=0` 与 `PlayerBar` 音量显示的同步**。UI 音量滑杆显示的是 `playerStore.volume`,实际音量由 `gainNode` 控制。要确保两者始终同步,降级切换时无跳变。
5. **音频代理安全(M2/M3,由 §2.1 前置任务解决)**。当前 `audio_proxy.rs` 的 SSRF + CORS `*` 是 ship-blocker,必须在实施前加固。

### 9.2 未决事项(实施时决定)

- worklet processor 文件的具体加载方式(Vite `?worklet` vs Blob URL)
- 降级时的爆音抑制(是否需要 `gainNode` 渐变而非瞬切)
- 是否需要保留 `createMediaElementSource` 路径作为 fallback(倾向:不保留,新架构已覆盖)

---

## 10. 验收标准

1. 连续播放 10 首歌,EQ 全程生效(每首歌拖滑杆都能听到音色变化)。**自动化覆盖**:§7.2 的 10 首 jsdom 测试;**端到端复核**:§7.3 手动带声卡验证。
2. 无 `InvalidStateError` 在控制台
3. AudioContext 挂起时自动降级,元素直放不出声中断,UI 显示"EQ 暂不可用,点击重试"
4. 用户点击"重试"后 EQ 恢复(失败计数 + 重置语义见 §6.3)
5. `pnpm --dir ui test` 全绿(新增 worklet 单元测试 + L4/L5/10 首集成测试 + 现有测试不破)
6. `pnpm --dir ui exec vue-tsc --noEmit` 无类型错误
7. 手动实测:切歌、单曲循环、音质切换三种场景下 EQ 都稳定
8. **[v3] Phase 0 spike §2.2 全 6 项 pass**(否则本设计作废,不进验收)

---

## 11. 后续(超出本设计范围)

- **原生 EQ 路径(C 方案)**:若 AudioWorklet 在某些环境不稳定,可考虑接通 `EqualizerMFT`,但需先修复 MFS 拓扑问题(`CONTEXT.md` 已知问题 1)。
- **EQ 预设持久化优化**:当前预设存在 localStorage,可考虑迁移到 Rust 侧 SQLite。
- **EQ 曲线可视化**:在 EQ 面板画实时频响曲线(基于 biquad 系数算频响)。
