# 维护手册

> 本文档记录 BottleMusic 的已知问题、模糊点、清理候选、未来提案。
> 基于 [evidence-report.md](./evidence-report.md) 的事实。
> 明确区分**当前实现**、**已知风险**、**未来提案**。

## 已知问题

> 来源:[CONTEXT.md § Known Issues](../../CONTEXT.md) + 代码核验。截至 `22ba7951` 基线。

### 1. MFS 原生播放损坏(已弃用,不修)

- **症状**:Media Foundation 原生播放拓扑解析失败,退出时死锁
- **状态**:已禁用,降级到 HTML5 后端
- **决策**:**不会修复** — MFS 路径已弃用(2026-07-17 commit `refactor(native): remove MF playback stack and BackendFacade`)
- **影响代码**:`native/playback/` 目录已删除;`EchoPlayback*` 已从 native/ 移除;`BackendFacade` 已移除;`ui/src-tauri/src/playback.rs` 已删除;`lib.rs` mod 声明无 playback
- **文档同步**:`CONTEXT.md` L119、L124、L127-128 仍描述已删除的代码,本轮会修正

### 2. EQ 对 KuGou CDN 媒体(已解决 ✅)

- **症状**:KuGou CDN 不发 CORS 头,Web Audio API 无法挂载 EQ graph
- **解决**:`audio_proxy.rs`(loopback 127.0.0.1)用 CORS 头 + range/resume 重发 CDN 媒体
- **降级**:代理不可用时显示降级提示(`eqState.available` 暴露到 UI)
- **关键不变量**:EQ 拓扑 `captureStream → MediaStreamSource → AudioWorkletNode → GainNode → destination`,**绝不** `createMediaElementSource`

### 3. `Music Player.html` 重写(0bedf68)

