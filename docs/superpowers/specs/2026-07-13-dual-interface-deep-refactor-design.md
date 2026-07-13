# BottleMusic 双界面深度重构设计规格

> 状态：已批准，实施权威来源
> Revision：r1（2026-07-13）
> 实施分支：`codex/dual-interface-deep-refactor`
> 实施方式：严格 TDD、子代理驱动、主代理逐任务审阅与提交

## 1. 文档地位

本文档是方案 C 的唯一产品与架构权威来源。以下文档保留为历史背景，不再单独驱动实现：

- `2026-07-10-dual-interface-player-redesign-design.md`
- `2026-07-11-aurora-immersive-redesign-design.md`
- `2026-07-12-dual-interface-closeout-design.md`
- 对应的旧实施计划与 closeout 报告

如旧文档与本文冲突，以本文为准。已经工作的能力只做回归保护，不重新实现。

## 2. 已确认的问题

### 2.1 稳定性

1. 快速切歌时，上一首的异步音频代理 URL 可能后返回，并覆盖当前歌曲的 `audio.src`。
2. 快速切歌时，上一首歌词请求可能覆盖当前歌词；旧请求的 `finally` 还可能错误结束新请求的 loading。
3. 首页用一个全局 `inFlight` 锁住三个分区。单个慢请求或后备请求重试会让页面长时间像“卡死”。
4. 手工 `currentView`、历史栈、歌词全屏和 GSAP 过渡分散在组件中；中断或异常时可能留下空白中心区、残余样式或错误全屏状态。
5. 页面渲染错误没有局部恢复边界，中心内容出错时缺少可操作的恢复路径。

### 2.2 交互与视觉

1. Aurora 与 Newsprint 的结构和动效差异不足；两套皮肤不应只是换颜色。
2. 非全屏歌词页结构不统一，Newsprint 尚未满足左封面信息、右歌词的双栏要求。
3. 播放器仍存在中文命令文字和手写内联 SVG；命令应图标化，中文保留在无障碍文本中。
4. 全屏播放控制需要更克制：默认隐藏，只有操作时出现。
5. 外观 Drawer 含无效背景、模糊、颗粒、暖度设置；Settings 中“清理缓存”是无真实效果的死按钮。
6. `newspaper` 等皮肤命名和部分功能文案没有完成中文化。
7. Topbar 的分享按钮只显示“已复制链接”，没有真正复制或可分享的桌面 URL，属于误导性死按钮。

## 3. 产品目标

1. **永不假死**：导航、播放器和窗口控制不被页面请求锁住；任何页面错误都有恢复入口。
2. **播放一致**：界面、队列、歌词和真实音频始终指向同一首歌。
3. **两套独立体验**：Aurora 是沉浸、流动、果冻感的现代播放器；Newsprint 是克制、轻微动画的报刊播放器。
4. **交互可预测**：单击、双击、全屏、3D 歌单和路由离开有唯一明确语义。
5. **可维护**：导航、外观、页面恢复、播放意图和歌词会话成为边界清晰的深模块。
6. **可验证**：每个行为先写失败测试，再实现；最终包含组件、集成、类型、构建和真实窗口视觉验收。

## 4. 非目标

- 不重写 Rust/Tauri 后端协议。
- 不删除 `nativeBackend.ts`；仅隔离并记录未来迁移入口。
- 不重做已经可用的 Aurora 3D 歌单、32 项窗口、拖拽防误触和选歌后保持全屏。
- 不开发自定义背景图、背景模糊、纸张颗粒、暖度或字体选择器。
- 不删除开发 QA 的 `?layoutDemo=1` 数据种子和截图工具。
- 不进行与本方案无关的全仓重命名或格式化。

## 5. 架构方案

### 5.1 导航深模块

采用 `vue-router@4` 取代 `App.vue` 中的手工 `currentView`、历史栈和参数分支。

新增建议结构：

