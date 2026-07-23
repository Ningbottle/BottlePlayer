# ADR-0003：共享 audio/HMR 生命周期与 EQ 图安全构建

- **状态**：Accepted
- **日期**：2026-07-23
- **决策者**：架构审计（code evidence）
- **关联文档**：[../wiki/playback-runtime.md](../wiki/playback-runtime.md)、[../wiki/frontend.md](../wiki/frontend.md)、[../wiki/evidence-report.md](../wiki/evidence-report.md)

## 上下文

BottleMusic 的均衡器（EQ）基于 Web Audio API AudioWorklet 实现，需要将 HTML5 `<audio>` 元素接入 AudioContext 图。**实际拓扑只使用 `captureStream → createMediaStreamSource`,从不使用 `createMediaElementSource`**（见 [ui/src/api/webAudioEq.ts](../../ui/src/api/webAudioEq.ts) L4-7、L107-109）：

```
captureStream() → MediaStreamAudioSourceNode → AudioWorkletNode → GainNode → destination
```

在开发环境（Vite HMR）与生产环境中，以下问题必须解决：

1. **HMR 安全**：Vite 热更新时 Vue 组件会重建，若 `AudioContext` 不释放，每次 HMR 都会泄漏一个 context，最终耗尽浏览器音频资源；
2. **图构建顺序**：filter→gain→destination 链必须在 `captureStream` 接入**之前**构建完成，否则若构建过程中抛异常，`<audio>` 元素的 `captureStream` 已启动但无处连接，状态不一致；
3. **跨域媒体**：KuGou CDN 不发 CORS 头，AudioWorklet 无法直接 attach 跨域媒体；
4. **降级路径**：`AudioContext.resume()` 失败、worklet 加载失败或代理不可用时，需有明确降级，且不能静默吞掉。

## 决策

**`webAudioEq` 控制器拥有 AudioContext 生命周期，在 teardown/HMR 时通过 `close()` 释放旧 context；EQ 图按安全顺序构建；HMR 保留 `<audio>` 元素但关闭旧 AudioContext,由新模块重建 EQ graph。**

### 生命周期管理（代码核验）

`webAudioEq.ts` 的 `close()` 方法释放 AudioContext 并清理所有节点引用（[webAudioEq.ts](../../ui/src/api/webAudioEq.ts) L168-186）：

```typescript
// ui/src/api/webAudioEq.ts
close(): void {
  this.clearDegradationTimer();
  this.disconnectSource();           // stop tracks + null sourceNode
  this.workletNode?.disconnect();
  this.gainNode?.disconnect();
  if (this.blobUrl) {
    URL.revokeObjectURL(this.blobUrl);
    this.blobUrl = null;
  }
  if (this.ctx) {
    this.ctx.close().catch(() => {}); // 释放 AudioContext
    this.ctx = null;
  }
  this.workletNode = null;
  this.gainNode = null;
  this.initStarted = false;
  this.workletFailed = false;
  this.readyPromise = null;
}
```

### HMR 生命周期（代码核验）

HMR 触发时旧模块调用 `cleanupCurrentModuleForHmr()`（[playerStore.ts](../../ui/src/api/playerStore.ts) L132-146），其中调用 `closeWebAudioEq()` → `WebAudioEq.close()`，**关闭旧 AudioContext + 拆除 worklet 图**。`<audio>` 元素通过 `window.__bottlemusic_audio__` 跨模块复用，**不 dispose、不清 src**（L205-230）。

新模块加载后，`initPlayer()` 复用同一个 `<audio>` 元素（L214-218），`initPlayerBackend()` 调用 `initWebAudioEQ()` 重建 AudioContext + worklet graph（L317）。

**HMR 三步语义**：

1. **保留**：`<audio>` 元素（避免播放中断与 `00:00` 回退）；
2. **关闭**：旧 `AudioContext`（避免资源泄漏）；
3. **重建**：新模块的 `initWebAudioEQ()` 创建新 `AudioContext` + worklet + gain 链，再由 `attachSource()` 重新 `captureStream`。

