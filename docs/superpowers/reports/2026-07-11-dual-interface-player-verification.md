# BottleMusic 双界面播放器重构 - 验证报告

日期：2026-07-11
状态：自动化回归全部通过；视觉矩阵与性能验证待手动执行

## 1. 自动化回归

### 1.1 Vitest 单元测试

**命令：** `pnpm test --run`

**结果：** 48 test files, 494 tests passed, 0 failures

```
 Test Files  48 passed (48)
      Tests  494 passed (494)
   Duration  20.58s
```

测试覆盖范围：
- homeFeedStore (3 tests) - 首页数据控制器
- viewRegistry (2 tests) - 稳定缓存键
- motion (25 tests) / motionProfiles (9 tests) - 动效系统
- themeStore (5 tests) - 皮肤/模式管理
- webAudioEq (29 tests) / eqWorkletProcessor (25 tests) - EQ 图
- playerBackend (18 tests) - HTML5 Audio 后端
- playerStore (33 tests) - 播放器状态
- playbackOrchestrator (19 tests) - 播放转换编排
- playSessionTracker (13 tests) - 统计会话
- usePlayerControls (28 tests) - 共享播放控制器
- AuroraPlayerBar (12 tests) / NewsprintPlayerBar (13 tests) - 播放器栏
- PlayerProgress (15 tests) - 可访问进度条
- AuroraHome (10 tests) / NewsprintHome (9 tests) - 首页
- Shells (23 tests) - Aurora/Newsprint Shell
- FullscreenWindowControls (7 tests) - 全屏窗口控制
- LyricStages (10 tests) - 歌词舞台
- LyricView (15 tests) - 歌词视图
- HomeView (3 tests) - 首页视图分发
- AppNetworkBanner (6 tests) - 网络横幅
- EqualizerPanel (10 tests) / EqualizerView (1 test) - 均衡器
- StatsView (15 tests) - 统计视图
- HistoryView (4 tests) - 历史视图
- SettingsView (5 tests) - 设置视图
- SkinPrimitives (19 tests) - 皮肤原语
- useLyricFollow (16 tests) - 歌词跟随
- recentPlayedStore (15 tests) - 最近播放
- circuitBreaker (5 tests) - 断路器
- audioProxy (2 tests) - 音频代理
- vipResolver (20 tests) - VIP 解析
- playbackDiagnostics (15 tests) - 播放诊断
- backend (4 tests) - 后端调用
- trackInfo (4 tests) / favorite (9 tests) / userStore (2 tests)
- syncVersion (1 test) / skipVersion (3 tests)
- musicPlayerHtml (1 test) / appInit (1 test) / LoginView (1 test)
- lyricFullscreen (2 tests)

### 1.2 TypeScript 类型检查

**命令：** `pnpm exec vue-tsc --noEmit`

**结果：** 通过，无类型错误

### 1.3 Vite 生产构建

**命令：** `pnpm build`

**结果：** 构建成功

```
vite v6.4.2 building for production...
✓ 190 modules transformed.
dist/index.html                   1.08 kB │ gzip:   0.54 kB
dist/assets/index-CNkJbSFh.css  102.98 kB │ gzip:  17.53 kB
dist/assets/index-Dd8bec-4.js  352.06 kB │ gzip: 125.21 kB
✓ built in 2.89s
```

## 2. 边界检查

**命令：** `git diff --name-only <merge-base>..HEAD`

**结果：** 所有变更仅限于前端 (`ui/src/`) 和 `.gitignore`。无后端文件被修改。

检查清单：
- `ui/src-tauri/` - 无变更
- `native/` - 无变更
- AudioProxy (`audio_proxy.rs`, `audioProxy.ts`) - 无变更
- EQ 数据逻辑 (`webAudioEq.ts`, `eqWorkletProcessor.ts`) - 无变更
- 后端 API (`backend.ts`, `backend_api.rs`) - 无变更
- 构建产物 - 未提交
- 测试截图 - 未提交
- Mineradio 参考图片 - 未提交

变更文件总数：57 个，全部在 `ui/src/` 和 `.gitignore` 内。

## 3. 视觉矩阵验证

**状态：待手动验证**

视觉矩阵（Step 2）需要在运行的应用中通过浏览器截图检查以下组合：

| 皮肤 | 模式 | 页面 | 尺寸 |
|---|---|---|---|
| Aurora | light/dark | 首页、普通歌词、全屏歌词 | 1280x720、1440x900、1920x1080 |
| Newsprint | light/dark | 首页、普通歌词、全屏歌词 | 1280x720、1440x900、1920x1080 |

每组还需覆盖：正常数据、初始加载、刷新中、单 section 错误、长歌名、无封面、0/50/100% 进度。

