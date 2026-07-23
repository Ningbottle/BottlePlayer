# 播放运行时稳定性重构报告

> Branch:`codex/runtime-stability-refactor`
> Baseline commit:`1f2069d3`
> Final HEAD:见 §6 提交链
> 重构日期:2026-07-23
> 关联文档:[runtime-stability-audit.md](../../.superpowers/sdd/runtime-stability-audit.md)、[ADR-0003](../adr/0003-shared-audio-hmr-lifecycle.md)、[playback-runtime.md](./playback-runtime.md)

## 1. 目标与范围

### 1.1 目标(对应原任务 10 项要求)

| # | 目标 | 状态 |
|---|------|:---:|
| 1 | Phase 是唯一真相源,布尔标志是投影 | ✅ |
| 2 | 所有播放命令经单一命令协调入口 | ✅(基线已具备,本轮锁定) |
| 3 | 过期异步结果不得覆盖新意图 | ✅(基线已具备,本轮锁定) |
| 4 | HMR 不得清 src / 暂停共享音频 / 写空队列 | ✅ |
| 5 | pagehide 先持久化队列,再停运行时 | ✅(基线已具备,本轮锁定) |
| 6 | currentTrack/currentIndex/queue/phase 始终一致 | ✅ |
| 7 | Personal FM 预取/去重/退避重试/追加/手选语义不变 | ✅(基线已具备,本轮锁定) |
| 8 | EQ 与 audioLevelMonitor 的 AudioContext 所有权、HMR、pagehide 生命周期明确且可测试 | ✅ |
| 9 | 失败 URL 解析 / 延迟播放 / 快速 next/prev / clear 障碍不留下悬挂 Promise | ✅(基线已具备,本轮锁定) |
| 10 | 收藏 / 推荐 / 统计 / 两套界面行为不回归 | ✅ |

### 1.2 冻结范围(未触碰)

- Aurora / Newsprint Vue 模板、CSS、动画、布局
- `favoriteStore` / `favoriteRepository` 域逻辑
- Rust `audio_proxy`、C++ Storage Actor、FFI exports、数据库 schema
- Pinia 迁移、依赖升级、路由重写、产品文案
- "无测试证据的顺手清理"(R5 deprecated `playbackQueue.ts` 保留)

## 2. 风险清单与修复状态

源自 [runtime-stability-audit.md §7](../../.superpowers/sdd/runtime-stability-audit.md) 的 9 项风险:

| ID | 优先级 | 风险 | 修复方式 | 状态 |
|----|:---:|------|----------|:---:|
| R1 | P1 | `patchPlayerState` 写入顺序导致 stale flag 可能残留 | patch 含 phase 时剥离 `isPlaying`/`isLoading`,强制经 `flagsFromPhase` 派生 | ✅ Phase 3 |
| R2 | P1 | `initPlayer` HMR 复用 `<audio>` 时直接写 `isPlaying` 而不写 phase | 改用 `applyStorePhase('playing'|'paused')`,audio 中播→playing,有 track 暂停→paused,无 track→idle | ✅ Phase 3 |
| R3 | P1 | `audioLevelMonitor` 无 dispose,HMR 累积孤儿 AudioContext | 新增 `disposeAudioLevelMonitor()`,仅由 `cleanupCurrentModuleForHmr()` 调用;生产 pagehide 不调用(保持 no-blip) | ✅ Phase 4 |
| R4 | P2 | `playerPersistence.ts` 模块顶层 `beforeunload` 监听与 `pagehide` 重复 | 删除 `beforeunload` 监听,`pagehide` 是唯一 flush owner | ✅ Phase 5 |
| R5 | P2 | `playbackQueue.ts` deprecated 模块仍存在 | 保留(frozen scope),仅在 audit 中标记为 test-only | 📌 不修(冻结) |
| R6 | P2 | `setQuality` 在 coordinator 外有第二次写 | 低优先级,本轮不修(quality 非 phase 字段,只在成功后写) | 📌 不修(低优先级) |
| R7 | P2 | FM `retryExhausted` 无时间冷却 | characterization tests 验证:当前行为(显式 append / 新 session 重置)满足 Goal #5 "不能永久 exhausted" | ✅ Phase 5(已验证) |
| R8 | P3 | `coordinator.togglePlay` isLoading 分支直接 patch phase | 低优先级,本轮不修(idle→paused 合法) | 📌 不修(低优先级) |
| R9 | P3 | 无显式 AudioContext 所有权测试 | 新增 `audioLifecycleOwnership.test.ts` + `playbackRuntimeCharacterization.test.ts` | ✅ Phase 2/4 |

