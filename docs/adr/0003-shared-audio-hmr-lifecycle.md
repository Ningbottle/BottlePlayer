# ADR-0003：共享 audio/HMR 生命周期与 EQ 图安全构建

- **状态**：Accepted
- **日期**：2026-07-23
- **决策者**：架构审计（code evidence）
- **关联文档**：[../wiki/playback-runtime.md](../wiki/playback-runtime.md)、[../wiki/frontend.md](../wiki/frontend.md)、[../wiki/evidence-report.md](../wiki/evidence-report.md)

## 上下文

BottleMusic 的均衡器（EQ）基于 Web Audio API AudioWorklet 实现，需要将 HTML5 `<audio>` 元素接入 AudioContext 图：

```
captureStream → MediaStreamAudioSourceNode → AudioWorkletNode → GainNode → destination
```

在开发环境（Vite HMR）与生产环境中，以下问题必须解决：

1. **HMR 安全**：Vite 热更新时 Vue 组件会重建，若 `AudioContext` 不释放，每次 HMR 都会泄漏一个 context，最终耗尽浏览器音频资源；
2. **图构建顺序**：若 `createMediaElementSource` 先于 EQ 图节点创建，`<audio>` 元素会被困在断开的图中，无法恢复；
3. **跨域媒体**：KuGou CDN 不发 CORS 头，AudioWorklet 无法直接 attach 跨域媒体；
4. **挂起恢复**：`AudioContext.resume()` 可能失败，需有降级路径。

## 决策

**`webAudioEq` 控制器拥有 AudioContext 生命周期，在 teardown 时通过 `close()` 释放，HMR 安全；EQ 图按安全顺序构建。**

### 生命周期管理（代码核验）

`webAudioEq.ts` 定义了 `close()` 方法，在组件卸载时调用：

```typescript
// ui/src/api/webAudioEq.ts
close(): void {
  // ... 清理节点引用
  this.ctx.close().catch(() => {});
}
```

- `close()` 释放 AudioContext，避免 HMR 累积泄漏；
- HMR 重建时新建 `AudioContext`，旧 context 已关闭；
- 该约束记录于 [CONTEXT.md](../../CONTEXT.md) S4 Details「AudioContext lifecycle (#9)」。

### EQ 图安全构建顺序

**完整 filter→gain→destination 链在 `createMediaElementSource` 之前构建**，保证：

- 若构建过程中抛异常，`<audio>` 元素从未进入图，不会被孤立；
- 元素接入时图已就绪，立即开始处理。

证据：[CONTEXT.md](../../CONTEXT.md) S4 Details「EQ graph build order (#4)」；实现见 [ui/src/api/webAudioEq.ts](../../ui/src/api/webAudioEq.ts)。

### 跨域媒体处理

本地 Tauri HTTP 代理（`audio_proxy.rs`，loopback 127.0.0.1）重新分发 CDN 媒体并附加 CORS 头 + range/resume 支持，使 EQ 图能 attach 跨域媒体。

- 代理可用时：`eqState.available = true`，EQ 正常工作；
- 代理不可用时：显示降级提示，EQ 自动禁用（不阻塞播放）。

证据：[ui/src-tauri/src/audio_proxy.rs](../../ui/src-tauri/src/audio_proxy.rs)；详见 [../wiki/playback-runtime.md](../wiki/playback-runtime.md)。

### 挂起恢复降级

`AudioContext.resume()` 失败时，通过 `onSuspendedFail` 回调上抛，而非静默吞掉。前端据此显示提示。

## 后果

### 正面

- **HMR 安全**：开发时无 AudioContext 泄漏，长时间开发不会耗尽资源；
- **异常安全**：EQ 图构建失败不会破坏 `<audio>` 元素的可播放性；
- **降级明确**：跨域/挂起场景有明确提示，不会静默失败；
- **生产稳定**：生产环境无 HMR，`close()` 在页面卸载时释放资源。

### 负面

- **HMR 重建成本**：每次 HMR 重建 AudioContext + Worklet 有几十毫秒开销；
- **单 AudioContext**：全局只允许一个 `webAudioEq` 实例，多实例会冲突（当前架构未有多实例需求）；
- **代理依赖**：跨域媒体 EQ 依赖 `audio_proxy`，代理故障时 EQ 降级。

## 备选方案

| 方案 | 否决理由 |
|---|---|
| 持久化 AudioContext 跨 HMR | Vite HMR 重建组件时旧 context 引用混乱；且 Worklet processor 模块可能已更新，需重建 |
| MediaElementSource 先建后补图 | 元素被困断开图，无法恢复（已知 bug 模式） |
| 不用 AudioWorklet，用 BiquadFilterNode | 灵活性不足，无法实现自定义 RBJ peaking DSP |
| 不用代理，直接对跨域媒体禁用 EQ | 降级体验差，CDN 媒体是主要场景 |

## 遵守方式

- **不得**在 `webAudioEq` 之外创建 `AudioContext`；
- **不得**在 `createMediaElementSource` 之前不构建 EQ 图；
- **不得**静默吞掉 `resume()` 失败；
- 修改 EQ 拓扑时必须同步更新 [../wiki/playback-runtime.md](../wiki/playback-runtime.md) 中的拓扑图；
- PR 触碰 audio 生命周期时需在描述中声明，并附 HMR 手动验证记录。
