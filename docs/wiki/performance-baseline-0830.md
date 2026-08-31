# Phase D Performance Baseline (08-30, corrected 08-31)

本文档是 `0830-architecture-remediation-plan.md` **Task D1** 的性能基线,为 08-30 版本的**更正版**。原版(见 git 历史 `7e0f8a06`)经证据审计后,FPS 算法与 Forced Layout 归因均被推翻(差异见 §7)。本版数据全部来自在真实 Tauri 桌面应用(WebView2 + C++ Native Backend)中、带有**逐项场景前置断言与 UserTiming 时间标记**的重新录制;产品代码零改动。

- 原始 Trace: `outputs/performance-traces-0831-audit/`(本地保留,不纳入版本库;文件名 `S<id>-run<1..3>-1280x820-dpr1.25.json.gz`)
- 断言与采样记录: `outputs/perf-audit-d1/results/`(每次录制的断言清单、S1 播放采样、采集/分析脚本)
- 08-30 旧 trace 归档保留于 `outputs/performance-traces-0830/`,仅作审计对照

---

## 1. 测试环境与设备信息

| 属性 | 测量值 |
| :--- | :--- |
| **操作系统** | Windows_NT 10.0.26200 (x64) |
| **处理器 (CPU)** | AMD Ryzen 9 7945HX with Radeon Graphics (32 logical cores) |
| **系统内存 (RAM)** | 15.2 GB |
| **窗口视口尺寸** | 1280x820 (Tauri 窗口,`decorations: false`) |
| **devicePixelRatio** | 1.25(录制前断言) |
| **运行容器与构建** | Debug / Dev(Tauri 2 + WebView2 Edg/151.0.4129.107 + EchoCAPI.dll Native C++,前端 Vite dev @ localhost:1420) |
| **基线 Commit** | `795ebf9c (Phase C completed)` |
| **显示器刷新率** | 75 Hz(由帧序列实测导出,见 §2) |

---

## 2. 方法与算法(数据之前,先定义度量)

录制通道:CDP `Tracing`(页面 target,DevTools 标准类别,含 `disabled-by-default-devtools.timeline.frame` 与栈捕获类别)。分析脚本位于 `outputs/perf-audit-d1/`(ignored,不提交)。

| 指标 | 算法 |
| :--- | :--- |
| **场景窗口** | 以页面内 `performance.mark('D1:<场景>:START' / ':END')`(trace 中 `blink.user_timing` 事件)切片,**只统计窗口内事件**;启动、加载与 Tracing 开销一律排除 |
| **FPS / 帧耗时** | 单一帧序列 = 渲染进程合成线程的 `DrawFrame`(`disabled-by-default-devtools.timeline.frame`)相邻间隔。Avg FPS = 1000/mean(Δ),Median FPS = 1000/median(Δ),P95/Worst 为帧耗时分布。**不混合** BeginFrame/BeginImplFrame/BeginMainThreadFrame,不封顶、不假设 60 |
| **Forced Layout** | 仅 `Layout` / `UpdateLayoutTree` 事件中 `args.beginData.stackTrace` **非空**者;以调用栈归因到源码。无栈的 Layout 是自然 Layout(如 CSS 动画),**不计入 forced** |
| **Long Task** | 渲染主线程 `RunTask` dur > 50ms(标准 Long Task 定义,非新发明的 D2 阈值),仅场景窗口内 |
| **CLS** | 窗口内 `LayoutShift` 事件 score 求和,排除 `had_recent_input` |

每个场景固定 3 次,表中同时列出**三次原始值**与中位数。

## 3. 场景前置断言(录制前及窗口内逐条断言,失败即中止本次录制)

通用:viewport=1280×820、`devicePixelRatio=1.25`、`prefers-reduced-motion=false`、`visibilityState=visible`、router 已挂载。

**S1 — Aurora 首页真实播放(30s,无脚本交互)**
- 播放状态取自 MediaRuntime 发布的 `window.__bottlemusic_media_runtime__.audio`(本应用以 `new Audio()` 持有音频元素,不挂 DOM):`paused=false`、时长 >0、源为本地原生后端流(`http://127.0.0.1:<port>/audio/...`)。
- `currentTrack 非空` 由 `.aurora-pb-play` 的 `:disabled="!currentTrack"` 绑定断言:按钮存在且 `disabled=false`。
- `isPlaying=true` 且 **currentTime 持续增长**:窗口内每 5s 断言一次(30s 内每轮全部通过;trace 佐证:113 次 `timeupdate`,≈250ms 周期覆盖全窗)。
- Aurora canvas 活动断言:`data-motion="active"`、`data-loop="1"`、`data-playing="true"`;trace 佐证:rAF `FunctionCall`(AuroraAtmosphere)次数 ≈ DrawFrame 数,每帧均 raster。

