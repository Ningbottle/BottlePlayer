# EQ 子系统重设:AudioWorklet + captureStream

**状态**:Draft(待用户 review)
**日期**:2026-06-28
**作者**:brainstorming session 产出
**关联**:取代当前工作树中的 `createMediaElementSource` EQ 实现(会触发 `InvalidStateError` 卡死)

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
- 不动 Rust/C++ 侧(`audio_proxy.rs` 保留,CORS 已解决)
- 不重写播放状态机(`<audio>` 元素继续承担播放/seek/缓冲)
- 不删除 `EqualizerMFT`(C++ 原生 EQ)代码,保留为未来备选
- 不改 EQ UI 组件(`EqualizerPanel.vue` / `EqualizerView.vue` 保持现状,只改数据源)

---

## 2. 架构设计

### 2.1 核心拓扑

```
┌─────────────────────────────────────────────────────────────┐
│  AudioContext(应用启动时创建一次,长存)                       │
│                                                              │
│   <audio> 元素                                                │
│   │ 1. src = proxyUrl(经 Tauri 代理,CORS 已解决)             │
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

### 2.2 为什么这样设计能解决问题

| 问题 | 当前架构 | 新架构 |
|---|---|---|
| `createMediaElementSource` 不可逆 | 每次切歌重调 → `InvalidStateError` | 完全不用这个 API |
| 切歌时元素被绑死 | 元素绑定到首次的 source node | 元素从未绑定,`src` 随便换 |
| AudioContext 挂起 → 静默 | 图接管了元素,挂起 = 无声 | 元素 `volume=0`,但 worklet 挂起时降级 `unmute` 元素,声音从 worklet 切到元素直放,不丢 |
| EQ 设置在切歌间保留 | 难(图要重建) | 天然保留(同一 worklet 实例) |

### 2.3 降级路径

**触发**:AudioContext `state === 'suspended'` 且 `resume()` reject(无用户手势的自动切歌场景)。

**降级动作**:
1. `audio.muted = false; audio.volume = playerStore.volume`(元素直放出声)
2. `sourceNode.disconnect(workletNode)`(停 worklet 输入)
3. `eqState.available = false`,`eqState.reason = 'EQ 暂不可用,点击重试'`
4. UI 显示降级提示 + "重试"按钮

**恢复(用户点重试)**:
1. `audioCtx.resume()`(用户点击是手势,应该成功)
2. 成功:`audio.volume = 0; sourceNode.connect(workletNode)`,`eqState.available = true`
3. 失败:保持降级,提示"重试失败,请检查浏览器音频权限"

**关键**:降级时**声音永不丢失**。元素本来就在 `volume=0` 直放(只是听不见),降级只是把音量调回来。这是 `captureStream` 路径相对于 `createMediaElementSource` 路径的最大优势。

---

## 3. 组件设计

### 3.1 `ui/src/api/eqWorkletProcessor.ts`(新文件)

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

### 3.2 `ui/src/api/webAudioEq.ts`(重写)

**职责变更**:从"用 BiquadFilterNode 建 EQ 图"改为"用 AudioWorkletNode 建 EQ 图 + captureStream 接入"。

**新接口**:
```ts
class WebAudioEq {
  init(audio: HTMLAudioElement, opts: EqOptions): void
  // 启动时:创建 AudioContext、加载 worklet 模块、建 workletNode + gainNode、连 destination
  // 切歌时:audio.volume=0, captureStream, createMediaStreamSource, connect to workletNode
  // 旧 sourceNode disconnect

  setBand(index: number, gainDb: number, enabled: boolean): void
  // workletNode.port.postMessage({ type: 'setBands', bands: ... })

  setEnabled(enabled: boolean, bands: number[]): void
  // workletNode.port.postMessage({ type: 'setEnabled', enabled })

  setVolume(vol: number): void  // 新增,用户音量叠加在 gainNode 上
  // gainNode.gain.value = vol