**结论:** 全部 P1 风险(R1/R2/R3)已修复;P2 中 R4 已修、R7 已验证;R5/R6/R8 经评估为低风险或属冻结范围,本轮不修并在 audit 中记录理由。

## 3. 修复详情

### 3.1 R1:`patchPlayerState` 强制 phase 派生

**文件:** [ui/src/api/playerStore.ts](../../ui/src/api/playerStore.ts) `patchPlayerState()`

**变更前:**
```typescript
function patchPlayerState(patch: Partial<typeof playerStore>) {
  Object.assign(playerStore, patch);           // ← 先写 patch 中的 flags
  if (patch.playbackPhase != null) {
    Object.assign(playerStore, flagsFromPhase(patch.playbackPhase));
  }
}
```

**变更后:**
```typescript
function patchPlayerState(patch: Partial<typeof playerStore>) {
  if (patch.playbackPhase != null) {
    const { isPlaying: _dropPlay, isLoading: _dropLoad, ...rest } = patch;
    void _dropPlay; void _dropLoad;
    Object.assign(playerStore, rest, flagsFromPhase(patch.playbackPhase));
  } else {
    Object.assign(playerStore, patch);
  }
}
```

**机制:** 当 patch 含 `playbackPhase` 时,从 patch 中**剥离** `isPlaying` / `isLoading`,只经 `flagsFromPhase(phase)` 派生。无 phase 的 patch(如 `currentTime` / `duration` / `errorMsg`)不触碰 flags。

**测试入口:** `__patchPlayerStateForTests`(测试专用 seam,锁死不变量)。

### 3.2 R2:`initPlayer` HMR 复用经 phase

**文件:** [ui/src/api/playerStore.ts](../../ui/src/api/playerStore.ts) `initPlayer()`

**变更前:**
```typescript
playerStore.audio = audio;
playerStore.isPlaying = !audio.paused && !audio.ended;  // ← R3: 只写 flag,不写 phase
```

**变更后:**
```typescript
playerStore.audio = audio;
const audioIsMidPlay = !audio.paused && !audio.ended;
if (audioIsMidPlay) {
  applyStorePhase('playing');
} else if (playerStore.currentTrack) {
  applyStorePhase('paused');
} else {
  playerStore.isPlaying = false;
  playerStore.isLoading = false;
}
```

**机制:** HMR 复用 `<audio>` 时,audio 元素的 `paused`/`ended` 反映真实播放态。把它投影到 phase(`applyStorePhase` 内部调用 `flagsFromPhase`),phase 与 flags 立即一致,不再有 idle↔isPlaying=true 的不一致窗口。

### 3.3 R3:`disposeAudioLevelMonitor` for HMR

**文件:** [ui/src/api/audioLevelMonitor.ts](../../ui/src/api/audioLevelMonitor.ts) + [ui/src/api/playerStore.ts](../../ui/src/api/playerStore.ts) `cleanupCurrentModuleForHmr()`

