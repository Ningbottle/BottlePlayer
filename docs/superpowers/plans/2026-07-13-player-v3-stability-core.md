# BottleMusic V3 Stability Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox state and must be checked only after the stated verification passes.

**Goal:** 消除快速切歌、快速切换歌词和首页分区加载中的异步竞态，使旧请求永远不能覆盖当前界面或音频状态。

**Architecture:** 播放链路由 orchestrator epoch 保护 store 副作用，并由 HTML5 backend 私有 SourceLease 保护 audio 元素；歌词使用按 FileHash 递增的 generation；首页把一个全局 inFlight 拆成三个分区会话。所有变化通过现有 store/composable 接口暴露，不改变 Rust/Tauri 协议。

**Tech Stack:** Vue 3.5、TypeScript、Vitest 4、现有 HTML5 Audio/Tauri loopback proxy。

**Design authority:** docs/superpowers/specs/2026-07-13-dual-interface-deep-refactor-design.md

**Command working directory:** 除显式 git -C 命令外，所有 pnpm、测试、类型检查和构建命令均在实施工作树的 ui/ 目录运行；git add/commit 在仓库根目录运行。

**Strict RED rule:** “模块不存在”、依赖未安装、TypeScript 无法收集测试不算有效 RED。新增模块时先放入最小可编译接口骨架，再运行测试，必须看到具体行为断言失败后才能写 GREEN 实现。

---

## Task 0: 核对实施基线与未提交候选改动

**Recommended agent:** Terra，高思考；只读比较，不直接应用 patch。

**Implementation worktree:** C:/Users/w1521/.codex/worktrees/a6d0/BottleMusic
**Candidate source tree:** C:/BottleMusic

- [ ] **Step 1: 确认分支起点**

~~~powershell
git status --short
git log -1 --oneline
~~~

Expected: 实施分支 codex/dual-interface-deep-refactor，起点 8d4d5dcd，工作树在开始前干净。

- [ ] **Step 2: 只读审查根工作树的 8 个文本改动**

~~~powershell
git -C C:/BottleMusic diff -- ui/src/api/motion.ts ui/src/components/QueuePanel.vue ui/src/views/home/AuroraHome.vue ui/src/views/home/__tests__/AuroraHome.test.ts ui/src/views/lyric/AuroraLyricStage.vue ui/src/views/lyric/AuroraPlaylistShelf.vue ui/src/views/lyric/NewsprintLyricStage.vue ui/src/views/lyric/__tests__/LyricStages.test.ts
~~~

按“已被本计划覆盖 / 值得保留但需先补 RED / 与新规格冲突”分类。不得直接 git apply，不得修改 C:/BottleMusic 根工作树。

- [ ] **Step 3: 明确保留二进制与 QA 资产边界**

不要把 C:/BottleMusic/ui/design-qa-captures/layout-qa 的未跟踪 PNG 或 mcps/ 带入实施分支。需要的候选行为在对应后续任务中通过 RED 测试重新实现。

- [ ] **Step 4: 在任务日志记录结论**

本 Task 不产生代码提交；主代理批准分类后再进入 Task 1。

## Task 1: 建立稳定性 RED 基线

**Recommended agent:** Luna，高思考；只负责测试夹具和失败证据。

**Files:**
- Modify: ui/src/api/__tests__/playbackOrchestrator.test.ts
- Modify: ui/src/api/__tests__/playerBackend.test.ts
- Modify: ui/src/views/lyric/__tests__/LyricStages.test.ts
- Modify: ui/src/api/__tests__/homeFeedStore.test.ts

- [ ] **Step 1: 写快速 A→B 播放竞态测试**

使用可控 deferred：A 的 source preparation 未完成，B 完成并开始播放，最后再 resolve A。断言 audio.src、currentTrack、currentIndex、isLoading、播放历史都保持 B。

