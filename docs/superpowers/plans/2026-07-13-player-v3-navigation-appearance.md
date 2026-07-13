# BottleMusic V3 Navigation And Appearance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox state and must be checked only after the stated verification passes.

**Goal:** 用正式路由、页面恢复边界和单一外观状态替换 App.vue 的手工历史栈、残余样式清理补丁和失效 Drawer。

**Architecture:** Vue Router 4 成为页面位置与历史的唯一来源；navigationLifecycle 统一歌词全屏退出和过渡清理；PageRecoveryBoundary 只保护中心内容；appearanceStore 统一合法设置与 DOM 同步，themeStore 保留薄兼容层直到所有调用迁移完成。

**Tech Stack:** Vue 3.5、Vue Router 4、TypeScript、Vitest、Vue Test Utils、GSAP。

**Prerequisite:** 完成 2026-07-13-player-v3-stability-core.md 并通过阶段回归。

**Command working directory:** pnpm、测试、类型检查和构建命令在 ui/ 目录运行；git add/commit 在仓库根目录运行。

**Strict RED rule:** 缺少 vue-router、模块不存在或测试无法收集不算 RED。先安装声明依赖并提供最小可编译接口骨架，再以具体行为断言失败作为 RED 证据。

---

## Task 1: 路由合同 RED 与依赖安装

**Recommended agent:** Luna，高思考；依赖与基础测试是机械性工作。

**Files:**
- Modify: ui/package.json
- Modify: ui/pnpm-lock.yaml
- Create: ui/src/navigation/routes.ts
- Create: ui/src/navigation/router.ts
- Create: ui/src/navigation/__tests__/router.test.ts
- Modify: ui/src/views/__tests__/appInit.test.ts
- Modify: ui/src/api/__tests__/lyricFullscreen.test.ts

- [ ] **Step 1: 安装唯一必要的路由依赖**

~~~powershell
pnpm add vue-router@^4
~~~

不得在此任务安装图标库。

- [ ] **Step 2: 添加最小可编译导航骨架并写路由合同测试**

骨架只导出测试需要的类型和空 route records，不实现导航生命周期；它与测试一起属于 RED 提交准备。

覆盖：
- home/search/playlist/lyric 的 name、params/query 与组件映射。
- 从 lyric 导航到任意非 lyric route 后 lyricFullscreen 立即为 false。
- back/forward 不维护第二套 history stack。
- 重复导航不会抛错或留下 pending transition。
- 只有 home route 的 meta.keepAlive=true；lyric/search/playlist 离开后卸载，返回 home 复用同一缓存实例。

- [ ] **Step 3: 运行 RED**

~~~powershell
pnpm test -- src/navigation/__tests__/router.test.ts src/views/__tests__/appInit.test.ts src/api/__tests__/lyricFullscreen.test.ts
~~~

Expected: 测试可以收集；具体 route/fullscreen/history 行为断言失败。模块不存在、import error 或类型错误不接受为 RED。

- [ ] **Step 4: 提交 RED、依赖与编译骨架**

~~~powershell
git add ui/package.json ui/pnpm-lock.yaml ui/src/navigation/routes.ts ui/src/navigation/router.ts ui/src/navigation/__tests__/router.test.ts ui/src/views/__tests__/appInit.test.ts ui/src/api/__tests__/lyricFullscreen.test.ts
git commit -m "test(ui): define router and lyric lifecycle contracts"
~~~

## Task 2: 建立正式导航模块

**Recommended agent:** Terra，高思考。

**Files:**
- Modify: ui/src/navigation/routes.ts
- Modify: ui/src/navigation/router.ts
- Create: ui/src/navigation/navigationLifecycle.ts
- Modify: ui/src/main.ts
- Modify: ui/src/App.vue
- Modify: ui/src/components/Sidebar.vue
- Modify: ui/src/components/Topbar.vue
- Modify: ui/src/components/PlayerBar.vue
- Delete: ui/src/api/viewRegistry.ts
- Delete: ui/src/api/__tests__/viewRegistry.test.ts
- Modify: ui/src/navigation/__tests__/router.test.ts
- Modify: ui/src/views/__tests__/appInit.test.ts

- [ ] **Step 1: 定义 typed route records**

~~~ts
export const routeNames = {
  home: 'home',
  stats: 'stats',
  history: 'history',
  equalizer: 'equalizer',
  settings: 'settings',
  search: 'search',
  playlist: 'playlist',
  lyric: 'lyric',
  login: 'login',
} as const;
~~~

search 使用 query.q；playlist 使用 params.id 和 query.name。组件 props 从 route 映射，不在 App.vue switch。

- [ ] **Step 2: 统一导航生命周期**

~~~ts
export function installNavigationLifecycle(router: Router) {
  router.beforeEach((to, from) => {
    if (from.name === 'lyric' && to.name !== 'lyric') {
      setLyricFullscreen(false);
    }
    cancelPageTransition();
    clearPageTransitionStyles();
  });
}
~~~

clear 只处理 transition session 标记过的节点，不再全局扫描猜测 selector。