**新增导出:**
```typescript
export function disposeAudioLevelMonitor(): void {
  if (sharedSource) {
    try { sharedSource.disconnect(); } catch { /* ignore */ }
    sharedSource = null;
  }
  if (sharedAnalyser) {
    try { sharedAnalyser.disconnect(); } catch { /* ignore */ }
    sharedAnalyser = null;
  }
  if (sharedCtx) {
    try { void sharedCtx.close(); } catch { /* ignore */ }
    sharedCtx = null;
  }
  sharedAudio = null;
  sharedSamples = null;
}
```

**调用点:** 仅 `cleanupCurrentModuleForHmr()`(开发态 HMR)。生产 `disposePlayerRuntime()`(pagehide)**不**调用——分析链路只读、不连 destination,关闭反而会 blip 输出设备。

**修复效果:** HMR 孤儿 AudioContext 累积风险在代码层面消除——旧模块的 `sharedCtx` 经 `disposeAudioLevelMonitor()` 关闭,新模块 `ensureGraph()` 重建 fresh context。Idempotent:二次调用为 no-op。**注:** "HMR 6+ 次后配额耗尽"的场景尚需 Tauri dev 手测验证(见 §8.2),本轮未执行 HMR 手测。

### 3.4 R4:移除 `beforeunload` 监听

**文件:** [ui/src/api/playerPersistence.ts](../../ui/src/api/playerPersistence.ts)

**变更前(模块顶层):**
```typescript
if (typeof window !== 'undefined') {
  window.addEventListener('beforeunload', () => {
    flushSaveQueue();
  });
}
```

**变更后:** 删除上述 4 行。`pagehide` → `disposePlayerRuntime()` → `flushSaveQueue()` 是唯一 flush owner(在 `initPlayer` 中绑定,只有 live 模块拥有)。

**修复效果:** 孤儿 HMR 模块不再注册 `beforeunload` 监听,避免 stale `getSnapshot` 闭包 flush 陈旧数据。`pagehide` 是现代标准,在所有支持平台可靠触发。

### 3.5 R7:FM retry cooldown 验证(无需生产改动)

**文件:** [ui/src/api/__tests__/fmSession.test.ts](../../ui/src/api/__tests__/fmSession.test.ts)

经 characterization tests 验证,当前 FM 行为已满足 Goal #5 "短时空响应不能永久 exhausted":

- 3 次失败后 `retryExhausted = true`,但**显式 `appendPersonalFmRecommendations()` 调用**会重置 `retryExhausted = false` 并发起新一轮 fetch。
- 新 session(queue identity 变化)会重置 `retryExhausted`。
- `disposeFmSession()` 取消 pending retry timer 并阻塞 stale retries(exit/HMR)。

**决策:** 不需要时间冷却。用户主动操作(切歌、手动刷新推荐)即触发重置;在用户无操作期间保持 exhausted 是合理的反压机制,避免对 KuGou 接口持续打流量。

### 3.6 Review P1-1:phase 真正成为唯一真相源(无 phase patch 也拒绝 bare flags)

**文件:** [ui/src/api/playerStore.ts](../../ui/src/api/playerStore.ts) `patchPlayerState()` + [ui/src/api/playbackOrchestrator.ts](../../ui/src/api/playbackOrchestrator.ts) `applyPhase()`

**问题:** 初版 R1 修复只在 patch **含** `playbackPhase` 时剥离 flags。无 phase 的 patch(如 `{ isPlaying: true, currentTime: 5 }`)仍经 `Object.assign` 直接写入 `isPlaying`,调用方可绕过 phase 派生。orchestrator 的 `applyPhase` 同 phase 路径(`from === to`)正依赖此裸写入:`this.deps.patchState(flagsFromPhase(to))` 只传 flags 不传 phase。

**修复:**
1. `patchPlayerState`:从**所有** patch 中剥离 `isPlaying`/`isLoading`(无论是否含 phase)。含 phase → 经 `flagsFromPhase` 派生;无 phase → 丢弃 flags,保留现有值。非 flag 字段(currentTime / duration / errorMsg 等)正常通过。
2. `applyPhase` 同 phase 路径:改为 `this.deps.patchState({ playbackPhase: to, ...flagsFromPhase(to) })`,始终带 phase。两分支合并为单一 `patchState` 调用。