~~~ts
it('ignores a prepared source from a superseded track intent', async () => {
  const a = deferred<PreparedAudioSource>();
  const playA = orchestrator.switchTrack(trackA);
  const playB = orchestrator.switchTrack(trackB);

  await playB;
  a.resolve({ url: 'http://127.0.0.1/a', crossOriginSafe: true });
  await playA;

  expect(audio.src).toContain('/b');
  expect(state.currentTrack?.FileHash).toBe(trackB.FileHash);
  expect(uploadPlayHistory).not.toHaveBeenCalledWith(trackA);
});
~~~

- [ ] **Step 2: 写歌词 A→B 乱序返回测试**

断言 A 晚返回后歌词仍属于 B，A 的 finally 不会关闭 B 的 loading，旧 follow timer 不会滚动当前歌词。

- [ ] **Step 3: 写首页分区隔离测试**

断言 daily pending 时 playlists/albums 可独立完成；playlists 失败不阻止 albums；retrySection('playlists') 只请求该分区；已有 items 刷新时不清空。

另外覆盖“HTTP/Promise 成功但业务失败”的响应：daily 主接口 status!=1 后后备接口也无有效列表、playlists/albums status!=1 或缺少 list。首次业务失败必须 error!=null、loaded=false，下一次 ensureLoaded 会重试；已有缓存时保留 items。

- [ ] **Step 4: 运行 RED**

~~~powershell
pnpm test -- src/api/__tests__/playbackOrchestrator.test.ts src/api/__tests__/playerBackend.test.ts src/views/lyric/__tests__/LyricStages.test.ts src/api/__tests__/homeFeedStore.test.ts
~~~

Expected: 新增竞态/分区测试失败，现有无关测试通过。记录失败测试名和根因；禁止修改断言绕过失败。

- [ ] **Step 5: 提交 RED 测试**

~~~powershell
git add ui/src/api/__tests__/playbackOrchestrator.test.ts ui/src/api/__tests__/playerBackend.test.ts ui/src/views/lyric/__tests__/LyricStages.test.ts ui/src/api/__tests__/homeFeedStore.test.ts
git commit -m "test(ui): reproduce async playback and feed races"
~~~

## Task 2: 播放 epoch 与 HTML5 source lease

**Recommended agent:** Terra，高思考；播放链路是高风险共享模块。

**Files:**
- Modify: ui/src/api/html5Backend.ts
- Modify: ui/src/api/playbackOrchestrator.ts
- Modify: ui/src/api/playerStore.ts
- Modify: ui/src/api/__tests__/playbackOrchestrator.test.ts
- Modify: ui/src/api/__tests__/playerBackend.test.ts

- [ ] **Step 1: 在 Html5AudioBackend 内建立私有 SourceLease**

~~~ts
interface SourceLease {
  readonly id: number;
}

private beginSourceLease(): SourceLease {
  return { id: ++this.sourceLeaseId };
}

private ownsSourceLease(lease: SourceLease): boolean {
  return lease.id === this.sourceLeaseId;
}
~~~

每次 playUrl、switchUrl、stop 都失效旧 lease。不得修改 PlayerBackend 公共接口，也不得让 backend 依赖 Vue/store。

- [ ] **Step 2: 在 HTML5 backend 的每个异步边界后检查 lease**

检查位置至少包括：prepareSourceUrl 返回后、写 crossOrigin/src 前、metadata/seek 后、audio.play() 返回后、post-play attach 前。失效时返回 false，且不得 pause 或清空新意图的 audio。

~~~ts
const lease = this.beginSourceLease();
const prepared = await this.prepare(url);
if (!this.ownsSourceLease(lease)) return false;
this.audio.src = prepared.url;
~~~

- [ ] **Step 3: 让 orchestrator 创建和失效 epoch**

保留并收紧现有 transition epoch：每次 switchTrack、取消 pending、清空当前播放和 quality reload 都使旧外层副作用失效。旧调用返回 superseded，不设置用户可见错误，不上传历史。