验收要点：
- 无顶部装饰横线
- 进度轨道、fill、thumb 和时间在四套组合中可见
- 首页播放器不再大片空荡；Aurora 与 Newsprint 一眼可区分
- 返回当前行按钮不覆盖歌词
- 全屏只显示最小化和退出全屏
- 1280px 下按钮不拥挤、不裁切、不覆盖

**本环境无法运行浏览器，此项需手动执行。**

## 4. 首页网络与组件生命周期验证

**状态：待手动验证**

需要在浏览器中验证：
- 首次首页三组请求各 1 次
- 两次返回首页新增首页请求为 0
- HomeView mount 总数为 1
- 数据与滚动位置立即恢复，无先空后填
- 显式刷新只发起一轮并发请求；请求期间旧内容保持

单元测试已覆盖：
- homeFeedStore: 首次加载并发请求、返回首页不重新请求、显式刷新保留旧数据
- viewRegistry: 稳定缓存键保证 KeepAlive 有效
- HomeView: section 错误隔离

## 5. 动效与性能验证

**状态：待手动验证**

需要在浏览器中验证：
- Aurora：主舞台、播放按钮和 progress thumb 有低强度果冻反馈；正文、歌词和列表无弹性晃动
- Newsprint：区块与列表以短距离、短 stagger 落版；稳定后无循环动画
- 快速连续切换页面 10 次，同一元素不存在叠加 tween；离开视图后 tween 已 kill
- `prefers-reduced-motion` 下立即到最终态
- 切换窗口失焦/隐藏标签页时 Aurora ambient pause，恢复后仅在播放中继续
- Performance 录制中页面切换和歌词跟随无持续长任务

单元测试已覆盖：
- motionProfiles: Aurora/Newsprint profile 参数验证
- motion: animateElement/animateStagger/startAmbientMotion 返回可取消 handle
- motion: killTweensOf 在快速导航时防止叠加 tween

## 6. 已知非阻塞限制

1. **视觉矩阵与性能录制待手动验证** - 本环境无法运行浏览器，Step 2-4 需在有桌面环境的机器上执行。
2. **HTMLMediaElement mock 限制** - Vitest 环境中 `play()`/`pause()`/`load()` 返回 "Not implemented" 警告，这是 jsdom mock 的已知行为，不影响测试结果。
3. **onScopeDispose 警告** - useLyricFollow 测试中 `onScopeDispose()` 在无活跃 effect scope 时发出 Vue 警告，不影响测试通过。
4. **backend.test.ts 慢测试** - "times out slow invoke and retry succeeds" 测试耗时 ~14.5s（模拟网络超时），不影响 CI 通过。

## 7. 任务完成记录

| 任务 | 提交 | 说明 |
|---|---|---|
| Task 0 (规划) | 558a3c18 | docs: plan dual-interface player redesign |
| Task 0 (设计) | 590f96e0 | docs: redesign dual-interface player experience |
| Task 1 | d0f6127c | fix: preserve home feed across navigation |
| Task 1 | c2b7b7a0 | refactor: keep home view identity stable |
| Task 2 | (包含在 Task 1 中) | viewRegistry 稳定缓存键 + KeepAlive |
| Task 3 | 8fe3002e | feat: add accessible progress and four light modes |
| Task 3 | 3140a8f4 | fix: resolve progress buffered variable collision and add test |
| Task 4 | e8aad8cd | feat: define distinct skin motion profiles |
| Task 5 | 143c4edf | feat: introduce independent aurora and newsprint shells |
| Task 6 | 124e05cb | feat: build distinct home experiences |
| Task 6 | 0af1b81b | fix: add skin crossfade, reset cover error, define skin CSS vars |
| Task 7 | 0593dbe5 | feat: redesign both player consoles |
| Task 7 | 15e03823 | fix: valid CSS easing, restore lyric button active state |
| Task 8 | a659a915 | feat: rebuild lyric stages and fullscreen controls |
| Task 8 | e9a9b1d9 | fix: three-row lyric layout, independent fullscreen template, queue padding |
| Task 9 | 9c117586 | refactor: apply skin primitives to secondary views |
| Task 9 | 42c0d2ce | fix: valid HTML nesting, SkinButton active prop, bilingual subtitles, semantic color |

## 8. 结论

自动化回归全部通过：
- Vitest: 494 tests passed (48 test files)
- vue-tsc --noEmit: 无类型错误
- Vite build: 构建成功 (190 modules, 2.89s)
- 边界检查: 无后端文件被修改

视觉矩阵（Step 2）、首页网络与组件生命周期（Step 3）和动效与性能验证（Step 4）需要运行的应用和浏览器环境，标记为待手动验证。相关单元测试已覆盖核心逻辑。