**S2 — Magnet 磁吸控件交互(10s 连续光标移动)**
- 目标为真正绑定 `attachMagnet` 的 `ref="playBtnEl"`(即 `.aurora-pb-play`,见 AuroraPlayerBar.vue);断言其存在且未禁用;`prefers-reduced-motion=false` 已断言(否则 `attachMagnet` 直接返回 noop)。
- 319–320 次 CDP 真实 `Input.dispatchMouseEvent(mouseMoved)`,坐标全部限制在按钮 rect **内缩 6px** 的区域(磁吸位移上限 3px,保证指针始终在按钮区域内)。
- 窗口中段两次断言按钮 computed transform 为非恒等 `matrix(...)` 且随光标变化 —— 证明 `attachMagnet` 的 GSAP tween 真实在跟随(监听器生效的直接行为学证据)。

**S3 — 路由快速切换(20 次)**
- 全部通过 Vue Router `router.push({name})` 完成(history 模式,`location.pathname` 同步变化;非 hash 修改)。
- 顺序 home → lyric → settings 循环 20 次;每次 push **前后**打 UserTiming 标记,并断言 `router.currentRoute.value.name === 目标路由`。三次录制均 20/20 通过(trace 中 nav 标记 40 个/轮)。

---

## 4. 场景三组测量与中位数

### 场景 S1:Aurora 首页真实播放(窗口 30.04–30.09s)

| 运行 | Avg FPS | Median FPS | Avg Frame (ms) | P95 Frame (ms) | Worst Frame (ms) | Layout 自然/Forced | Layout 总耗时 (ms) | Forced ULT¹ | Long Tasks | CLS | timeupdate |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| Run 1 | 74.99 | 75.01 | 13.33 | 13.50 | 14.43 | 2366 / **0** | 2676.39 | **0** | 0 | 0 | 113 |
| Run 2 | 74.73 | 75.01 | 13.38 | 13.49 | 106.97² | 2291 / **0** | 2901.44 | **0** | 0 | 0 | 113 |
| Run 3 | 75.00 | 75.01 | 13.33 | 13.50 | 14.28 | 2257 / **0** | 2827.72 | **0** | 0 | 0 | 113 |
| **中位数** | **74.99** | **75.01** | **13.33** | **13.50** | **14.43** | **2291 / 0** | **2827.72** | **0** | **0** | **0** | **113** |

### 场景 S2:Magnet 磁吸控件交互(窗口 10.03–10.04s)

| 运行 | Avg FPS | Median FPS | Avg Frame (ms) | P95 Frame (ms) | Worst Frame (ms) | Layout 自然/Forced | Layout 总耗时 (ms) | Forced ULT | Long Tasks | CLS | mousemove |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| Run 1 | 74.99 | 75.04 | 13.33 | 13.51 | 14.26 | 790 / **0** | 1004.58 | **0** | 0 | 0 | 319 |
| Run 2 | 74.99 | 75.00 | 13.33 | 13.50 | 14.15 | 791 / **0** | 1011.56 | **0** | 0 | 0 | 320 |
| Run 3 | 74.99 | 74.99 | 13.33 | 13.49 | 13.66 | 790 / **0** | 1000.53 | **0** | 0 | 0 | 320 |
| **中位数** | **74.99** | **75.00** | **13.33** | **13.50** | **14.15** | **790 / 0** | **1004.58** | **0** | **0** | **0** | **320** |

### 场景 S3:路由快速切换 20 次(窗口 3.23–3.45s,脚本节奏 ≈160ms/次)

| 运行 | Avg FPS³ | Median FPS | P95 Frame (ms) | Worst Frame (ms) | Forced Layout(耗时 ms) | Forced ULT(耗时 ms) | Layout 自然 | Long Tasks | CLS |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| Run 1 | 71.13 | 75.15 | 25.10 | 53.59 | 329 (327.24) | 1099 (258.63) | 230 | 0 | 0 |
| Run 2 | 73.17 | 75.29 | 25.22 | 35.03 | 318 (308.64) | 1079 (240.53) | 228 | 0 | 0 |
| Run 3 | 69.83 | 75.16 | 24.42 | 104.29 | 313 (286.44) | 1237 (271.34) | 208 | 1 (104.2ms) | 0 |
| **中位数** | **71.13** | **75.16** | **25.10** | **53.59** | **318 (308.64)** | **1099 (258.63)** | **228** | **0** | **0** |

¹ ULT = `UpdateLayoutTree`(强制样式重算),同样要求带 JS 调用栈才计入 forced。
² Run 2 的单次 107ms 帧间隙未伴随任何 >50ms 主线程任务(合成器侧调度间隙),未计为 Long Task。
³ S3 窗口仅 ~3.3s 且过渡动画本身有帧开销,Avg FPS 被 P95 帧拉低;Median FPS 仍贴 75。

---

## 5. Forced Layout 调用栈归因

三个场景中,**只有 S3 存在 forced reflow**,且全部有同步 JS 调用栈。以中位轮(S3-run2)为例:

| 栈顶(归因) | 位置 | Forced ULT | Forced Layout |
| :--- | :--- | :--- | :--- |
| GSAP `_getComputedProperty2` | `node_modules/.vite/deps/gsap.js:3314` | **1041** | **287** |
| vue-router `computeScrollPosition` | `node_modules/.vite/deps/vue-router.js:370` | 19 | 19 |
| HomeView 卸载/失活钩子 | `src/features/home/HomeView.vue:45 / :55` | 12 | 12 |
| Lyric stage 几何读取 | `src/features/lyrics/useLyricStage.ts:86` | 7 | — |