~~~ts
export type PlaybackResult =
  | { status: 'played' }
  | { status: 'superseded' }
  | { status: 'failed'; message: string };
~~~

- [ ] **Step 4: 保持 EQ 与诊断兼容**

复用同一 epoch 作为 getAttachTransitionSeq/isAttachTransitionCurrent 的来源；只记录 superseded 诊断，不将其作为 media error。

- [ ] **Step 5: 运行 GREEN**

~~~powershell
pnpm test -- src/api/__tests__/playbackOrchestrator.test.ts src/api/__tests__/playerBackend.test.ts src/api/__tests__/playerStore.test.ts src/api/__tests__/audioProxy.test.ts
~~~

Expected: 全部通过；A 的延迟 URL 无法写入 audio；既有 EQ、seek、single-loop、cancel tests 不回退。

- [ ] **Step 6: 重构并提交**

删除重复 epoch helper，只保留 orchestrator 为意图所有者。

~~~powershell
git add ui/src/api/html5Backend.ts ui/src/api/playbackOrchestrator.ts ui/src/api/playerStore.ts ui/src/api/__tests__/playbackOrchestrator.test.ts ui/src/api/__tests__/playerBackend.test.ts
git commit -m "fix(ui): guard playback side effects by current intent"
~~~

## Task 3: 歌词 request generation 与定时器清理

**Recommended agent:** Terra，高思考。

**Files:**
- Create: ui/src/api/lyricsResource.ts
- Create: ui/src/api/__tests__/lyricsResource.test.ts
- Modify: ui/src/views/lyric/useLyricStage.ts
- Modify: ui/src/views/lyric/__tests__/LyricStages.test.ts
- Modify: ui/src/api/__tests__/useLyricFollow.test.ts

- [ ] **Step 1: 添加最小可编译 LyricsResource 骨架并写 RED**

resource 骨架只公开 state、load(track)、retry()、dispose() 并返回空状态。测试 A/B 两段请求乱序、失败重试和 dispose 后不提交；不依赖 Vue 组件。

~~~powershell
pnpm test -- src/api/__tests__/lyricsResource.test.ts
~~~

Expected: 测试可收集，并因 A 的结果覆盖 B、error/retry/dispose 行为缺失而失败。

- [ ] **Step 2: 最小实现 resource generation**

实现当前 generation 提交门和 error/retry/dispose；只运行 lyricsResource.test.ts 直到 GREEN，再进入组件适配。

- [ ] **Step 3: 用单一 immediate watch 驱动 resource**

移除 watch(currentTrack) 与 onMounted(loadLyrics) 的双入口。

~~~ts
watch(
  () => currentTrack.value?.FileHash ?? null,
  () => void lyricsResource.load(currentTrack.value),
  { immediate: true },
);
~~~

- [ ] **Step 4: generation 提交门只存在于 resource**

~~~ts
let lyricGeneration = 0;

async function load(track: Track | null) {
  const generation = ++lyricGeneration;
  if (!track) return resetLyrics(generation);
  state.loading = true;
  try {
    const nextLines = await fetchLyrics(track.FileHash);
    if (generation !== lyricGeneration) return;
    state.lines = nextLines;
  } finally {
    if (generation === lyricGeneration) state.loading = false;
  }
}
~~~

请求失败写入 error 并提供 retry，不再把“歌词加载出错”塞进普通歌词行。useLyricStage 的 model 暴露 error，commands 暴露 retryLyrics()，供两套舞台渲染真实错误态。

- [ ] **Step 5: 让 useLyricStage 成为适配器并清理 follow timeout**

切歌、手动 seek、重新跟随和卸载前都清理旧 timeout；timeout 回调也验证 generation 和组件 mounted 状态。

- [ ] **Step 6: 运行整组 GREEN**

~~~powershell
pnpm test -- src/api/__tests__/lyricsResource.test.ts src/views/lyric/__tests__/LyricStages.test.ts src/api/__tests__/useLyricFollow.test.ts src/api/__tests__/lyricFocusStore.test.ts
~~~