- [ ] **Step 3: 改 App.vue 为 RouterView**

使用 RouterView slot + route meta 控制 KeepAlive 和皮肤过渡。只有 home route 设置 `meta: { keepAlive: true }`；search、playlist、lyric 和其余页面均为 false/缺省，离开后卸载。路由测试必须断言 home 返回时保留实例/缓存内容，lyric/search/playlist 离开后触发 unmount。删除 currentView、searchQuery 镜像、playlistId、playlistName、historyStack、historyIndex、pushHistory/applyHistoryEntry。

- [ ] **Step 4: 把事件导航迁移为 router 命令**

Sidebar/Topbar/PlayerBar 可以先保留 emit API，但 App adapter 必须调用 router.push；随后删除不再使用的 activeView 镜像 prop。导航按钮的 active 状态来自 useRoute()。

- [ ] **Step 5: 将 viewRegistry 降为兼容层并最终删除**

先让 router tests 证明所有 route records、cache key 和参数可解析，再删除 viewRegistry.ts 与对应旧测试。不得同时改页面 UI。

- [ ] **Step 6: 运行 GREEN**

~~~powershell
pnpm test -- src/navigation/__tests__/router.test.ts src/views/__tests__/appInit.test.ts src/components/__tests__/Sidebar.test.ts src/components/__tests__/Topbar.skin.test.ts
pnpm exec vue-tsc --noEmit
~~~

- [ ] **Step 7: 提交**

~~~powershell
git add ui/src/navigation/routes.ts ui/src/navigation/router.ts ui/src/navigation/navigationLifecycle.ts ui/src/navigation/__tests__/router.test.ts ui/src/main.ts ui/src/App.vue ui/src/components/Sidebar.vue ui/src/components/Topbar.vue ui/src/components/PlayerBar.vue ui/src/views/__tests__/appInit.test.ts
git add -u -- ui/src/api/viewRegistry.ts ui/src/api/__tests__/viewRegistry.test.ts
git commit -m "refactor(ui): make router the navigation source of truth"
~~~

## Task 3: 页面恢复边界

**Recommended agent:** Terra，高思考。

**Files:**
- Create: ui/src/components/shell/PageRecoveryBoundary.vue
- Create: ui/src/components/shell/__tests__/PageRecoveryBoundary.test.ts
- Modify: ui/src/App.vue
- Modify: ui/src/api/transitionSession.ts
- Modify: ui/src/api/__tests__/transitionSession.test.ts
- Modify: ui/src/style.css

- [ ] **Step 1: 添加最小可编译 boundary 骨架并写 RED 组件测试**

骨架只透传 slot，不捕获错误。构造会在 setup/render 抛错的 route child，断言：
- shell/player slot 仍存在。
- 中心显示中文错误。
- 错误时 lyricFullscreen=false。
- 返回首页调用 router.replace({ name: 'home' })。
- 重试增加 retry key 并重建 child。
- transition session 被 cancel 且样式恢复。

~~~powershell
pnpm test -- src/components/shell/__tests__/PageRecoveryBoundary.test.ts
~~~

Expected: 测试可收集；因未捕获错误、无恢复按钮、未退出 fullscreen 和未清 transition 而失败。

- [ ] **Step 2: 实现 boundary**

使用 onErrorCaptured 捕获中心后代错误；不要捕获 shell/player 自身。错误日志包含 route.fullPath、skin、Error。

~~~ts
onErrorCaptured((error) => {
  failure.value = normalizePageFailure(error, route.fullPath);
  setLyricFullscreen(false);
  cancelPageTransition();
  return false;
});
~~~

- [ ] **Step 3: 为两套皮肤提供语义一致的错误状态**

同一组件通过 data-skin/CSS token 呈现，不创建嵌套卡片。按钮是“返回首页”和“重试当前页面”，均可键盘操作。

- [ ] **Step 4: 运行 GREEN**

~~~powershell
pnpm test -- src/components/shell/__tests__/PageRecoveryBoundary.test.ts src/api/__tests__/transitionSession.test.ts src/views/__tests__/appInit.test.ts
~~~

- [ ] **Step 5: 提交**

~~~powershell
git add ui/src/components/shell/PageRecoveryBoundary.vue ui/src/components/shell/__tests__/PageRecoveryBoundary.test.ts ui/src/App.vue ui/src/api/transitionSession.ts ui/src/api/__tests__/transitionSession.test.ts ui/src/style.css
git commit -m "feat(ui): recover failed pages without losing the player shell"
~~~

## Task 4: 外观状态 RED 与单一 store

**Recommended agent:** Terra，高思考。

**Files:**
- Create: ui/src/api/appearanceStore.ts
- Create: ui/src/api/__tests__/appearanceStore.test.ts
- Modify: ui/src/api/themeStore.ts
- Modify: ui/src/api/__tests__/themeStore.test.ts
- Modify: ui/src/views/SettingsView.vue
- Modify: ui/src/views/__tests__/SettingsView.test.ts