即:路由过渡期间,GSAP 逐项探测 computed style(约 1300+ 次/窗口)是唯一显著的 forced reflow 来源;vue-router 滚动位置读取与 HomeView 钩子为小头。三次原始归因分布一致(见 `results/analysis-S3-run*.json`)。

**S1/S2 的 Forced Layout / Forced ULT 三次全部为 0。**

**自然 Layout 的来源(非热点,仅为解释计数)**:S1/S2 中 ≈75 次/秒的 Layout 为自然 Layout,来源是 AuroraHome 的装饰性均衡器 `.aurora-eq.is-live i` 应用 CSS `@keyframes aurora-eq-bounce`(height 25%↔100%,infinite),3 根条形每帧使布局失效——这是 CSS 动画的常规代价,不属于 forced reflow,也不归因于任何 JS 读取。

---

## 6. Task D2 修复准则判定

| 评估项 | 判定条件(计划原文) | 观测证据(本基线) | D2 判定 |
| :--- | :--- | :--- | :--- |
| **Magnet 交互 (`motion.ts` `attachMagnet`)** | S2 显示 attachMagnet 产生可观测 forced layout | S2 Forced Layout / Forced ULT = **0 / 0**(三次一致);磁吸读取落在干净布局树上(元素只被 transform),rect 读取未触发同步布局 | **不满足条件 → No Change** |
| **Accent 取色 (`AuroraAtmosphere.readAccentRGB`)** | S1 显示 accent computed style 是可观测热点 | S1 Forced ULT = **0**(三次一致);读取被计数器节流(每 30 次调用一读,paintWash 每帧 4 次递增 ≈ 每 7.5 帧一次),且仅样式树脏时才产生重算 | **不满足条件 → No Change** |
| **路由重排 (Transition)** | S3 显示 CLS > 0 且源头在页面过渡容器 | S3 CLS = **0**(三次一致,20 次已断言的 router.push) | **不满足条件 → No Change** |
| **GSAP 与 Vue 竞争** | S3 显示 GSAP clearProps 与 Vue patch 产生可重现闪烁 | 本基线未测量闪烁(不同指标);但 S3 观测到**栈证实的 GSAP computed style 探测热点**(forced ULT 中位 1099 + forced Layout 318,合计 ≈567ms/窗口,源 `gsap.js:3314`)——不在计划 D2 现有授权范围内,留作未来独立 red-green Task 的候选证据 | **条件未满足 → No Change(本 Phase)** |

**D2 结论:四个候选项均未获得满足计划条件的证据,本 Phase 不做任何动画/布局代码变更。**

---

## 7. 对 08-30 版本的更正(为什么推翻)

对 `outputs/performance-traces-0830/` 的 9 个 trace 逐事件复算(脚本同 `outputs/perf-audit-d1/`),发现:

1. **FPS 系伪影**:旧版 Avg FPS(S1 119.1、S2 139.3、S3 143.8)由混用 `BeginFrame` + `DrawFrame` 两条交错的帧序列计算(半周期增量),例如 S1-run1 的 1000/8.07ms=123.9。真实单一 `DrawFrame` 序列为 **≈75 fps**(实测 74.73–75.29,75Hz 显示器);"Min FPS ≈74.8" 实为真实帧率的另一表述。
2. **"Forced Layout" 计数误标**:旧版把**全部 Layout 事件**当作 forced——其计数与总耗时(如 S1 2246 次 / 3052.71ms)与复算的自然 Layout 完全吻合。旧 trace 中这些 Layout **全部无 JS 调用栈**,属自然 Layout(§5 所述均衡器 CSS 动画)。
3. **S1 readAccentRGB 归因数学不成立,正式推翻**:真实 forced 重算 0 次(带栈捕获的旧 trace 中也仅一次 run 出现 17 次,≈0.57 次/秒);而旧版声称的 2246 次/30s≈74.9 次/秒与其节流读取频率相差两个数量级以上。
4. **S2 旧结论推翻**:0 forced(带栈捕获下亦然)。`attachMagnet` 的 `getBoundingClientRect` 未触发同步布局,"rect 缓存优化"无证据支持。
5. **S3 旧结论部分更正**:CLS=0 成立并经 20 次断言导航确认;但旧版"672 次 forced layout"标注错误——真实 forced = Layout 318 + ULT 1099(中位),源为 **GSAP `_getComputedProperty2`**(§5),旧版未做栈归因,遗漏了这一真实热点。
6. **Long Task 无窗口定义**:旧版计数含启动/加载(如 S1-run1 的 361.8ms 首帧任务),且计数口径不一;本版仅在标记窗口内统计(50ms 标准阈值)。
7. **旧 trace 缺少场景断言**:9 个 trace 中 UserTiming 标记为 0,无任何 currentTrack/isPlaying/currentTime/路由断言,不满足 D1 的验证要求(播放仅能由 timeupdate 周期旁证)。因此本版全部数据以带断言与标记的方式重新录制。