**测试:** 2 个 RED→GREEN 测试 —— 无 phase patch 传 `isPlaying: true` 被拒绝;即使 phase=playing 时传 `isPlaying: false` 也被拒绝(锁死"funnel 从不接受 bare flags")。

### 3.7 Review P1-2:initPlayer currentTrack 恢复顺序

**文件:** [ui/src/api/playerStore.ts](../../ui/src/api/playerStore.ts) `initPlayer()`

**问题:** `initPlayer()` 在 L242 投影 phase(依据 `currentTrack`),但 `currentTrack` 从持久化队列恢复在 L316。冷启动/HMR 中,audio 暂停 + 有效队列时,phase 投影时 `currentTrack` 仍为 null → 落入"无 track"分支 → phase=idle。随后 L318 恢复 currentTrack,但 phase 不再更新 → "currentTrack 非空但 phase=idle"的不一致状态。测试提前手工设置 `currentTrack` 掩盖了此问题。

**修复:** 将 `currentTrack` 恢复块从函数末尾移至 phase 投影之前。恢复后 `playerStore.currentTrack` 非空,paused-with-track 分支正确触发 `applyStorePhase('paused')`。删除函数末尾的重复恢复块。

**测试:** 1 个 RED→GREEN 测试 —— 不预设 `currentTrack`,只 seed `queue` + `currentIndex`(模拟 localStorage 冷启动),验证 initPlayer 先恢复 currentTrack 再投影 phase=paused。

### 3.8 Review P2-3:AudioContext.close() 异步 rejection

**文件:** [ui/src/api/audioLevelMonitor.ts](../../ui/src/api/audioLevelMonitor.ts) `disposeAudioLevelMonitor()`

**问题:** `try { void sharedCtx.close(); } catch { /* ignore */ }` 的同步 try/catch 捕获不到 `close()` 返回的 Promise rejection。HMR 时若 close() 异步失败,会产生 unhandled rejection。

**修复:** 保存 ctx 引用 → null 模块级 slot → `void ctx.close().catch(() => {})` 显式捕获异步 rejection。

## 4. 测试覆盖

### 4.1 新增测试文件

| 文件 | 测试数 | 覆盖范围 |
|------|:---:|---------|
| `ui/src/api/__tests__/playbackRuntimeCharacterization.test.ts` | 4 | audioLevelMonitor 永不关闭、`start() after stop()` 复用同一 ctx、两 monitor 共享 ctx、`beforeunload` 不再 flush |
| `ui/src/api/__tests__/playbackPhaseProjection.test.ts` | 10 | R1: phase 存在时剥离 stale flags(4 个)+ 无 phase patch 拒绝 bare flags(2 个 review-fix);R2: HMR 复用经 phase(3 个)+ 真实初始化顺序(1 个 review-fix) |
| `ui/src/api/__tests__/audioLifecycleOwnership.test.ts` | 3 | R3: dispose 关闭 ctx、dispose 后新建 fresh ctx、dispose idempotent |

**新增测试合计:17 个**(937 → 954)。其中 3 个为 review P1/P2 修复新增的 RED→GREEN 测试。

### 4.2 TDD 流程

每个修复均按 RED → GREEN 流程:
1. **RED**:先写失败测试,描述期望不变量。
2. **GREEN**:实施最小修复让测试通过。
3. **mutation check**:确保移除修复后测试会失败(否则不是真正的 characterization test)。

### 4.3 Stress gate

`playbackStress.gate.test.ts` 在 1000 条混合命令下通过(coordinator-stress,wallClock 6ms,无 stuck queue / phase contradiction):

