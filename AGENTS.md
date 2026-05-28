# BottleMusic Agent 工作指南

本文回答「怎么做」。项目事实只读 `docs/PROJECT_LOGIC.zh-CN.md`。

## 1. 项目身份

- BottleMusic 是 **酷狗音乐概念版 PC 非官方客户端**。
- 酷狗音乐概念版没有官网、没有官方 PC 端；本项目目标是在 Windows 上提供非官方 PC 体验。
- 不要混成酷狗标准版、酷狗官方 PC、m 站项目或 Electron 兼容项目。

## 2. 必读顺序

1. `AGENTS.md`：工作流程、skills、约束。
2. `docs/PROJECT_LOGIC.zh-CN.md`：架构、业务链路、酷狗音乐概念版 API 事实。

旧 `CLAUDE.md`、`REFERENCE`、`WORKLIST` 若出现，视为过期资料，不作为事实源。

## 3. 工作规则

- 默认用中文沟通和写项目文档；代码标识符使用英文。
- 用户要求「先不动手」「重新梳理」时，只沟通和整理文档，不改代码。
- 不确定时先查代码和项目逻辑文档，不凭记忆猜。
- 架构讨论使用：Module、Interface、Implementation、Depth、Seam、Adapter、Leverage、Locality。

## 4. 实现前检查

- **Module**：改动属于 UI、CompatServer、EchoCore、Storage、Playback、Image、Async、Diagnostics 哪一块？
- **Interface**：调用方看到的 public Interface 是什么？
- **Concept baseline**：是否仍服务酷狗音乐概念版，而不是误切到标准版、m 站或 MakcRe `platform=lite` 的命名假设？
- **Memory**：是否增加长期驻留对象、缓存或大依赖？
- **Threading**：是否会把网络、SQLite、图片解码、Media Foundation 放到 UI 线程？
- **Test**：是否需要 TDD tracer bullet？

## 5. Skills

| Skill | 使用场景 |
| --- | --- |
| `tdd` | 新功能、修 bug、迁移接口、缓存与内存预算 |
| `diagnose` | 风控、签名、播放、性能等不确定问题 |
| `improve-codebase-architecture` | 模块边界、依赖混乱、深 Module 设计 |
| `to-issues` | 把计划拆成可独立验收的纵向任务 |
| `zoom-out` | 进入陌生模块前建立全局理解 |

Skill 是流程工具，不替代项目事实；结论必须落回 `docs/PROJECT_LOGIC.zh-CN.md` 或测试。

## 6. TDD

- 每次只验证一个用户可观察行为。
- RED：先写失败测试。
- GREEN：最小实现通过。
- REFACTOR：通过后再整理命名和结构。
- 通过 public Interface 测行为，不测私有函数和内部调用顺序。

## 7. 实现约束

### UI

- 前端使用 Tauri 2 + Vue 3 + Vanilla CSS。
- 所有业务请求经 `ui/src/api/backend.ts`，组件不直接访问酷狗接口。
- UI 线程不做网络、SQLite、图片解码、Media Foundation。
- 视觉方向是 Newsprint 报纸风：纸色 `#f1ead8`，红强调 `#a8311b`。

### 后端

- 酷狗细节集中在 EchoCore。
- `dfid`、`mid`、`uuid`、`guid`、`mac`、`token` 不泄漏到 UI。
- 兼容 JSON 与 typed DTO 分开。
- 大响应不长期保留原始 JSON。
- 分页接口不一次性拉取全部历史。
- 酷狗音乐概念版 `appid`、`clientver`、`busi_type`、盐选择、设备指纹血缘必须以 `docs/PROJECT_LOGIC.zh-CN.md` 为准。

### 播放、图片、异步

- EchoPlayback 只暴露状态与命令；切歌释放旧播放对象；不做整曲解码缓存。
- 图片加载归口 EchoImage；请求必须可取消；缓存必须有内存和磁盘上限。
- 后台任务必须可追踪、可取消；性能问题先诊断再改。

## 8. 禁止事项

- 不要把酷狗标准版参数当作酷狗音乐概念版默认事实。
- 不要把 MakcRe 的 `platform=lite` 命名写成 BottleMusic 的业务身份；本项目身份是酷狗音乐概念版。
- 不要把 m.kugou.com Cookie-only GET 当作已确认的 VIP 领取路线。
- 不要把临时 EchoCompatServer HTTP 形态污染最终业务 Interface。
- 不要在 UI 线程做网络、数据库、图片解码或播放底层调用。
- 不要无上限缓存图片、搜索结果、歌单歌曲或歌词对象。
- 不要新增大体积依赖绕过首版问题。
- 不要伪造成功；未迁移或不可绕过接口应返回稳定错误。
- 酷狗 appid / clientver / 盐选择 / busi_type 不得在业务代码里硬编码，必须经由 `GetKuGouProfile()` 派生；唯一豁免是 `KuGouProfile.h/cpp`。
- 不要把 dfid 派生公式无条件当作所有酷狗音乐概念版接口的全局 `mid` 事实；先查 `PROJECT_LOGIC.zh-CN.md` 的设备指纹章节。

## 9. 验证

- 改前端：运行类型检查/构建。
- 改 C++：构建相关 target，必要时跑 smoke tests。
- 改酷狗接口：至少验证请求参数、签名路径、错误码归因。
- 改文档：确认 `AGENTS.md` 只回答「怎么做」，`docs/PROJECT_LOGIC.zh-CN.md` 只沉淀项目事实，二者不互相矛盾。

## 10. 长任务

- 优先纵向切片：数据/API/Interface/UI 或测试贯通。
- 每个任务写清 Type：HITL 或 AFK。
- 上下文压缩只保留 BottleMusic 项目事实，丢弃账号、代理、证书等临时排障信息。