```text
ui/src/navigation/
  routes.ts
  router.ts
  navigationLifecycle.ts
```

路由至少包含：`home`、`stats`、`history`、`equalizer`、`settings`、`search`、`playlist`、`lyric`、`login`。

核心不变量：

- 离开 `lyric` 路由时立即执行 `setLyricFullscreen(false)`。
- 路由参数是搜索词和歌单标识的唯一来源。
- 返回/前进由 router history 管理，不再维护第二套历史栈。
- 页面过渡可以被中断；取消后必须恢复 `opacity`、`transform`、`filter`，且导航 Promise 必须完成。
- `KeepAlive` 只缓存明确需要缓存的首页，不通过组件名字符串猜测。

### 5.2 页面恢复边界

新增 `PageRecoveryBoundary.vue`，包裹中心路由视图，不包裹 shell、播放器、队列和窗口控制。

错误状态必须：

- 保留当前皮肤 shell 和底部播放器。
- 显示皮肤化错误信息、`返回首页`、`重试当前页面`。
- 进入错误状态时退出歌词全屏并清理过渡残余样式。
- 记录异常、路由和皮肤信息，不吞掉错误。
- 重试通过改变稳定的 retry key 重建当前页面，不刷新整个 WebView。

### 5.3 播放意图与媒体源租约

`PlaybackOrchestrator` 继续拥有单调递增的 transition epoch，保护 store、历史和 EQ 等外层副作用。`Html5AudioBackend` 内部新增私有 `SourceLease`，保护媒体元素赋值；不扩大 `PlayerBackend` 公共接口。

每次 `playUrl`、`switchUrl` 或 `stop` 都创建/失效 source lease。等待 `prepareSourceUrl()`、metadata 或 `audio.play()` 后，只有当前 lease 才能继续：

- 设置 `audio.src`。
- 设置 currentTime 和调用 `audio.play()`。
- 连接 post-play EQ source。

外层 orchestrator 的 epoch 继续验证：

- `currentTrack/currentIndex/isLoading/errorMsg`。
- 上传播放历史和异步封面更新。
- quality reload、取消播放和队列清空。

旧 lease/epoch 完成时视为 `superseded`，不得覆盖当前播放、pause 新音频或显示用户错误。租约是 HTML5 backend 私有实现，避免把取消细节泄漏到所有 backend。

### 5.4 歌词会话

新增无 Vue 视图依赖的 `lyricsResource.ts`，由它拥有歌词请求、generation、错误与重试；`useLyricStage` 只把 resource 状态适配成舞台 model/commands。

```ts
export interface LyricsResource {
  readonly state: LyricsResourceState;
  load(track: Track | null): Promise<void>;
  retry(): Promise<void>;
  dispose(): void;
}
```

resource 使用与当前 `FileHash` 绑定的 request generation：

- 同一首歌只发起一个初始请求，去掉 watch + onMounted 的重复加载。
- 只有最新 generation 可以写入歌词、loading 和 error。
- 切歌和卸载清理旧的 follow timeout。
- 点击歌词仍走真实 `playerStore.seek()`，并恢复跟随。
- 请求失败有独立 `error` 和 `retry()`，不再把错误伪装成普通歌词行。

### 5.5 首页分区请求

`daily`、`playlists`、`albums` 各自拥有请求 Promise/generation，不再共享全局 `inFlight`。

状态约定：

- 首次无内容：`loading=true`，显示皮肤匹配的 skeleton。
- 已有内容刷新：保留 items，仅 `refreshing=true`。
- 单区失败：保留其他分区；失败区显示中文错误和重试按钮。
- 返回首页：立即显示缓存，再按需要后台刷新。
- 同一区重复点击刷新：复用当前请求或使旧 generation 失效，不并发写入。
- `loaded` 只表示该分区至少成功完成过一次；首次失败时保持 `loaded=false`，使返回首页仍可重试。
- `layoutDemo` 继续绕过网络并提供稳定 QA 数据。