```
{"mode":"coordinator-stress","commands":1000,"wallClockMs":6,
 "playCount":64,"qualityFails":2,"finalPhase":"idle","queueLen":0}
```

## 5. 全门禁结果

| Gate | 命令 | 基线(1f2069d3) | 终态(HEAD) | 备注 |
|------|------|:---:|:---:|------|
| Vitest | `cd ui && npx vitest run --maxWorkers=2` | 937/937 | **954/954** | +17 新测试(14 初始 + 3 review-fix),81 文件全过 |
| vue-tsc | `cd ui && npx vue-tsc --noEmit` | pass | **pass** | 无类型错误 |
| vite build | `cd ui && npx vite build` | pass | **pass**(25.34s) | 无 warning(仅既存 chunk size 提示) |
| Rust cargo test | `cd ui/src-tauri && cargo test --no-fail-fast` | 34/34 | **34/34** + 2 integration | lib + bin + integration 全过 |
| CTest | `ctest --test-dir native/out/bottlemusic-check --output-on-failure` | 10/11 | **11/11** | EchoNativeSmokeTests 从环境性 fail 恢复为 pass |

**无回归。** 所有基线测试继续通过,新增 17 个测试全部 GREEN。

## 6. 提交链

| # | Commit | 类型 | 范围 |
|---|--------|------|------|
| 1 | `f8090128` | test | Phase 2: characterization tests(audioLevelMonitor never-close、playerPersistence beforeunload lock) |
| 2 | `714a7a79` | refactor | Phase 3: R1+R2 fix —— `patchPlayerState` 剥离 stale flags、`initPlayer` HMR 经 `applyStorePhase` |
| 3 | `f520637d` | refactor | Phase 4: R3 fix —— `disposeAudioLevelMonitor()` 新增,`cleanupCurrentModuleForHmr` 调用 |
| 4 | `ee6b0570` | refactor | Phase 5: R4 fix —— 删除 `playerPersistence.ts` 顶层 `beforeunload` 监听 |
| 5 | `c264560a` | docs | ADR-0003 + playback-runtime.md 更新(analyser HMR dispose、R1/R2/R4 文档化) |
| 6 | `35df4625` | docs | refactor-report.md 初版 |
| 7 | `0f840db9` | fix | Review P1+P2 修复 —— phase-as-sole-source 强制(剥离所有 patch 的 flags)、initPlayer currentTrack 恢复顺序、AudioContext.close() 异步 catch |

**纪律:** 每个 commit 独立可 revert;production code 改动仅 4 个 commit(714a7a79 / f520637d / ee6b0570 / 0f840db9),其余为纯测试或纯文档;frozen scope 全程未触碰。

## 7. 文档同步

| 文档 | 更新内容 |
|------|---------|
| [ADR-0003](../adr/0003-shared-audio-hmr-lifecycle.md) | 分析链路 §新增 `disposeAudioLevelMonitor()` 契约;负面后果中"分析链路 HMR 风险"标记为已修复;遵守方式 §明确"开发 HMR 时经 dispose 释放,生产 pagehide 不调用" |
| [playback-runtime.md](./playback-runtime.md) | §3 新增 R1/R2 强制说明(phase 是唯一真相源,patchPlayerState 剥离 flags);§11 新增 R3 analyser dispose 调用 + R4 beforeunload 移除说明 |
| [runtime-stability-audit.md](../../.superpowers/sdd/runtime-stability-audit.md) | Phase 1 audit 文档,作为本轮重构的设计来源(未在本轮修改) |

## 8. 待用户手测项(非自动化可覆盖)

以下场景需在 Tauri dev 环境手动验证(自动化测试已覆盖逻辑层不变量,但端到端体验需手测):

### 8.1 播放/暂停/seek/切歌/歌词/全屏/设置/两套皮肤/退出重启