- [ ] **Step 1: 写外观合同 RED**

先创建只返回默认值的最小可编译 appearanceStore 骨架；不得先实现 localStorage 校验或 DOM 同步。

设置模型：

~~~ts
export interface AppearanceSettings {
  skin: 'aurora' | 'newsprint';
  mode: 'light' | 'dark';
  accent: string;
  compactList: boolean;
  lyricAlign: 'left' | 'center';
}
~~~

测试非法 localStorage 值回退；init 幂等；每项设置同步 dataset/CSS variable；皮肤名称显示“极光 Aurora”“报刊 Newsprint”；没有字体、背景、暖度、模糊、颗粒、假缓存控制。

~~~powershell
pnpm test -- src/api/__tests__/appearanceStore.test.ts
~~~

Expected: 测试可收集；默认值可以通过，但非法存储回退、持久化和 DOM 同步等具体行为断言失败。

- [ ] **Step 2: 实现 appearanceStore**

集中定义 storage key、默认值、校验和 applyToDom。themeStore 暂时只转发 skinId/mode，避免一次性破坏所有消费者。

- [ ] **Step 3: Settings 外观分组接入**

保留皮肤、光感、强调色、紧凑列表、歌词对齐。功能文案中文；装饰英文只作为次级文字。

- [ ] **Step 4: 运行 GREEN**

~~~powershell
pnpm test -- src/api/__tests__/appearanceStore.test.ts src/api/__tests__/themeStore.test.ts src/views/__tests__/SettingsView.test.ts
~~~

- [ ] **Step 5: 提交**

~~~powershell
git add ui/src/api/appearanceStore.ts ui/src/api/__tests__/appearanceStore.test.ts ui/src/api/themeStore.ts ui/src/api/__tests__/themeStore.test.ts ui/src/views/SettingsView.vue ui/src/views/__tests__/SettingsView.test.ts
git commit -m "refactor(ui): centralize validated appearance settings"
~~~

## Task 5: 移除 Drawer 与假缓存

**Recommended agent:** Luna，高思考；删除范围必须由引用证据约束。

**Files:**
- Delete: ui/src/components/Drawer.vue
- Delete: ui/src/components/__tests__/Drawer.test.ts
- Modify: ui/src/App.vue
- Modify: ui/src/components/Topbar.vue
- Modify: ui/src/components/__tests__/Topbar.skin.test.ts
- Modify: ui/src/views/SettingsView.vue
- Modify: ui/src/views/__tests__/SettingsView.test.ts
- Modify: ui/src/style.css
- Modify: ui/src/styles/skins/aurora.css
- Modify: ui/src/styles/skins/newsprint.css

- [ ] **Step 1: 记录删除前引用证据**

~~~powershell
rg -n "Drawer|toggle-tweaks|tweaksCollapsed|tweak_(warmth|blur|grain|bg|font)|clearCache|确认清理" ui/src
~~~

- [ ] **Step 2: 删除 Drawer 入口与无效状态**

删除 App extras 中 Drawer、Topbar 的 toggle-tweaks、Settings 假缓存 modal。不得删除 QueuePanel。

- [ ] **Step 3: 删除只服务失效功能的 CSS 变量和规则**

仅删除有引用证据的 background/warmth/blur/grain/font 规则；保留 skin/mode/accent/compact/lyric alignment。

- [ ] **Step 4: 证明失效功能不再存在**

~~~powershell
rg -n "Drawer|toggle-tweaks|tweaksCollapsed|tweak_(warmth|blur|grain|bg|font)|clearCache|确认清理" ui/src
~~~

Expected: 无生产命中；测试文字只可出现在“not present”断言中。

- [ ] **Step 5: 运行回归并提交**

~~~powershell
pnpm test -- src/views/__tests__/SettingsView.test.ts src/components/__tests__/Topbar.skin.test.ts src/views/__tests__/appInit.test.ts
pnpm exec vue-tsc --noEmit
git add ui/src/App.vue ui/src/components/Topbar.vue ui/src/components/__tests__/Topbar.skin.test.ts ui/src/views/SettingsView.vue ui/src/views/__tests__/SettingsView.test.ts ui/src/style.css ui/src/styles/skins/aurora.css ui/src/styles/skins/newsprint.css
git add -u ui/src/components/Drawer.vue ui/src/components/__tests__/Drawer.test.ts
git commit -m "refactor(ui): retire the disconnected appearance drawer"
~~~

## Task 6: 导航与外观阶段回归

**Recommended agent:** Luna，高思考；只验证并报告。

- [ ] **Step 1: 运行全量前端测试**

~~~powershell
pnpm test
~~~

- [ ] **Step 2: 类型与构建**

~~~powershell
pnpm exec vue-tsc --noEmit
pnpm build
~~~

- [ ] **Step 3: 主代理审阅**

重点检查：无双历史栈、离开歌词一次退出全屏、错误只替换中心区、Drawer/假缓存已消失、layoutDemo/nativeBackend/QueuePanel 未误删。任何修正回到对应任务代理并重跑该任务测试。
