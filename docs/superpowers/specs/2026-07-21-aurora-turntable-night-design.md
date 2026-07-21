# 极光皮肤「唱机之夜」重设计 Spec

**日期:** 2026-07-21
**状态:** 待用户审阅
**范围:** BottleMusic UI(`ui/`)Aurora 皮肤首页 + 播放 Dock 的视觉重设计。不改信息架构、不改数据源、不改 Newsprint 皮肤。

## Goal

把 Aurora 首页的舞台从"衬线巨标 + 描边卡片 + 满屏粒子"的模板语言，重设计为**唱机之夜**:封面是一张缓慢旋转的黑胶唱片，极光收敛为唱机指示灯与沟槽上的一道绿色反光；动效全部携带播放语义。

## Background / Evidence

调查结论（2026-07-21，含 dev server 实际截图证据):

- 双皮肤架构：`themeStore.skinId` 在 `AuroraShell`/`NewsprintShell`、`AuroraPlayerBar`/`NewsprintPlayerBar` 间切换。
- 重设计主目标文件：`ui/src/views/home/AuroraHome.vue`(1350 行，舞台 hero + 队列 rail + 三个推荐区）、`ui/src/views/home/AuroraAtmosphere.vue`(Canvas 粒子，140/100 颗随播放变化）、`ui/src/components/player/AuroraPlayerBar.vue` + `AuroraDockParticles.vue`。
- 令牌：`ui/src/styles/tokens.css` 四个显式 selector 块（2 皮肤 × 2 模式），深色独立取值（非透明度派生）——必须延续。
- 字体约束：Aurora `--font-sans` 被覆写为 Noto Serif SC（衬线）,`api/__tests__/auroraFont.test.ts` 锁定，不得回退 SF Pro/MiSans。
- 用户已确认的保留清单（2026-07-21 对话）:Noto Serif SC 衬线、翡翠 accent(`#62d6a2` dark / `#18875b` light)、Canvas 粒子、右侧队列 rail、底部浮动 Dock、推荐卡片网格、混合布局（舞台 + 推荐区）。
- 用户问题清单：信息层级不清、视觉中心不明确、播放控制不突出、封面与背景关系混乱、文字对比度不足、留白间距不统一、圆角/阴影/渐变过多、动效无音乐语义、窄屏拥挤（900px 截图证实 sidebar 占 ~200px 挤压舞台）、AI 模板感、粒子特效难看。
- 审美参照：`docs/mineradio-reference-cinema.png`——纯黑、克制、极光作为"光"而非涂料。

## Global Constraints

- 保留混合布局：舞台 hero(唱盘+信息 | 队列 rail)+ 今日推荐/编辑推荐/最新歌单三区，信息架构不变。
- 保留清单内元素全部保留（见上）；允许改变它们的视觉表达与实现。
- `tokens.css` 深色值必须独立选取，禁止用透明度从浅色派生。
- Newsprint 皮肤、`PlayerProgress.vue` 组件内部、`usePlayerControls` API、`homeViewModel`、路由：零改动。
- 所有动效尊重 `prefers-reduced-motion` 与 KeepAlive 激活/停用生命周期（沿用 `api/motion.ts` 现有模式）。
- 全程不新增运行时依赖（GSAP 已在依赖中）。

## Design Decisions

### D1 设计纪律（对应问题清单）

1. **圆只给器物**：唱片、播放键、音量旋钮、进度/音量滑块为圆形；其余元素统一 8px 圆角。废除 999px pill / 20px / 16px / 14px / 12px / 10px 混用。
2. **动效必须有语义**：旋转=播放态、唱针播放头=进度、尘埃=氛围、按压回弹=触控。无其他持续动画。
3. **光只打在舞台上**：光锥只覆盖唱机区；推荐区、rail、Dock 为"暗房家具"，以 1px 细边线分区，不用玻璃拟态卡片堆叠。

### D2 舞台(hero)

- **唱盘**(`.aurora-cover` 改造为黑胶结构):
  - 封面图圆形裁切，直径 ≤320px(≥1600px 时 340px)。
  - 叠加三层:① 静态同心沟槽纹(CSS `repeating-radial-gradient`,alpha 极低);② 中心孔 + 翡翠色唱片 label 圆环;③ 绿色反光弧(conic-gradient mask,与碟同转)。
  - 旋转:~24s/圈,GSAP 驱动;播放时 0.8s ramp 启动,暂停 0.8s 减速停住;reduced-motion 恒静止。
- **信息区**:与唱盘轴心垂直居中对齐。层级:kicker(sans 11px,大写字距,"正在播放/每日推荐")→ 歌名(Noto Serif SC 36px,自 46px 降档)→ 艺人 16px → 音质 chips → 操作行。播放 CTA 为实心翡翠圆钮;"查看歌词/刷新"降为文字链。
- **空态**(无 heroTrack):唱盘位显示静态空盘(仅沟槽纹,无封面图),单行小字"选择一首歌,开始聆听"。废除衬线巨标 + 描边圆角盒 + 径向光晕。
- **加载态**:骨架屏沿用现有结构,样式同步为圆形唱盘骨架。

### D3 背景与粒子(重写 AuroraAtmosphere)

- **光锥**:右上方打入的锥形静态光,纯 CSS 径向渐变实现,只覆盖舞台区,无 rAF 成本。
- **尘埃**:Canvas 粒子重写——粒子只生成于光锥内,锥外 alpha≈0;数量 140→60(播放)/ 100→30(暂停);播放时叠加振幅 <0.5px 的轻微机械抖动(唱机震感),暂停时静止悬浮。保持现有 KeepAlive 安全 rAF 生命周期与 DPR cap。
- **极光表达**:翡翠绿从"主色"降为"点色"——唱片沟槽反光弧、播放键指示光、激活态、进度填充。

### D4 配色与字体

- **仅改 aurora-dark 令牌块**(tokens.css),固定值(对比度已经 WCAG 校验,基准 `--app-bg #0c0b09`):

  | 令牌 | 值 | 对 bg 对比度 |
  |---|---|---|
  | `--app-bg` | `#0c0b09` | — |
  | `--surface-1` | `#13110e` | — |
  | `--surface-2` | `#1a1713` | — |
  | `--surface-elevated` | `#221e19` | — |
  | `--text-primary` | `#f5f2ec` | 17.6:1 |
  | `--text-secondary` | `#a39d93` | 7.3:1 |
  | `--text-muted` | `#8a8378` | 5.2:1(在 `--surface-2` 上 4.8:1,均过 AA) |
  | `--accent` | `#62d6a2`(不变) | 10.9:1 |
  | `--focus-ring` | `rgba(98,214,162,0.42)`(不变) | — |
  | `--border-subtle` | `rgba(245,242,236,0.10)` | — |
  | `--progress-track` | `#45403a` | 填充/轨 5.7:1 |
  | `--progress-buffered` | `#322e29` | — |
  | `--progress-fill` / `--progress-thumb-ring` | `#62d6a2`(不变) | — |
  | `--progress-thumb-fill` | `#ffffff`(不变) | — |
  | `--progress-time` | `#8a8378` | 5.2:1 |
- **aurora-light 不动**(冷白 + 翡翠,避免滑向 newsprint 纸米色)。
- **字阶固定为 4 级**:11 kicker / 13 正文 / 18 区标题 / 36 歌名;时长、序号、时间码用 tabular-nums。废除 11/12/12.5/13/14/18 随机字号。

### D5 播放控制区(Dock)

- **中央传输区常驻**:无 currentTrack 时也渲染完整传输键(disabled 静音态);删除"选择曲目后显示播放控制"虚线占位 pill。
- **播放键**:大号圆形器物钮(内凹微阴影 + 静态翡翠指示光,不脉冲);上下曲为小圆钮;循环/收藏为幽灵图标。沿用现有 `pressBounceDown/Up` 按压回弹。
- **进度条**:`PlayerProgress.vue` 内部不动;经 `[data-skin='aurora']` 外部选择器覆写——3px 轨、缓冲段可见、播放头改为"唱针"小三角(CSS clip-path)。
- **音量**:滑块改圆形小旋钮,与进度条同族线宽。
- **Dock 粒子**(AuroraDockParticles)保留,数量/亮度下调,与舞台尘埃同族(细尘非光斑)。
- Dock 封面缩略图保持方形小图(只有舞台唱盘是圆的——克制)。

### D6 队列 rail 与推荐区

- rail 保留 12 行上限与"每日推荐/正在推荐"双态;改密排"唱片脊":等宽数字编目(01–12),行高收紧,去底色卡片,仅留细分隔线。
- 推荐卡片:方形封面(仅 hero 为黑胶),8px 圆角,重投影改 1px 细边,hover -2px 位移保留。

### D7 响应式

| 宽度 | 规则 |
|---|---|
| ≥1600 | 唱盘 340px,网格卡片加大(沿用) |
| 1359–1279 | 唱盘 280px,rail 收窄(沿用断点) |
| <1279 | rail 隐藏(沿用) |
| <1100 | **sidebar 收为 64px 纯图标栏(新断点;现行为 <900 才收,是窄屏拥挤主因)**;仅 `[data-shell='aurora']`,不影响 newsprint |
| <900 | 唱盘 200px 居上、信息居中(沿用模式);光锥收窄 |

### D8 动效清单(穷举,除此无持续动画)

| 动效 | 触发 | 规格 |
|---|---|---|
| 页面进入 | 路由进入(cold/return) | 沿用 auroraProfile expo.out 0.72s/0.36s + 卡片 stagger |
| 唱片旋转 | isPlaying | GSAP,24s/圈,0.8s ramp 启停;reduced-motion 静止 |
| 沟槽反光弧 | 随碟旋转 | 与旋转同一驱动,无独立动画 |
| 尘埃抖动 | isPlaying | Canvas,<0.5px 振幅 |
| 按压回弹 | pointerdown/up | 沿用 pressBounceDown/Up(elastic.out) |
| hover 位移 | 卡片 hover | -2px,0.2s(沿用) |

## File Map

| 动作 | 文件 |
|---|---|
| 改 aurora-dark 暖黑令牌 | `ui/src/styles/tokens.css` |
| 唱盘模板 + 舞台/排版/空态样式 | `ui/src/views/home/AuroraHome.vue` |
| 光锥尘埃渲染重写 | `ui/src/views/home/AuroraAtmosphere.vue` |
| 新增 `startVinylSpin`(GSAP ramp 启停,reduced-motion/KeepAlive 安全,仿 `startAmbientMotion` 模式);motionProfiles 加 vinyl 段 | `ui/src/api/motion.ts`、`ui/src/api/motionProfiles.ts` |
| 常驻传输区 + 器物化控件样式 | `ui/src/components/player/AuroraPlayerBar.vue` |
| 唱针播放头覆写、sidebar <1100 图标栏、Dock chrome、radii 统一 | `ui/src/styles/skins/aurora.css` |
| 测试同步 | `ui/src/views/home/__tests__/`(AuroraHome/AuroraAtmosphere)、`ui/src/components/player/__tests__/`(AuroraPlayerBar)、`ui/src/api/__tests__/`(motion/motionProfiles/tokens 相关) |
| QA 记录 | `ui/design-qa.md` 更新 + 截图矩阵重跑 |

## Verification

1. `pnpm test`(vitest 全量)——视觉断言类测试随设计同步更新,契约类测试(数据流、交互)必须原样通过。
2. `AURORA_QA_URL=http://localhost:1420/ node scripts/capture-aurora-qa.mjs` 六视角矩阵(1586 dark/light、1440、1280、900、reduced-motion)重跑并人工核对:唱盘圆形与旋转态、光锥范围、暖黑令牌、唱针播放头、<1100 sidebar 图标栏、900px 窄屏舞台垂直化。
3. 对比度抽查:正文/次级文本在 `#0c0b09` 上 ≥4.5:1。
4. reduced-motion 截图中:唱盘静止、尘埃静止、全部控件可用。

## Out of Scope

- Newsprint 皮肤任何改动;歌词页/全屏沉浸视觉;真实 FFT 音频频谱;唱针臂(tonearm)拟物装饰;light 模式调色;后端与数据层;新增依赖。
- 圆形封面裁切损失方形专辑构图(约 21% 面积)为已接受代价(用户 2026-07-21 确认方向时知情)。