### 5.6 外观设置

删除独立 `Drawer.vue` 入口，把有效设置统一到 `Settings → 外观`。

保留：

- 皮肤：`极光 Aurora` / `报刊 Newsprint`
- 光感：浅色 / 深色
- 强调色
- 紧凑列表
- 歌词对齐

移除：

- 自定义背景图与 URL/data URL 持久化
- 背景暗度、玻璃模糊、颗粒、纸张暖度
- 字体选择器
- 假“清理缓存”按钮及确认状态
- 无真实分享目标的 Topbar 分享按钮

外观状态由单一 `appearanceStore` 负责校验、持久化和 DOM dataset/CSS 变量同步。读取非法 localStorage 值时回退默认值。

### 5.7 播放器控制契约

共享的是命令语义，不共享视觉组件：

```ts
export interface PlayerControlModel {
  hasTrack: boolean;
  isPlaying: boolean;
  loopMode: LoopMode;
  volume: number;
  openLyrics(): void;
  enterLyricFullscreen(): void;
  togglePlay(): Promise<void>;
  previous(): void;
  next(): Promise<void>;
  cycleLoop(): void;
  toggleQueue(): void;
}
```

- Aurora 使用 `@phosphor-icons/vue`，允许填充、双色和圆润图标。
- Newsprint 使用 `lucide-vue-next`，保持线性、机械、编辑感。
- 不手写内联 SVG，不用中文单字代替图标。
- 所有图标按钮必须有中文 `aria-label` 和 `title`，并有 focus-visible 状态。
- 核心顺序固定为上一首 → 播放/暂停 → 下一首；其他控制可按皮肤独立布局。

## 6. 交互合同

### 6.1 歌词与全屏

1. 单击底部播放器封面或歌曲信息：进入非全屏歌词页。
2. 单击底部全屏图标：直接进入歌词页并全屏。
3. 非全屏歌词页双击左侧封面：进入全屏。
4. 非全屏封面下方的紧凑全屏图标：进入全屏。
5. Aurora 全屏大封面单击：打开 3D 歌单。
6. 3D 歌单选择歌曲：切歌，但保持歌词全屏和 3D/沉浸上下文。
7. 离开歌词路由：一次操作立即退出全屏，不需要第二次退出。
8. `Esc`：只退出歌词全屏，不触发导航。

### 6.2 非全屏歌词布局

两套皮肤都使用左右双栏：

- 左侧：真实封面、歌曲名、歌手、专辑/音质简要信息、紧凑全屏图标。
- 右侧：歌词列占满剩余空间。
- “恢复歌词跟随”固定在歌词列底部控制区，不覆盖歌词行。
- 应用最小宽度内不退化为单列；窄窗口以压缩间距和字号解决。

### 6.3 全屏控制显隐

- 播放/进度控制默认隐藏。
- 指针移动、键盘 focus 进入控制区、或指针接近底边时出现。
- 空闲后渐隐；播放中与暂停时规则一致。
- `prefers-reduced-motion` 下取消位移动画，但不能取消可见性和可操作性。
- 顶部仅保留最小化与退出全屏，不出现额外文本命令。

## 7. 两套视觉语言

### 7.1 极光 Aurora

- 现代、沉浸、内容聚焦；参考 Mineradio 的舞台关系，但不照抄布局或品牌。
- 动效丰富：弹性 dock、果冻按钮反馈、封面/歌词层次过渡、低速氛围粒子。
- 动画以 transform/opacity 为主；避免 border、layout 和阴影高频抖动。
- “正在播放”粒子和 dock 粒子降低速度，不能抢过歌词与封面。
- 深色与浅色都要保持进度、focus、禁用和 hover 的对比度。
- `BottleMusic` 使用轻盈、高对比、略带动势的字标，下方显示 `极光 Aurora`。

