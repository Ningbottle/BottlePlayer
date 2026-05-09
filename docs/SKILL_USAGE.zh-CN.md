# BottleMusic 技能调用规则

本项目指定使用以下 skills 作为工作方法。skills 是流程工具，不替代本目录中的项目事实。

## improve-codebase-architecture

用途：

- 梳理模块边界。
- 发现浅模块。
- 评估 Interface 是否过宽。
- 判断复杂性是否集中在正确 Module 内。

使用语言：

- Module
- Interface
- Implementation
- Depth
- Seam
- Adapter
- Leverage
- Locality

使用时机：

- 新增模块前。
- 拆分 `EchoImage`、`EchoAsync`、`EchoDiagnostics` 时。
- 发现 UI、Core、Storage 互相泄漏细节时。
- 大重构前。

产出：

- 候选架构问题列表。
- 每个问题说明 Files、Problem、Solution、Benefits。
- 需要用户选择后再进入具体设计。

## tdd

用途：

- 功能实现。
- bug 修复。
- 行为确认。
- 防止一次性大迁移跑偏。

使用方式：

- 一个行为一个测试。
- RED、GREEN、REFACTOR。
- 测 public Interface，不测私有实现。
- 不进行横向切片。

使用时机：

- 迁移搜索、歌曲 URL、歌词、播放状态机。
- 实现图片缓存和内存上限。
- 实现虚拟列表和 ViewModel。

产出：

- 行为测试。
- 最小实现。
- 重构后的稳定 Interface。

## design-an-interface

用途：

- 设计关键 Interface。
- 比较多个 radically different 方案。
- 避免第一版接口锁死架构。

候选 Interface：

- `IBackendFacade`
- `IPlaybackController`
- `IImageLoader`
- `IImageCache`
- `IEventQueue`
- `IMemorySnapshotProvider`
- `IPage`
- `IRenderContext`

使用方式：

- 至少比较三种方向：最少方法、最灵活、最常见路径优化。
- 比较 Interface simplicity、Depth、Ease of correct use、Implementation efficiency。
- 只设计，不立即实现。

## to-issues

用途：

- 把计划拆成可执行任务。
- 保持纵向切片。
- 区分 HITL 和 AFK。

切片规则：

- 每个任务都应可验证。
- 每个任务应尽量穿过数据、Interface、UI 或测试中的完整路径。
- 不写“只做某层”的横向任务，除非它是明确的基础设施切片。

产出格式：

- Title。
- Type：HITL 或 AFK。
- Blocked by。
- Acceptance criteria。

## 项目内 skill 使用顺序

重新设计阶段：

1. `improve-codebase-architecture`
2. `design-an-interface`
3. `to-issues`

实现阶段：

1. `tdd`
2. `improve-codebase-architecture`
3. `to-issues`

UI 关键 Interface 阶段：

1. `design-an-interface`
2. `tdd`
3. `improve-codebase-architecture`

## 重要规则

- skill 结论必须落回 `docs/`。
- 如果 skill 结论和当前文档冲突，先更新文档或请用户确认。
- 不要把 skill 当作自动实现许可；用户说“先不动手”时只沟通和写文档。
