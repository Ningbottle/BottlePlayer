# Style Ownership Inventory（Task E1）

审计日期：2026-08-31 · 基线 commit：`7a1d4a69` · 本文档只清点，不修改任何 CSS（E3/E4 的输入）。

> **E3a 执行更正（2026-08-31）**：已删除本清单证明为零引用的 17 个 selector family 与 9 个 token 名称；删除死规则后又暴露并删除了 0 使用的 `--accent-deep`。原清单把 `.artists/.artist/.ah` 整组判死是错误的：精确类名 `.artist` 正被 Search / History / Playlist / Stats 使用，因此整组保留，等待跨 Feature owner 收敛。下文行号与数量仍是 E1 基线口径，E3a 结果以本注记及 §4.2/§4.6/§7 的更正为准。

> **E3b 执行更正（2026-08-31，HEAD `3edd059f`）**：Settings/PageRecovery/Lyrics 三组 owner 规则已从 style.css **原样迁出**到 Feature/shell-local CSS（声明值、选择器名、顺序均不变；skin 变体随本体同迁）。
>
> **E3c/E3d/E4 执行更正（2026-08-31，最终状态）**：E3c 将 shell chrome 迁入 `app/shell/shell.css`；E3d 将 Newsprint Home 组迁入 `NewsprintHome.vue` scoped、`.recent` 迁入 `QueuePanel.vue` scoped、删除经生产引用 + 截图前后对比证明的死规则（`.artists`/`.artist .ah`/`.artist .nm`/dark `.toast`/`.toast-2`），裸 `.artist` 为跨 Feature 活跃共享选择器保留于 shared；`.page-head` 全家为 NewsprintHome + LoginView 双调用方的共享 page chrome，保留 global。E4 在 style.css 只剩 tokens + shared 时执行：`git mv` 为 **`ui/src/styles/base.css`**。§4 各表行号全部为 E1 存档口径。观察项：`.lyric-right` 生产模板 0 引用（a659a915 孤儿化），仅观察、不删除。

## 0. 目录现状

> E3b 后实际 tree 见 §0.1；下图为 E1 审计时点快照，保留作历史口径。

```text
ui/src/
├── style.css                ⚠️ 单文件 1576 行：legacy tokens + shared + 3 类 Feature 规则混住
├── styles/
│   ├── tokens.css           ✅ semantic tokens（--app-bg/--surface-*/--text-* 等），5 个显式块（105 行）
│   ├── progress.css         ✅ PlayerProgress 组件样式 + 组件局部 token（137 行）
│   └── skins/
│       ├── aurora.css       ✅ Aurora shell/chrome overrides，无 token 重定义（535 行，84 个顶层规则组）
│       └── newsprint.css    ✅ Newsprint shell/chrome overrides，无 token 重定义（419 行，69 个顶层规则组）
└── features/                ✅ Feature 私有样式已在各 .vue scoped style 内
    ├── account/  home/  library/  lyrics/  overlays/  search/  settings/  stats/
```

CSS 载入顺序（`ui/src/main.ts:19-23`，cascade 事实基础）：

```text
1. ./styles/tokens.css
2. ./styles/progress.css
3. ./style.css
4. ./styles/skins/aurora.css
5. ./styles/skins/newsprint.css
```

`ui/index.html:8-22` 的内联 FOUC 脚本在任何 CSS 加载前设置 `data-skin`（默认 `aurora`）与 `data-mode`（默认 `light`），因此所有 `[data-skin]/[data-mode]` 限定块自首次绘制即生效。

### 0.1 Phase E 完成后 tree 与载入顺序（2026-08-31，权威现状）