  resume(): Promise<void>
  // 失败时触发降级(opts.onDegraded 回调)

  close(): void
  // 销毁:disconnect 所有、close AudioContext
}
```

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

### 3.3 `ui/src/api/html5Backend.ts`(修改)

**变更点**:
- `Html5AudioBackendOptions.initEq` 签名不变
- `setPreparedSource` 内部:**仍然设置 `audio.crossOrigin = 'anonymous'`**(当 `crossOriginSafe === true` 时)。原因:`captureStream()` 返回的 `MediaStream` 是否能被 `MediaStreamAudioSourceNode` 使用而不污染 AudioContext,取决于 `<audio>` 元素是否 CORS-safe。代理 URL 在 `127.0.0.1`,代理返回 `Access-Control-Allow-Origin: *`,所以 `crossOrigin='anonymous'` 不会导致加载失败(和旧架构一致)。
- 音量控制(关键):**EQ 生效时** `audio.volume = 0`,实际音量由 `webAudioEq.setVolume(v)` 控制(叠加在 worklet 输出的 `gainNode` 上);**降级时** `audio.volume = playerStore.volume`,worklet 输入 disconnect。`Html5AudioBackend.setVolume` 通过 `webAudioEq.isRerouted` 判断当前模式。

**CORS 验证清单**(实现阶段必须实测):
- [ ] 代理 URL + `crossOrigin='anonymous'` → 元素能正常加载播放(和旧架构一致,已验证)
- [ ] 元素 `volume=0` 时 `captureStream()` 仍能拿到非空 PCM 流(若停流,改用 `muted=false` + worklet 输出 gain=0 的方式静音)
- [ ] `MediaStreamAudioSourceNode` 从该流创建后,AudioContext 不被污染(`decodeAudioData` 等不抛 SecurityError)

### 3.4 `ui/src/api/playerStore.ts`(修改)

**变更点**:
- `initWebAudioEQ` 适配新 `WebAudioEq` 接口
- 新增 `setWebAudioEqVolume(vol)` 导出,`watch(playerStore.volume)` 调用它
- `onDegraded` 回调:`eqState.available = false` + reason
- `onRecovered` 回调:`eqState.available = true` + reason 清空
- 新增 `retryEq()` 导出函数,供 UI "重试"按钮调用 → `webAudioEq.resume()` + 恢复接入

**删除的旧逻辑**:
- `if (this.ctx) return` 守卫(新架构下 init 只在启动时调一次,不存在重复调用)
- `disposeGraph` 在 catch 里的元素遗弃问题(新架构元素从不被接管,没有遗弃问题)
- `[DEBUG-bug #16]` 日志(清理阶段移除)

### 3.5 EQ UI 组件(不改)

`EqualizerPanel.vue` / `EqualizerView.vue` 保持现状。它们只读写 `playerStore.eqBands`、`playerStore.eqEnabled`、`eqState.available`。新增的"重试"按钮:在 `EqualizerPanel.vue` 的 `eq-unavailable` 提示块里加一个 `<button @click="retryEq">重试 EQ</button>`,仅当 `!eqState.available` 时显示。

---

## 4. 数据流

### 4.1 启动序列

```
App.onMounted
  → initPlayer()           // 建 <audio> 元素
  → initPlayerBackend()    // 建 Html5AudioBackend
  → initWebAudioEQ(audio)  // 建 AudioContext + worklet + gainNode(无输入源,静默)
                            // 注意:initEq 在这里调一次,不是每次切歌调
```

### 4.2 切歌序列

```
playTrack(track)
  → playbackOrchestrator.switchTrack
    → backend.stop()
      → audio.pause()  // 不再 removeAttribute('src') + load(),因为要保留元素给 captureStream
                       // 实际上 stop 仍然清 src,但新 src 设置后重新 captureStream
    → resolveTrack → finalUrl
    → backend.playUrl(finalUrl)
      → setPreparedSource(url)
        → prepareAudioSourceUrl(url)  // Tauri 代理
        → audio.src = proxyUrl
        → audio.volume = 0  // EQ 生效时静音元素
        → initEq(audio, crossOriginSafe)
          → webAudioEq.init(audio, ...)  // 新逻辑:captureStream + connect worklet
        → audio.play()
```

### 4.3 EQ 滑杆拖动

```
EqualizerPanel onSliderInput
  → playerStore.eqBands = [...]
  → setWebAudioEqBand(i, gain)
    → webAudioEq.setBand(i, gain, enabled)
      → workletNode.port.postMessage({ type: 'setBands', bands })
```

### 4.4 音量调节

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

## 5. 错误处理与降级

### 5.1 降级触发场景

| 场景 | 触发 | 动作 |
|---|---|---|
| AudioContext 自动挂起(无手势切歌) | `resume()` reject | `onDegraded`:元素 unmute,disconnect worklet 输入 |
| `captureStream()` 不支持 | 启动时检测 | `onDegraded`:元素直放,EQ 永久不可用 |
| worklet 模块加载失败 | `audioCtx.audioWorklet.addModule` reject | `onDegraded`:元素直放,EQ 永久不可用 |
| `crossOriginSafe === false`(代理失败回退直连) | `prepareAudioSourceUrl` 返回 false | 元素直放,EQ 不可用(本次播放) |

### 5.2 恢复路径

- **挂起降级**:用户点"重试 EQ" → `retryEq()` → `resume()`(有手势) → 成功则 `onRecovered`
- **永久降级(captureStream 不支持 / worklet 加载失败)**:不提供重试,提示"EQ 在此环境不可用"

### 5.3 无限降级循环防护

- `retryEq` 失败计数:连续失败 3 次后禁用重试按钮,提示"请重启应用"
- worklet 加载失败是启动时一次性的,不存在循环

---

## 6. 测试策略

### 6.1 单元测试(可 jsdom 模拟的部分)

- **`eqWorkletProcessor` 的 biquad 数学**:纯函数测试,输入 PCM buffer + bands,断言输出。用白噪声/正弦波输入,验证频段增益。翻自 C++ `BiquadFilter.cpp` 的测试用例。
- **`WebAudioEq` 的图构建**:mock AudioContext(沿用 `webAudioEq.test.ts` 的 mock 模式),验证 `captureStream` 被调用、`createMediaStreamSource` 被调用、workletNode 连 gainNode 连 destination、sourceNode 连 workletNode。
- **降级触发**:mock `resume()` reject,验证 `onDegraded` 被调用、元素 `volume` 被恢复、sourceNode disconnected。
- **切歌不卡死**:模拟两次 `init(audio)`,验证第二次不抛 `InvalidStateError`(因为不再调 `createMediaElementSource`)。

### 6.2 集成测试(需手动/浏览器环境)

- 真实浏览器:播放第一首 → 切第二首 → 确认 EQ 仍生效(拖滑杆听变化)
- 真实浏览器:模拟 AudioContext 挂起(后台标签几分钟)→ 切歌 → 确认降级 → 点重试 → 确认恢复
- 验收标准:连续切 10 首歌,EQ 全程生效,无卡死,无 `InvalidStateError`

### 6.3 性能测试

- worklet 进程内 10 级 biquad:在 48kHz / 128 sample block 下,CPU 占用应 < 1%(实测)
- 若性能不达标,备选 2B(WASM)启动

---

## 7. 迁移与清理

### 7.1 删除的代码

- `webAudioEq.ts` 旧实现(`createMediaElementSource` + `BiquadFilterNode` 链)
- `[DEBUG-bug #16]` 日志(所有文件)
- 上一轮 `resume()` 修复中的 `this.ctx = null`(不再需要,新架构不依赖这个守卫)

### 7.2 保留的代码

- `audioProxy.ts` / `audio_proxy.rs`(CORS 代理,仍需要)
- `equalizerConfig.ts`(EQ 频段/预设定义,不变)
- `EqualizerPanel.vue` / `EqualizerView.vue`(UI 不变,加一个重试按钮)
- `native/playback/BiquadFilter.cpp`(C++ 原始数学,作为 JS 翻译的参考来源)

### 7.3 与当前工作树的关系

当前工作树有上一任的 `createMediaElementSource` 实现 + 上一轮的"换元素"修复尝试。本设计**取代**这些实现:实施时删除旧 EQ 代码,替换为 AudioWorklet 实现。具体实施顺序(先创可贴止血还是直接重设)由 writing-plans 阶段决定,不属于本设计文档范围。

---

## 8. 风险与未决事项

### 8.1 已识别风险

1. **`captureStream` 在 Tauri WebView2(Chromium)中的实际行为**(最高风险)。需要实测三件事(见 §3.3 验证清单):(a) 代理 URL + `crossOrigin='anonymous'` 能否加载;(b) 元素 `volume=0` 时 `captureStream` 是否仍出流——若停流,备选是改用 `muted=false` + 在 worklet 输出 `gainNode` 上做静音(元素不静音,worklet 输出 0,但元素本身会出声 → 需要元素音量极低但不为 0,或用 `MediaStreamAudioDestinationNode` 中转);(c) 流是否污染 AudioContext。**若 (b) 无法解决,整个 captureStream 路径不可行,需回退到 §1.2 的备选 C(原生 EQ)或备选 A(只插一次管+挂起容错)。**
2. **AudioWorklet 模块的 Vite 打包**。Vite 对 `audioWorklet.addModule(new URL('./processor.ts', import.meta.url))` 的支持需要验证。备选:worklet processor 作为独立 JS 字符串用 `Blob` URL 加载。
3. **延迟**。`captureStream` 路径可能有几百毫秒延迟(PCM 从元素到 worklet 的传播)。对 EQ 来说可接受(用户不会注意到 EQ 比直放慢 200ms),但要在验收时确认。
4. **`<audio>` 元素 `volume=0` 与 `PlayerBar` 音量显示的同步**。UI 音量滑杆显示的是 `playerStore.volume`,实际音量由 `gainNode` 控制。要确保两者始终同步,降级切换时无跳变。

### 8.2 未决事项(实施时决定)

- worklet processor 文件的具体加载方式(Vite `?worklet` vs Blob URL)
- 降级时的爆音抑制(是否需要 `gainNode` 渐变而非瞬切)
- 是否需要保留 `createMediaElementSource` 路径作为 fallback(倾向:不保留,新架构已覆盖)

---

## 9. 验收标准

1. 连续播放 10 首歌,EQ 全程生效(每首歌拖滑杆都能听到音色变化)
2. 无 `InvalidStateError` 在控制台
3. AudioContext 挂起时自动降级,元素直放不出声中断,UI 显示"EQ 暂不可用,点击重试"
4. 用户点击"重试"后 EQ 恢复
5. `pnpm --dir ui test` 全绿(新增 worklet 单元测试 + 现有测试不破)
6. `pnpm --dir ui exec vue-tsc --noEmit` 无类型错误
7. 手动实测:切歌、单曲循环、音质切换三种场景下 EQ 都稳定

---

## 10. 后续(超出本设计范围)

- **原生 EQ 路径(C 方案)**:若 AudioWorklet 在某些环境不稳定,可考虑接通 `EqualizerMFT`,但需先修复 MFS 拓扑问题(`CONTEXT.md` 已知问题 1)。
- **EQ 预设持久化优化**:当前预设存在 localStorage,可考虑迁移到 Rust 侧 SQLite。
- **EQ 曲线可视化**:在 EQ 面板画实时频响曲线(基于 biquad 系数算频响)。