### 7.2 报刊 Newsprint

- 编辑排版、清晰分栏、纸面秩序；动画轻微且可被快速打断。
- 进入/离开使用短距离淡入和裁切，不使用果冻或大幅漂移。
- 保留报刊特色图标和排版，不要求与 Aurora 图标统一。
- 移除突兀的整页双横线；分隔线只服务局部层级。
- `BottleMusic` 使用 masthead 式衬线字标，下方显示 `报刊 Newsprint`。

### 7.3 功能文案

- 功能文字以中文为主。
- 装饰英文必须同时有中文含义，不允许单独出现 `newspaper` 等未翻译词。
- 品牌名、歌曲名、艺人名和行业标准缩写保持原文。

## 8. 加载、空状态与错误

每个皮肤独立实现相同语义的视图状态：

- `loading-empty`：无内容时 skeleton。
- `refreshing-content`：旧内容保持，刷新标记轻量显示。
- `empty`：请求成功但无结果。
- `section-error`：单区错误和重试。
- `page-error`：中心页恢复边界。

首页上半部的“正在播放/当前内容舞台”直接读取 `playerStore.currentTrack`，不得等待 daily/playlists/albums。已有当前歌曲时即刻呈现；没有当前歌曲时显示设计完整的空播放状态，不能留下无说明的大块空白。

任何状态都不得遮挡底部播放器、窗口控制或主导航。

## 9. 保护项与清理准则

### 必须保护

- `ui/src/api/nativeBackend.ts`
- `ui/src/api/homeFeedStore.ts` 中 `?layoutDemo=1`
- 现有 3D 歌单真实队列、选中态、跟随和拖拽防误触
- Rust/Tauri 音频代理与诊断链路
- 用户现有未提交改动；实施工作树从干净基线独立执行

### 允许删除

只有测试或 `rg` 证明无生产引用后才删除：

- `Drawer.vue` 及入口
- 失效的背景、模糊、颗粒、暖度、字体状态与 CSS
- 假缓存按钮及 modal 状态
- 未被模板使用的旧全局播放器 CSS
- 重复皮肤规则和无模板引用的旧播放器规则

NewsprintShell 真实使用的 `paper-base`、`paper-fibers`、`paper-grain`、`paper-vignette` 必须保留；只能移除自定义背景分支和有证据的死规则。

## 10. 测试策略

严格 RED → GREEN → REFACTOR：

1. 单元测试：播放 epoch、歌词 generation、首页分区状态、外观校验。
2. 组件测试：路由离开全屏、错误恢复、图标按钮/a11y、双栏歌词、自动隐藏控制。
3. 集成测试：快速 A→B 切歌、慢请求返回顺序、返回首页缓存、3D 歌单选歌保持全屏。
4. 静态验证：`vue-tsc --noEmit`、`vite build`。
5. 真实窗口视觉 QA：Aurora/Newsprint × 浅色/深色 × 普通/歌词/全屏；检查空白页、进度对比、按钮拥挤、歌词遮挡、边框抖动和粒子速度。

实施阶段不得先修改测试以接受错误行为。旧测试若锁定了错误要求，必须先写新的产品合同测试，再删除或改写冲突断言，并在提交说明中指出原因。

## 11. 实施顺序

0. 基线核对：实施分支从 `main@8d4d5dcd` 开始；该提交已包含旧双界面分支的等价功能和后续修正。根工作树还有 8 个未提交的源文件/测试候选改动，执行者只能按行为和 RED 测试选择性移植，不得整包应用，更不得导入未跟踪 QA PNG。
1. 稳定性内核：播放、歌词、首页并发。
2. 导航、页面恢复和外观状态。
3. 双皮肤播放器、歌词布局、全屏显隐和文案。
4. 保守清理、全量回归和真实窗口视觉验收。

每阶段独立提交，上一阶段测试通过后才进入下一阶段。