Expected: 乱序歌词、loading、seek、恢复跟随和卸载清理全部通过。

- [ ] **Step 7: 提交**

~~~powershell
git add ui/src/api/lyricsResource.ts ui/src/api/__tests__/lyricsResource.test.ts ui/src/views/lyric/useLyricStage.ts ui/src/views/lyric/__tests__/LyricStages.test.ts ui/src/api/__tests__/useLyricFollow.test.ts
git commit -m "fix(ui): isolate lyric requests by track generation"
~~~

## Task 4: 首页分区请求会话

**Recommended agent:** Terra，高思考。

**Files:**
- Modify: ui/src/api/homeFeedStore.ts
- Modify: ui/src/api/__tests__/homeFeedStore.test.ts
- Modify: ui/src/views/home/homeViewModel.ts
- Modify: ui/src/views/home/__tests__/homeViewModel.contract.test.ts

- [ ] **Step 1: 为每个分区建立运行态**

~~~ts
export type HomeSection = 'daily' | 'playlists' | 'albums';

interface SectionRequestSession {
  generation: number;
  promise: Promise<void> | null;
}
~~~

ensureLoaded() 并行启动所有未加载区；refresh() 并行刷新三区；retrySection(section) 只刷新目标区。

- [ ] **Step 2: 只允许当前 generation 提交**

旧请求不得写 items、error、loading 或 refreshing。已有 items 时刷新失败必须保留旧 items。只有明确的业务成功响应才能令 loaded=true；status!=1、缺少预期 payload、daily 主/后备均无有效结果都进入 error。业务明确返回成功空列表可以 loaded=true；首次失败保持 false，使下一次 ensureLoaded 能重试。

- [ ] **Step 3: 保留 layout demo**

不得删除或改名 isLayoutDemo()/seedLayoutDemo()；三种入口都在 demo 模式下同步返回稳定数据。

- [ ] **Step 4: 扩展 view model 合同**

每个区暴露 loading、refreshing、error、isEmpty 和 retry()，页面不再推导全局 loading。

- [ ] **Step 5: 运行 GREEN**

~~~powershell
pnpm test -- src/api/__tests__/homeFeedStore.test.ts src/views/home/__tests__/homeViewModel.contract.test.ts src/views/home/__tests__/HomeView.enterMode.test.ts
~~~

Expected: slow daily 不阻止其他区，单区 retry 正确，缓存刷新不闪空，layout demo 测试通过。

- [ ] **Step 6: 提交**

~~~powershell
git add ui/src/api/homeFeedStore.ts ui/src/api/__tests__/homeFeedStore.test.ts ui/src/views/home/homeViewModel.ts ui/src/views/home/__tests__/homeViewModel.contract.test.ts
git commit -m "fix(ui): isolate home feed section requests"
~~~

## Task 5: 稳定性阶段回归

**Recommended agent:** Luna，高思考；只运行验证并整理失败，不修改产品逻辑。

- [ ] **Step 1: 运行相关测试集合**

~~~powershell
pnpm test -- src/api/__tests__ src/views/home/__tests__ src/views/lyric/__tests__
~~~

- [ ] **Step 2: 运行类型检查**

~~~powershell
pnpm exec vue-tsc --noEmit
~~~

- [ ] **Step 3: 检查保护项**

~~~powershell
git diff -- ui/src/api/nativeBackend.ts ui/src-tauri native
rg -n "layoutDemo|seedLayoutDemo" ui/src/api/homeFeedStore.ts ui/src/api/__tests__/homeFeedStore.test.ts
~~~

Expected: 保护项无意外 diff，layout demo 仍有实现与测试。

- [ ] **Step 4: 主代理审阅**

主代理逐项检查 RED 证据、GREEN 结果、epoch 所有权、无 stale finally、无全局首页锁。发现问题时退回原任务代理修正，不在验证任务中顺手重写。