- **背景**:spec 要求 line 673 一行语法修复,但 commit `0bedf68` 做了完整重写(格式化 + 死代码删除)
- **状态**:文件被仓库追踪,**不是** v2 源文件,但随应用一起分发(?需核验 — 见 [evidence-report.md § 8 UNKNOWN](./evidence-report.md#8-unknown-项证据不足不删不改))
- **遗留**:无后续动作,仅作记录

### 4. PR review `0bedf68..ce5233c` 延期小项

#### 4.1 EQ 重复 `initPlayer` 重初始化顺序

- **现状**:重复 `initPlayer` 时 EQ 重初始化顺序问题
- **当前无害**:EQ 始终因 CORS 被禁用(在 audio_proxy 之前)
- **风险**:audio_proxy 启用后可能暴露
- **状态**:延期

#### 4.2 `onEnded` phase guard

- **现状**:phase guard 防御性代码
- **理论不可能触发**:transitionSeq supersede 保证旧 onEnded 不会执行
- **状态**:保留作防御,延期

#### 4.3 DeepSeek API URL `/v1` 前缀

- **现状**:`ai_analysis.rs` `DEEPSEEK_API_URL = "https://api.deepseek.com/chat/completions"` 缺 `/v1` 前缀
- **当前可用**:DeepSeek 端点对带/不带 `/v1` 都响应
- **状态**:spec 偏差,延期

## 模糊点 / 易误解处

> 这些是本 Wiki 编写过程中发现的**事实层面不一致或描述模糊**处。

### 1. CHANGELOG.md 严重滞后

- **事实**:[CHANGELOG.md](../../CHANGELOG.md) 15 个条目,日期范围 2026-02-03 至 2026-05-22
- **git 实际**:首次提交 2026-02-02,最新提交 2026-07-22
- **不一致**:CHANGELOG 停在 v1.0.0(06-04)之前,后续 2 个月未更新
- **处理**:不手工补;输出 [release-tooling-rfc.md](../release-tooling-rfc.md) 评估自动化方案

### 2. 测试计数时间差

- **CONTEXT.md**(2026-07-03 基线):C++ 11, Rust 22, Frontend 98, Total 131
- **evidence-report.md**(2026-07-23 基线):C++ 11, Rust 34 个 #[test], Frontend 78 个 .test.ts 文件
- **不一致**:数字差异大
- **解释**:两者非冲突 — 前者是 07-03 时的总数,后者是 07-23 时按文件/#[test] 计数;前端用例数远多于文件数
- **处理**:Wiki 不写死测试数量为长期事实,只放在 [evidence-report.md](./evidence-report.md) 和 [testing-and-release.md](./testing-and-release.md) 的生成报告中

### 3. DeepSeek Key 存储描述不一致

- **CONTEXT.md** L98:`User provides API key via localStorage deepseek_api_key`
- **PRIVACY.md**(2026-07-17):`API Key 仅当前页面会话,不写入磁盘或 localStorage`
- **代码事实**:`StatsView.vue:53` `localStorage.removeItem('deepseek_api_key')`(清理旧数据);`aiApiKey = ref('')`(内存)
- **处理**:本轮修正 CONTEXT.md L98,以 PRIVACY.md 为准

### 4. 作者署名不一致

- **LICENSE**:版权 `hoowhoami`
- **Cargo.toml / package.json / tauri.conf.json**:`Ningbottle`
- **GitHub 仓库**:`Ningbottle/BottlePlayer`
- **处理**:**不修改 LICENSE**(约束一.6),理解为人不同 ID

### 5. 仓库名 vs 产品名

- **仓库名**:`BottlePlayer`(GitHub URL `Ningbottle/BottlePlayer`)
- **产品名**:`BottleMusic`(README、tauri.conf.json、package.json)
- **处理**:理解 `BottlePlayer` 是 GitHub 仓库名,`BottleMusic` 是产品/应用名,同一物

### 6. docs/ gitignore 策略

- **现状**:`.gitignore:110` `docs/` 整目录被忽略
- **影响**:Wiki、ADR 等长期文档无法常规追踪
- **处理**:本轮调整 gitignore,允许 `docs/wiki/`、`docs/adr/` 追踪,保持 `docs/superpowers/`、`docs/captures/` 忽略

### 7. README "Media Foundation" 描述过时

- **README.md** L98、L126 仍提 "Media Foundation 接口"
- **事实**:MF 已于 2026-07-17 移除
- **处理**:本轮修正 README 这两行

### 8. SECURITY.md 链接核对

- **PRIVACY.md** 引用 `[SECURITY.md](./SECURITY.md)`
- **事实**:[SECURITY.md](../../SECURITY.md) 存在于仓库根目录(2026-07-17 由 commit `80a423ea` 引入),PRIVACY.md 的链接**非悬空**,指向有效文件
- **处理**:本轮核对确认链接有效;README.md 原本也指向 `./SECURITY.md`(有效),本轮保留该指向

### 9. EchoImage 挂载状态

- **现状**:`EchoImage` 库代码完整(MemoryImageCache + DiskImageCache + WIC ImageLoader),有测试,但**未挂载主链路**
- **意图**:UNKNOWN — 无 ADR 说明是否计划挂载
- **处理**:不删除,在 [native-cpp.md](./native-cpp.md) 标注"预留功能"

## 清理候选

> 每项清理需先有证据,再独立 commit + 构建测试。恢复方式:`git revert <commit>`。

### 1. vcpkg.json 死依赖(httplib / spdlog / wil)

- **证据**:[evidence-report.md § 2](./evidence-report.md#2-无引用依赖证据httplib--spdlog--wil)
- **现状**:`vcpkg.json` 声明 `cpp-httplib`、`spdlog`、`wil`,但 `CMakeLists.txt` `find_package QUIET` 后无 `target_link_libraries`,源码零 `#include`
- **影响**:浪费 CI/本地 vcpkg install 时间 + 磁盘空间
- **清理步骤**:
  1. 删 `vcpkg.json` 中 `cpp-httplib`、`spdlog`、`wil` 三行
  2. 删 `CMakeLists.txt` L24-26 三个 `find_package`
  3. 独立 commit
  4. 跑 `ctest --preset bottlemusic-check` 验证
- **状态**:本轮实施

### 2. CHANGELOG 自动化

- **现状**:纯手工,滞后 2 个月
- **清理步骤**:见 [release-tooling-rfc.md](../release-tooling-rfc.md)
- **状态**:本轮输出 RFC,不直接安装工具

### 3. server/ 子模块策略

- **现状**:git submodule 指向 `MakcRe/KuGouMusicApi`,不进生产链路
- **清理步骤**:见 [server-strategy-rfc.md](../server-strategy-rfc.md)
- **状态**:本轮输出 RFC,不改变 server/ 存储方式(约束四.6)

### 4. UI 文档残留(不在本轮范围)

- `ui/design-qa.md`、`ui/test_url.cjs`、`ui/test_url.js` 看起来是开发过程残留
- **状态**:不本轮处理(未明确列入清理候选,需进一步证据)

## 未来提案(高风险,只输出 RFC,不修改生产实现)

### 1. Pinia 化状态管理

- **现状**:模块级 `reactive`/`ref` 单例 + `window.__bottlemusic_audio__` HMR 共享
- **提案**:迁移到 Pinia,获得 devtools、SSR 兼容、更可测的 store 单元测试
- **代价**:HMR 共享引用机制需重设计
- **状态**:本轮不实施,记录为 P2 后续路线

### 2. 三层 deadline 简化

- **现状**:Rust `deadline_for_path` → C++ `RequestScheduler` → `HttpClient` watchdog 三层冗余
- **提案**:测量哪一层实际起作用,合并为单层 deadline + 优先级队列
- **状态**:本轮不实施,记录为 P2 后续路线

### 3. C++ 测试框架迁移 gtest

- **现状**:用 `assert` 宏 + CMake 强制 `/UNDEBUG`
- **提案**:迁移到 Catch2 或 gtest,获得更好失败信息和参数化测试,移除 `/UNDEBUG` 反常编译开关
- **状态**:本轮不实施,记录为 P2 后续路线

### 4. 路由表化进一步集中

- **现状**:`CompatApi::GetRouteTable()` 已实现路由表,但 7 个 compat_routes 仍分散在独立文件
- **提案**:进一步集中路由声明,CI 生成路由清单对比 server/
- **状态**:本轮不实施,记录为 P3 后续路线

### 5. 一键清除本地数据按钮

- **现状**:[PRIVACY.md](../../PRIVACY.md) 说明"应用当前没有承诺提供一个'一键清除所有本地数据'的按钮"
- **提案**:在 SettingsView 加按钮,调用 Tauri 命令清理 AppData
- **状态**:本轮不实施,记录为 P3 后续路线

### 6. Playwright E2E 烟测

- **现状**:Playwright 仅用于设计 QA 截图,非 E2E
- **阻塞条件**:需安装浏览器二进制(~300MB)+ Tauri 应用需打包后才能 E2E
- **提案**:等 Tauri E2E 工具链(tauri-driver / webdriver-io)成熟后启用
- **状态**:本轮不实施,记录为 P3 后续路线

## P0/P1/P2/P3 后续路线

| 优先级 | 项 | 类型 |
|---|---|---|
| **P0** | httplib/spdlog/wil 死依赖清理 | 本轮实施 |
| **P0** | gitignore 调整(允许 docs/wiki/、docs/adr/) | 本轮实施 |
| **P0** | CONTRIBUTING.md 添加 | 本轮实施 |
| **P0** | CONTEXT.md / README.md 过时描述同步 | 本轮实施 |
| **P0** | ADR(FFI 边界、Storage Actor、HMR audio) | 本轮实施 |
| **P1** | release-tooling-rfc.md | 本轮输出 RFC |
| **P1** | server-strategy-rfc.md | 本轮输出 RFC |
| **P1** | 覆盖率基线建立 | 本轮调研 |
| **P2** | Pinia 化状态管理 | 未来 RFC |
| **P2** | 三层 deadline 简化 | 未来 RFC |
| **P2** | C++ 测试框架迁移 gtest | 未来 RFC |
| **P3** | 路由表化集中 | 未来 RFC |
| **P3** | 一键清除本地数据按钮 | 未来 RFC |
| **P3** | Playwright E2E 烟测 | 未来 RFC |