> 注：EQ graph **确实在 HMR 时重建**（旧 context 关闭、新 context 创建）。早期文档中"graph 不重建"的描述不准确。`<audio>` 才是不重建的对象。

### EQ 图安全构建顺序

`buildGraph()` 先创建 AudioContext、加载 worklet、连接 `workletNode → gainNode → destination`，全部成功后才在 `attachSource()` 中调用 `captureStream()` + `createMediaStreamSource()` 接入元素（[webAudioEq.ts](../../ui/src/api/webAudioEq.ts) L237-264、L101-112）：

```typescript
// buildGraph: 先建链,再 postBands/postEnabled
this.workletNode = new WorkletNodeCtor(ctx, 'eq-processor');
this.gainNode = ctx.createGain();
this.workletNode.connect(this.gainNode);
this.gainNode.connect(ctx.destination);

// attachSource: 链就绪后才 captureStream
this.currentStream = capturable.captureStream();
this.sourceNode = this.ctx.createMediaStreamSource(this.currentStream);
this.sourceNode.connect(this.workletNode);
```

保证：

- 若 `buildGraph` 抛异常（worklet 加载失败、`createGain` 失败等），`<audio>` 从未进入图，元素仍可正常播放（退回原生路径）；
- 元素接入时图已就绪，立即开始处理。

> 旧文档称"在 `createMediaElementSource` 之前构建" —— 不正确，**实现从不调用 `createMediaElementSource`**；接入元素用的是 `captureStream` + `createMediaStreamSource`。

### 跨域媒体处理

本地 Tauri HTTP 代理（`audio_proxy.rs`，loopback 127.0.0.1）重新分发 CDN 媒体并附加 CORS 头 + range/resume 支持，使 EQ 图能 attach 跨域媒体。

- 代理可用时：`eqState.available = true`，EQ 正常工作；
- 代理不可用时：显示降级提示，EQ 自动禁用（不阻塞播放）。

证据：[ui/src-tauri/src/audio_proxy.rs](../../ui/src-tauri/src/audio_proxy.rs)；详见 [../wiki/playback-runtime.md](../wiki/playback-runtime.md)。

### 降级回调（`onDegraded` / `onRecovered`）

`EqOptions` 暴露两个回调（[webAudioEq.ts](../../ui/src/api/webAudioEq.ts) L15-24）：

- **`onDegraded`**：当 `AudioContext` 创建失败、worklet 加载失败、或 `enterDegradation()` 完成 fade-out 时触发（L240-243、L261-263、L204）；
- **`onRecovered`**：当 `recoverFromDegradation()` 完成 fade-in 时触发（L214）。

> 旧文档称回调为 `onSuspendedFail` —— 不正确，**实际符号是 `onDegraded`**。`onDegraded` 覆盖范围比"suspended fail"更广（包括 worklet 加载失败、context 创建失败等所有降级场景）。

### 分析链路（`audioLevelMonitor.ts`,第二条音频 Context）

除 EQ 链路外,生产代码中存在**第二条独立的 Web Audio 链路**用于音频电平分析,服务于 Aurora 首页粒子动画与可视化页面。

**代码核验**（[ui/src/api/audioLevelMonitor.ts](../../ui/src/api/audioLevelMonitor.ts)）:

- **独立 AudioContext**:`ensureGraph()` 中 `sharedCtx = new AudioContext()`（L65）,与 EQ 的 `WebAudioEq.ctx` 完全独立;
- **拓扑**:`captureStream() → MediaStreamAudioSourceNode → AnalyserNode`,**不连接到 `destination`**（L77 注释:"analysis only — never to destination"）,因此不干预播放路径;
- **永不关闭**:`stop()` 仅停止 rAF 采样循环,`sharedCtx` / `sharedAnalyser` / `sharedSource` 作为模块级单例保留（L133-134 注释:"closing them would blip the output device on every page navigation"）;
- **消费者**:`AuroraHome.vue`（L48 `createAudioLevelMonitor`）、`VisualizerView.vue`（L149 同）;
- **降级**:无 `captureStream` / 无 WebAudio（jsdom、旧 WebView2）→ inert monitor,`level` 恒为 0。