```text
ui/src/
├── styles/
│   ├── base.css                           370 行：legacy tokens + reset + scrollbar + paper + .scroll +
│   │                                        .page-head（shared page chrome）+ svg + .artist（跨 Feature 共享）+
│   │                                        .list-view/.song-row/.spinner + html.compact + reduced-motion
│   ├── tokens.css                         semantic tokens（不变）
│   ├── progress.css                       PlayerProgress（不变）
│   └── skins/aurora.css / newsprint.css   skin overrides（不变）
├── app/shell/
│   ├── shell.css                          388 行：.app/.sidebar 全家/.topbar 组/.titlebar 全家/
│   │                                        .app.lyric-fullscreen-active + dark shell 份额
│   └── pageRecovery.css                   69 行：.page-recovery* 全家
└── features/
    ├── settings/settings.css              61 行：.settings-*/.diag-*/.status-list + 两 skin 变体
    ├── lyrics/lyrics.css                  107 行：.lyric-* 全家 + html.lyric-left + lyric dark overrides
    ├── home/NewsprintHome.vue (scoped)    .feature/.hero/.side-list/.section-bar/.grid/.card 全家 +
    │                                      dark 份额（既有 warm side-list dark 规则保留为唯一来源）
    └── playback/QueuePanel.vue (scoped)   .recent 全家 + dark hover + html.compact 两条

CSS 载入顺序（`ui/src/main.ts`，最终 9 项；import-order 契约由 `styleOwnership.test.ts` 锁定）：

```text
1. ./styles/tokens.css
2. ./styles/progress.css
3. ./styles/base.css          ← E4 rename（原 style.css）
4. ./app/shell/shell.css
5. ./features/settings/settings.css
6. ./app/shell/pageRecovery.css
7. ./features/lyrics/lyrics.css
8. ./styles/skins/aurora.css
9. ./styles/skins/newsprint.css
```

## 1. 五类清点总览

| 类别 | 定义处 | 数量 | 复算命令 |
| :--- | :--- | :--- | :--- |
| 1. Legacy tokens | `style.css` `:root`(:3) + 3 个变体块(:53,:87,:112) | 27 个名称（:root 25 + 组件局部 2） | `rg -o -e '^\s*--[A-Za-z0-9-]+' ui/src/style.css \| sort -u` |
| 2. Semantic tokens | `styles/tokens.css` 5 块(:8,:28,:48,:68,:88) | 16 个名称 | `rg -o -e '^\s*--[A-Za-z0-9-]+' ui/src/styles/tokens.css` |
| 3. Shared selectors | `style.css` reset/scrollbar/list/spinner/compact 等 | 12 组（含 3 死组） | §3 表 |
| 4. Feature selectors | `style.css` 内的 settings/lyrics/home/shell-chrome/legacy-player 组 | 51 组（含 12 死组） | §4 表 |
| 5. Skin overrides | `styles/skins/aurora.css` / `newsprint.css` | 84 / 69 个顶层规则组 | `grep -cE '^\s*[^/@].*\{\s*$' ui/src/styles/skins/aurora.css`（newsprint 同） |

另有两处游离定义：`--on-accent`（skins 各定义 1 次，0 使用，E3a 已删）、`--page-recovery-*`（E1 时在 `style.css:1513-1514`，组件局部；E3b 已随 owner 迁入 `app/shell/pageRecovery.css`）。

usage 计数统一口径：`rg -o -e 'var\(--NAME[),]' ui/src | wc -l`（精确边界，避免 `--ink` 吞掉 `--ink-soft` 前缀）。

## 2. Token 逐项清单

### 2.1 Legacy tokens（E4 后由 `styles/base.css` 拥有；逐项替换须按 §2.3 逐皮肤 QA）

`:root` 基础块（style.css:3-50）定义 25 个名称；其中 17 个在 aurora / aurora-dark / dark 变体块重定义。逐项：

| Token | 变体块数 | var() 使用 | 使用证据（style.css 之外） | 死定义? |
| :--- | :--- | :--- | :--- | :--- |
| `--paper` | 4 | 48 | Sidebar/Topbar/LoginView/QueuePanel 等 | 否 |
| `--paper-2` | 4 | 14 | QueuePanel:262 等 | 否 |
| `--paper-edge` | 4 | 3 | style.css:641,:1282、QueuePanel:262 | 否 |
| `--paper-alt`（alias→`--paper-2`） | 4 | 1 | LoginView.vue:263 | 否 |
| `--ink` | 4 | 78 | 全仓广泛 | 否 |
| `--ink-soft` | 4 | 54 | 全仓广泛 | 否 |
| `--ink-mute` | 4 | 67 | 全仓广泛 | 否 |
| `--ink-faint` | 4 | 4 | style.css:374、NewsprintLyricStage:459、QueuePanel:289 | 否 |
| `--ink-light`（alias→`--ink-faint`） | 4 | 1 | AddToPlaylistModal.vue:151 | 否 |
| `--ink-soft-10` | 4 | 0 | 无 | **是** |
| `--rule` | 4 | 46 | 全仓广泛 | 否 |
| `--rule-soft` | 4 | 28 | 全仓广泛 | 否 |
| `--accent` | 3（dark 块无） | 171 | 全仓广泛；**同时被 tokens.css 定义，见 §2.3** | 否 |
| `--accent-deep` | 3（dark 块无） | 2 | 仅 style.css:929,:1430 | 否（弱使用） |
| `--glass-tint` | 4 | 0 | 无 | **是** |
| `--glass-tint-2` | 4 | 0 | 无 | **是** |
| `--glass-edge` | 4 | 0 | 无 | **是** |
| `--glass-shadow` | 2 | 0 | 无 | **是** |
| `--font-serif` | 2（:root + aurora） | 32 | 全仓广泛 | 否 |
| `--font-sans` | 2（:root + aurora） | 22 | 全仓广泛 | 否 |
| `--ease-spa` | 1 | 17 | progress.css:87、LyricFollowFooter/QueuePanel/NewsprintPlayerBar 等 | 否 |
| `--ease-material` | 1 | 0 | 无 | **是** |
| `--dur-fast` | 1 | 6 | 仅 style.css 内部 | 否（弱使用） |
| `--dur-normal` | 1 | 0 | 无 | **是** |
| `--dur-slow` | 1 | 0 | 无 | **是** |

组件局部的 `--page-recovery-border/--page-recovery-muted`（E1 时在 style.css:1513-1514，定义与使用均在 `.page-recovery` 块内）不算 legacy vocabulary，E3b 已随 owner 迁入 `app/shell/pageRecovery.css`。

**E1 死定义合计：8 个名称、21 条声明**（`--ink-soft-10`×4、`--glass-tint/-2/edge` 各×4、`--glass-shadow`×2、`--ease-material`、`--dur-normal`、`--dur-slow`；另有 skins 的 `--on-accent`×2，见 §2.4）。这些定义已由 E3a 删除；死 selector 删除后变为 0 使用的 `--accent-deep`×3 也已一并删除。

### 2.2 Semantic tokens（tokens.css 拥有，owner 不变）

16 个名称 × 5 块（`:root` fallback(:8) + aurora-light(:28) + aurora-dark(:48) + newsprint-light(:68) + newsprint-dark(:88)），暗色值为独立选定、非透明度派生（文件头注释）。

| Token | var() 使用 | 使用证据 |
| :--- | :--- | :--- |
| `--app-bg` | 14 | aurora.css:17、skins/组件 |
| `--surface-1` | 18 | skins/组件 |
| `--surface-2` | 25 | skins/组件 |
| `--surface-elevated` | 19 | skins/progress.css:121/组件 |
| `--text-primary` | 100 | 全仓 |
| `--text-secondary` | 41 | 全仓 |
| `--text-muted` | 48 | 全仓 |
| `--accent` | （见 §2.3 双定义） | |
| `--focus-ring` | 16 | progress.css、skins |
| `--border-subtle` | 39 | skins/组件 |
| `--progress-track` | 4 | progress.css:45、AuroraLyricStage.vue、AuroraPlayerBar.vue |
| `--progress-buffered/fill/thumb-fill/thumb-ring/time` | 1/3/2/1/1 | progress.css 为主 |

`progress.css:34-35` 的 `--progress-pct/--progress-buffered-pct` 是 `.progress-track` 元素局部 token，owner 即该组件，不迁移。

### 2.3 Legacy ↔ Semantic 逐项映射（禁止未映射直接合并）

两套词汇表**同名冲突只有一个：`--accent`**。其余名称不同，一律不得判为重复；逐项映射如下（映射方向：legacy → semantic。`＝` 表示当前值在皮肤×模式组合下语义等价，`≈` 表示接近但存在组合值不同，`∅` 表示无对应）：

| Legacy（style.css） | Semantic 候选（tokens.css） | 等价性 | 合并风险 |
| :--- | :--- | :--- | :--- |
| `--paper` | `--app-bg` | ≈（aurora：#eef3f0 同值；newsprint-light：#f1ead8 = --app-bg #f1ead8；newsprint-dark：--paper #1c1c1e ≠ --app-bg #1a1714） | newsprint-dark 两值不同，替换会改底色 |
| `--paper-2` | `--surface-2` | ≈（aurora 同值 #e7eeea；newsprint-light：#ebe2cb ≠ #e0d5b8） | newsprint 下深浅不同 |
| `--paper-edge` | ∅ | — | 无对应；保留 |
| `--paper-alt` | `--surface-1` | ≈（aurora：#f7faf8 同值；newsprint-light：#ebe2cb = surface-1 #ebe2cb 仅 light 相等） | dark 组合不等 |
| `--ink` | `--text-primary` | ≈（aurora：#16201d 同值；newsprint-light：#221b12 同值；dark 组合分别 #ffffff vs #f5f2ec / #f5f0e4） | dark 组合值不同 |
| `--ink-soft` | `--text-secondary` | ≈（light 组合同值；dark 组合不同） | 同上 |
| `--ink-mute` | `--text-muted` | ≈（同上模式） | 同上 |
| `--ink-faint` | ∅ | — | 无对应；保留 |
| `--ink-light` | `--text-muted` | ≈（= --ink-faint，见 §2.1） | alias 语义已偏移 |
| `--ink-soft-10` | ∅（死定义） | — | 可随 E3 直接删（唯一无风险项） |
| `--rule` | `--border-subtle` | ≈（aurora：rgba(22,32,29,0.08) 同值；newsprint-light：rgba(34,27,18,0.14) 同值；dark 组合不同） | dark 组合值不同 |
| `--rule-soft` | ∅ | — | 无对应；保留 |
| `--accent` | `--accent`（同名） | **双定义冲突，见下** | 见下 |
| `--accent-deep` | ∅ | — | 无对应；保留 |
| `--glass-tint/-2/edge/shadow` | ∅（死定义） | — | 可随 E3 直接删 |
| `--font-serif/--font-sans` | ∅ | — | tokens.css 不管字体；保留在 base |
| `--ease-spa/--dur-*` | ∅ | — | motion token，保留在 base |

**`--accent` 双定义冲突（唯一）：**

- tokens.css：`:root`(:16) #18875b、aurora-light(:36) #18875b、aurora-dark(:56) #62d6a2、newsprint-light(:76) #a8311b、newsprint-dark(:96) #c4391e。
- style.css：`:root`(:20) #a8311b、`:root[data-skin=aurora]`(:70) #18875b、`:root[data-skin=aurora][data-mode=dark]`(:103) #62d6a2。

当前结果由特异性+载入顺序共同决定：`[data-skin][data-mode]` 三元块（0,3,0）> 双块（0,2,0）> 单 `:root`（0,1,0）；同特异性时后载入的 style.css 胜 tokens.css。因 index.html FOUC 脚本保证 `data-skin/data-mode` 恒存在，实际取值恒等于 tokens.css 的对应块；但**若 FOUC 脚本被移除或 SSR 化，裸 `:root` 下 style.css 的 newsprint 红 #a8311b 将覆盖 tokens.css 的 aurora 绿 fallback**。E3 收敛时须删 style.css 侧的 `--accent` 定义而非 tokens.css 侧。

### 2.4 游离定义

- `--on-accent`：aurora.css:227（#ffffff）与 newsprint.css:240（var(--paper)）各定义 1 次，**全仓 0 使用**（AuroraPlayerBar 用的是组件局部 `--pb-glass`，AuroraPlayerBar.vue:427）。死定义。

### 2.5 未定义变量（使用处无任何定义）

| Token | 使用处 | 状态 |
| :--- | :--- | :--- |
| `--border` | SettingsView.vue:703 `var(--rule, var(--border, #ccc))` | **从未定义**；因 `--rule` 恒有定义，内层 fallback 永不触发（dead fallback） |
| `--lyric-font-size` / `--lyric-opacity` | DesktopLyricView.vue:147 内联 `:style` 定义、:272 等消费 | 非"未定义"：运行时由组件内联注入，组件局部 |

复算：`rg -n -F -e '--border:' ui/src`（0 命中）vs `rg -n -F -e 'var(--border' ui/src`（1 命中）。

## 3. Shared selectors（E4 后由 `styles/base.css` 拥有，owner = 全局）

| 组 | style.css 行 | 使用证据 | 状态 |
| :--- | :--- | :--- | :--- |
| Reset：`*`/`html,body,#app`/`a`/`button` | 141-172 | 全局 | 活 |
| Scrollbar `::-webkit-scrollbar*` + dark | 174-194 | 全局 | 活 |
| `.paper-base/.paper-grain/.paper-fibers/.paper-vignette` | 196-215 + :133-139 | app/shell/NewsprintShell.vue:21-24 渲染 paper 层 | 活（aurora 隐藏，见 §6/C8） |
| `.list-view` | 954-961 | Stats/History/Playlist/Login/HomeView/NewsprintHome（6 文件） | 活 |
| `.song-row`（+:hover/.active/子元素） | 962-1010 | PlaylistView/HistoryView/SearchView（3 文件） | 活 |
| `.spinner` + `@keyframes rotate` | 1012-1031 | 6 个 Feature 视图 | 活 |
| `html.compact` 组 | 1186-1199 | appearanceStore.ts:89 `classList.toggle('compact')` | 活 |
| `html.lyric-left .lyric-line` | 1204-1209 | appearanceStore.ts:90 | 活（注意：选择器同时钉住 lyric feature 的 `.lyric-line`） |
| `@media (prefers-reduced-motion: reduce)` 全局关闭 | 1395-1402 | 全局 | 活（计划禁止本 Phase 触碰） |
| `.btn-primary/.btn-secondary/.btn-ghost` | 1424-1450 | **0 引用** | **死** |
| `.dim`、`svg{display:block}` | 949-951 | `.dim` 0 引用 | `.dim` **死** |
| `.lyric-container` | 1034-1039 | 0 引用 | **死** |

## 4. Feature selectors（现居 style.css，E3 目标 = 对应 Feature 的 scoped style 或 Feature-local CSS）

### 4.1 Shell chrome（owner = `src/app/shell/`）

| 组 | style.css 行 | 使用证据 |
| :--- | :--- | :--- |
| `.app` grid 骨架 | 218-225 | App.vue（被 skins 的 `.app[data-shell]` 覆盖，见 §6/C2） |
| `.sidebar` 全家（`.masthead/.user/.avatar/.section-label/.nav/.playlists/.playlist-placeholder/.playlist-retry/.sidebar-footer`） | 228-393 | app/shell/Sidebar.vue |
| `.topbar/.nav-arrows/.icon-btn/.search/.free-badge/.top-actions` | 404-468 | app/shell/Topbar.vue |
| `.titlebar/.titlebar-logo/.titlebar-center/.titlebar-controls` | 1124-1184 | App.vue、shell/AuroraShell.vue、shell/NewsprintShell.vue、shell/FullscreenWindowControls.vue |
| `.app.lyric-fullscreen-active` 组 | 1404-1419 | App.vue / shells |
| dark 变体中针对以上 selector 的 `:root[data-mode=dark]` 覆盖 | 1219-1393 内散布 | 同上 |
| `.page-recovery*` | 1512-1576 | app/shell/PageRecoveryBoundary.vue（shell，非 feature）。**E3b 已迁出** → `app/shell/pageRecovery.css` |

### 4.2 Newsprint Home feature（owner = `src/features/home/NewsprintHome.vue`）

| 组 | style.css 行 | 使用证据 | 状态 |
| :--- | :--- | :--- | :--- |
| `.page-head` 全家 | 484-516 | NewsprintHome.vue、LoginView.vue、shared/ui/SkinPageHeader.vue（3 处跨 feature 使用 → 迁移前须先收敛调用方） | 活 |
| `.feature/.hero/.side-list` 全家 | 519-604 | NewsprintHome.vue | 活 |
| `.section-bar` | 607-622 | NewsprintHome.vue | 活 |
| `.grid` | 624-630 | NewsprintHome.vue | 活 |
| `.card` 全家 | 631-690 | NewsprintHome.vue | 活 |
| `.func-grid/.func` | 692-712 | 0 引用 | **死** |
| `.artists/.artist`（含 `.ah`） | 714-736 | `.artist` 精确命中 SearchView、HistoryView、PlaylistView 与 StatsView；CSS 不会区分“修饰类”与“本组” | **活，且存在跨 Feature 命名碰撞；E3a 保留整组** |
| `.recent` 全家 | 738-773 | **NewsprintHome.vue + playback/components/QueuePanel.vue:111 两处**（跨 feature 共用，迁移需拆分或先统一） | 活 |

### 4.3 Lyrics feature（owner = `src/features/lyrics/`，E3b 后 = `features/lyrics/lyrics.css`）

**E3b 已迁出**：下表全部规则现位于 `ui/src/features/lyrics/lyrics.css`（107 行，含 `html.lyric-left` 与 dark 覆盖；`html.compact .lyric-scroll` 与 `.app.lyric-fullscreen-active` 除外——前者随 §3 compact 组留在 style.css，后者随 §4.1 shell chrome 留在 style.css）。行号为 E1 基线口径，仅存档。

| 组 | style.css 行（E1 口径） | 使用证据 |
| :--- | :--- | :--- |
| `.lyric-meta/.lyric-right/.lyric-scroll/.lyric-line` 全家 | 1040-1122 | AuroraLyricStage/NewsprintLyricStage/LyricView/DesktopLyricView。**E3b 复核：`.lyric-right` 在生产 Vue 模板中 0 引用**（a659a915 舞台重建时孤儿化，本表原使用证据有误）；按 E3b"原样迁入"随组保留在 lyrics.css，仅观察、不删除 |

### 4.4 Legacy playback player（owner 已迁 `src/playback/components/player/`，CSS 全死）

`.player/.np/.transport/.t-btn/.seek/.track/.player-right/.quality/.p-icon/.volume`：style.css 775-947，约 170 行。**全部 0 引用**——两条 PlayerBar 已是 AuroraPlayerBar.vue（`aurora-pb-*`）与 NewsprintPlayerBar.vue（`np-pb-*`，skin 覆盖在 newsprint.css:243-294）。复算：`rg -n 'class="[^"]*\b(player|np|transport|t-btn|seek|track|volume|quality|p-icon)\b' ui/src --glob '*.vue'` 仅命中 `aurora-pb-*`/`np-pb-*`/`island-transport` 等复合名。

### 4.5 Settings feature + 皮肤变体（owner = `src/features/settings/SettingsView.vue`，E3b 后 = `features/settings/settings.css`）

**E3b 已迁出**：`.settings-*`、`.diag-*`、`.status-list` 及 `:root[data-skin="aurora"/"newsprint"] .settings-*` 皮肤变体现全部位于 `ui/src/features/settings/settings.css`（61 行，变体随本体归 Feature，未进 skins/）。行号为 E1 基线口径，仅存档。

E1 时态记录（已解决）：`.settings-*`（1455-1499）、`.diag-*`（1489-1494）、`.status-list`（1495-1498）当时住在 style.css；`:root[data-skin="aurora"] .settings-*`（1501-1503）与 `:root[data-skin="newsprint"] .settings-*`（1506-1509）是皮肤变体却同住 style.css（所有权泄漏，见 §6/C9）。E3b 连同本体一起归 settings feature。附带：SettingsView.vue 的死 fallback `var(--rule, var(--border, #ccc))` 已收敛为 `var(--rule)`（commit `3edd059f`，§2.5）。

### 4.6 死 selector 合计

E3a 已删除经精确模板 class、动态 class 与 DOM selector 三路检索均为 0 引用的 17 个 family：`.btn-primary/.btn-secondary/.btn-ghost`、`.dim`、`.lyric-container`、`.func-grid/.func`，以及 §4.4 的 `.player/.np/.transport/.t-btn/.seek/.track/.player-right/.quality/.p-icon/.volume`。`.artists/.artist/.ah` 不在删除集合；其中 `.artist` 是活跃全局类，须在后续 owner 迁移中先消除跨 Feature 碰撞。

## 5. Skin overrides（styles/skins/，owner 已正确）

- `aurora.css`（535 行，84 个顶层规则组）：`.app[data-shell=aurora]` grid、`.shell-*` 布局、`.lyric-fullscreen-active` 覆盖、sidebar/topbar chrome（`[data-skin-chrome=aurora]`）、playerbar dock、stage 响应式、Skin* primitives（`[data-skin=aurora] .skin-*`）。
- `newsprint.css`（419 行，69 个顶层规则组）：同构的 newsprint 版本 + `.newsprint-stage-*` 骨架屏 + `.np-pb-*` 暗色覆盖组。
- E1 时两文件各有一条无使用的 `--on-accent`；E3a 删除后，两文件均**无 token 重定义**（与文件头 owner 声明一致）。
- skin override 对 style.css 的覆盖关系见 §6。

## 6. 跨文件覆盖与 cascade-order 依赖（全部只记录，不改）

| # | 覆盖对 | 机制 | 顺序敏感? |
| :--- | :--- | :--- | :--- |
| C1 | tokens.css `:root` --accent #18875b ← style.css `:root` #a8311b | 同特异性 (0,1,0)，style.css 后载入获胜；FOUC 脚本设置属性后由 (0,2,0)/(0,3,0) 块接管 | **是**（见 §2.3） |
| C2 | style.css `.app` grid（232px/88px 行）← aurora.css:7 / newsprint.css:7 `.app[data-shell]` | 特异性 (0,2,0)>(0,1,0)，顺序无关 | 否 |
| C3 | style.css `.scroll` padding(:480) ← aurora.css:56 padding + :63 `:has(.aurora-home)` 归零 | aurora.css 后载入 | **是**（改 import 顺序即回归） |
| C4 | style.css `.titlebar` grid 放置(:1126) ↔ aurora.css:19 / newsprint.css:17 重复声明 | 值相同，冗余但无害 | 否 |
| C5 | style.css `.app.lyric-fullscreen-active`(:1404，rows `32px 0 1fr 0` !important) ← skins（rows `0 0 1fr 0` !important，aurora.css:74 / newsprint.css:57） | 双方 !important 同级，skin 特异性 (0,3,0)>(0,2,0) | 否 |
| C6 | style.css `.icon-btn`(:412) 与 dark 覆盖(:1235) ← aurora.css:510 `[data-skin-chrome=aurora].topbar .icon-btn` | 同特异性 (0,3,0) 时 aurora.css 后载入获胜；dark+aurora 组合两者都命中 | **是** |
| C7 | style.css `.nav a.active`(:324) ← aurora.css:178 / newsprint.css:222 `[data-skin-chrome].sidebar .nav > a.active` | skin 特异性更高 | 否 |
| C8 | style.css `.paper-*` aurora 隐藏(:134-139) ↔ aurora.css:164-169 **完全重复** | 同规则双文件冗余 | 否（删任一需 QA 佐证） |
| C9 | `.settings-*` 皮肤变体住在 style.css（:1501-1509）而非 skins/ | 所有权放错文件（非 cascade 问题） | — | **E3b 已解决**：变体随本体迁入 `features/settings/settings.css`（feature-local，非 skins/） |

## 7. E3 输入摘要（E3a/E3b 执行后存档）

> **E3b 完成记录（2026-08-31，4 commit）**：`f8049c45` Settings → `features/settings/settings.css`；`4996e399` PageRecovery → `app/shell/pageRecovery.css`（PageRecoveryBoundary 测试同步改读 owner 文件）；`9cf52203` Lyrics → `features/lyrics/lyrics.css`；`3edd059f` SettingsView 死 fallback 收敛。三文件在 main.ts 中紧接 style.css、两 skin CSS 之前导入（§0.1）；source-level gate `ui/src/test/__tests__/styleOwnership.test.ts` 锁定 selector 归属与 import-order 契约。视觉验证：Aurora 6/6 + Newsprint 2/2 PNG hash 与 E2 基线一致。E3b 共 4 个实现 commit，随后 `77952d9f` 为 gate/doc correction（无条件 readFileSync 加固 + import-order 契约 + 本文档 E3b 记录）。**全部已完成**：E3c（shell chrome）→ E3d（home/.recent/.artist 收敛）→ shared 收敛 + E4 rename（最终顺序见 §0.1）。Phase E 结束时 base.css 仅含 tokens 与 shared 规则。

> **E3c/E3d/E4 完成记录（2026-08-31）**：`c35915f0` refactor(shell): colocate shell chrome styles（shell.css 388 行 + gate shell suite + import-order 9 项契约）；`a61e0252` refactor(home): colocate Newsprint Home styles and converge cross-feature selectors（NewsprintHome/QueuePanel scoped 迁移 + 死规则删除 + `.artist` 归 shared）；E4 rename commit `git mv ui/src/style.css ui/src/styles/base.css` + main.ts/测试/本文档引用更新。E4 门禁核验：迁移后 style.css 仅含 `:root` token 块、reset、scrollbar、paper、`.scroll`、`.page-head`（shared page chrome）、`svg`、`.artist`（跨 Feature 共享）、`.list-view`/`.song-row`/`.spinner`、`html.compact`（compact 组含 `.playlists a`/`.lyric-scroll` 留守）、`@media prefers-reduced-motion`——无任何 Feature/shell 专属选择器。legacy tokens 因 §2.3 映射表 dark 组合值不同而全部保留于 base.css（替换须逐皮肤×模式 QA，超出本批次授权）。

1. **E3a 已完成**：§4.4 死 playback 组与 §4.6 经复核的死组（`.btn-*`/`.dim`/`.lyric-container`/`.func*`）已经删除；`.artists*` 因 `.artist` 活跃而撤销删除结论。
2. **需先拆共用**：`.page-head`（3 个调用方跨 feature）、`.recent`（NewsprintHome + QueuePanel）——先决定 owner 再动。
3. **需决策（已解决）**：`.settings-*` 皮肤变体位置——E3b 决议随本体归 settings feature（`features/settings/settings.css`），未进 skins/；`html.lyric-left .lyric-line` 已随 lyric feature 迁入 `features/lyrics/lyrics.css`。
4. **token 合并前置**：§2.3 映射表显示 legacy→semantic 在 dark 组合下普遍值不同，**逐项替换必须按皮肤×模式组合逐一 QA**；E3a 已删除确认无使用的 `--ink-soft-10`、`--glass-*`、`--ease-material`、`--dur-normal`、`--dur-slow`、`--on-accent` 及继发死亡的 `--accent-deep`。Settings 的 dead fallback `var(--border, #ccc)` 已随 E3b 处理（SettingsView.vue 收敛为 `var(--rule)`，commit `3edd059f`）。
5. **cascade 保护**：C1/C3/C6 顺序敏感项迁移时保持 main.ts import 顺序不变，或以特异性替代顺序依赖。