- 播放 → 暂停 → 恢复:无双重声音、无 00:00 回退
- seek 跳转:position 立即一致
- 快速 next/prev 连按:最终落在正确曲目,无悬挂 Promise
- 切音质:从原位续播
- 歌词同步、全屏切换、设置面板
- Aurora / Newsprint 两套皮肤切换
- 退出 → 重启:队列恢复,无空队列

### 8.2 HMR 手测(仅 dev)

- 播放中编辑 player 模块文件(如 `playerStore.ts` / `audioLevelMonitor.ts`)
- 验证:音频继续播放、EQ graph 重建、analyser 重建、console 无孤儿 AudioContext 警告
- 连续 HMR 6+ 次:无 AudioContext 配额耗尽

### 8.3 Personal FM 手测

- FM 推荐空响应 → 1s/3s/10s 退避重试
- 3 次失败后 exhausted → 用户手动切歌或新 session → 重置
- 播放中点击推荐曲目:不重建整个 FM 队列

## 9. Soak test 声明

**自动化 soak test 状态:** 未执行(需用户在本地 dev/prod 环境手动运行)。

**建议时长:**
- **2h soak**:dev 环境,连续播放 + 偶尔切歌/seek,验证无内存泄漏、无 AudioContext 累积、无 phase 卡死
- **24h soak**:prod build,长期播放 + pagehide/恢复循环,验证 queue 持久化、统计会话累加、FM 退避恢复

**通过判据:**
- 内存占用稳定(无单调上升)
- AudioContext 数量稳定(EQ 1 + analyser 1 = 2,跨 HMR/pagehide 不累积)
- phase 始终与 audio 实际状态一致
- 队列 / currentIndex / currentTrack 始终一致
- 统计 PlayRecord 数量与实际播放次数一致

**如 soak test 发现问题:** 在 audit 文档中追加新风险项,按 RED → GREEN 流程修复,新增 commit 接在 review-fix commit 之后。

## 10. 自检清单

- [x] 全部 P1 风险(R1/R2/R3)已修复并有测试覆盖
- [x] Review P1-1 修复:phase 真正成为唯一真相源(无 phase patch 也拒绝 bare flags)
- [x] Review P1-2 修复:initPlayer currentTrack 恢复在 phase 投影之前
- [x] Review P2-3 修复:AudioContext.close() 异步 rejection 经 .catch() 捕获
- [x] 全部门禁通过(Vitest 954 / vue-tsc / vite build / Rust 36 / CTest 11)
- [x] 无 frozen scope 触碰(Vue 模板、CSS、Rust/C++/FFI/DB、Pinia、deps、路由)
- [x] 每个 commit 独立可 revert
- [x] 文档同步(ADR-0003 + playback-runtime.md)
- [x] 新增测试经 mutation 验证(移除修复会失败)
- [x] Stress gate 1000 命令通过
- [ ] 2h/24h soak test(待用户手动执行)
- [ ] Tauri dev 手测(待用户手动执行)

## 11. 已知遗留(本轮不修)

| 项 | 原因 | 跟进建议 |
|----|------|---------|
| R5 `playbackQueue.ts` deprecated 模块 | frozen scope 排除"无测试证据的顺手清理" | 后续 PR 统一清理 deprecated 模块 |
| R6 `setQuality` 第二次写 | quality 非 phase 字段,只在成功后写,低风险 | 可考虑改为 watch handler,与 volume/loopMode 一致 |
| R8 `coordinator.togglePlay` isLoading 直接 patch phase | idle→paused 合法,影响小 | 可考虑路由经 `applyPhase`,但需评估 toggle 响应延迟 |
| Coordinator 拆分(1015 行) | 当前结构内聚,拆分引入跨模块状态同步风险 | 如必须拆分,单独 PR + 全 characterization test 覆盖 |

---

**重构完成日期:** 2026-07-23
**最终 commit:** `0f840db9`(含 review P1/P2 修复)
**等待:** 用户手测 + soak test 反馈,或合并指令