**与 EQ 链路的关系**:两条链路各自 `captureStream` 同一个 `<audio>` 元素,互不干扰。EQ 链路可 HMR 重建（`close()` + 新建）,分析链路设计为永不关闭。这是**有意的设计分歧**,不是疏漏。

> 旧版本 ADR-0003 约束"不得在 `webAudioEq` 之外创建 `AudioContext`"与生产代码冲突 —— 已修正为允许两条已记录的链路（见 §遵守方式）。

## 后果

### 正面

- **EQ 链路 HMR 安全**：EQ 链路的旧 AudioContext 被 `close()` 释放,无累积泄漏；`<audio>` 复用避免播放中断；
- **异常安全**：EQ 图构建失败不会破坏 `<audio>` 元素的可播放性（元素从未进入图）；
- **降级明确**：跨域/worklet 失败场景由 `onDegraded` 上抛,不会静默失败；
- **生产稳定**：生产环境无 HMR,`close()` 在 `pagehide` 时释放资源。

### 负面

- **HMR 重建成本**：每次 HMR 重建 AudioContext + Worklet 有几十毫秒开销；
- **分析链路 HMR 风险（开发态残余）**：`audioLevelMonitor` 的 AudioContext 永不关闭（L133-134），编辑该模块触发 HMR 时缺少显式 dispose，旧模块的模块级单例 `sharedCtx` 在新模块加载后成为孤儿引用。仅影响开发态，生产无 HMR；列为已知风险，未来若需要可补 `dispose()` 钩子；
- **两条 AudioContext**：生产环境存在 EQ（`webAudioEq`）和分析（`audioLevelMonitor`）两条独立 AudioContext,各有一次创建开销；新增第三条需 ADR 评审；
- **代理依赖**：跨域媒体 EQ 依赖 `audio_proxy`,代理故障时 EQ 降级；
- **`<audio>` 全局引用**：`window.__bottlemusic_audio__` 是隐性全局状态,测试需显式清理。

## 备选方案

| 方案 | 否决理由 |
|---|---|
| 持久化 AudioContext 跨 HMR | Vite HMR 重建组件时旧 context 引用混乱；且 Worklet processor 模块可能已更新,需重建 |
| `createMediaElementSource` 接入 | 一旦接入元素即被"吞"进图,断开后无法恢复（已知 bug 模式）；且对跨域媒体会静音 |
| 先 `captureStream` 后建图 | 元素 stream 已启动但无处连接,状态不一致 |
| 不用 AudioWorklet,用 BiquadFilterNode | 灵活性不足,无法实现自定义 RBJ peaking DSP |
| 不用代理,直接对跨域媒体禁用 EQ | 降级体验差,CDN 媒体是主要场景 |
| 静默吞掉 `resume()` 失败 | 用户无法感知 EQ 未生效,与可观测性原则冲突 |

## 遵守方式

- **允许的两条 AudioContext 链路**：① EQ 链路（`webAudioEq.ts`,可 HMR 重建）；② 分析链路（`audioLevelMonitor.ts`,模块级单例,永不关闭）。**新增第三条 AudioContext 需先写 ADR**；
- **不得**使用 `createMediaElementSource` 接入 `<audio>` —— 只能用 `captureStream` + `createMediaStreamSource`；
- **不得**在 `buildGraph` 完成前调用 `attachSource`；
- **不得**静默吞掉降级 —— EQ 链路必须通过 `onDegraded` 上抛；分析链路降级为 inert（`level = 0`）,无需用户提示;
- HMR 时**必须**复用 `window.__bottlemusic_audio__`,**不得** dispose `<audio>` 或清空 `src`；
- 修改 EQ 拓扑时必须同步更新 [../wiki/playback-runtime.md](../wiki/playback-runtime.md) 中的拓扑图；
- PR 触碰 audio 生命周期时需在描述中声明,并附 HMR 手动验证记录。
