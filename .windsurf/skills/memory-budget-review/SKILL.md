---
name: memory-budget-review
description: 内存预算审查。防止新增大依赖、无上限缓存、长期驻留对象。适用于 EchoImage、搜索结果、歌单、歌词等模块。
---

## 何时使用
新增或修改以下功能时：
- 图片加载/缓存（`EchoImage` 模块）
- 搜索结果缓存
- 歌单歌曲列表缓存
- 歌词对象缓存
- 播放缓冲/解码缓存
- 引入新依赖库

## 审查清单

### [ ] 缓存上限
- [ ] 图片缓存有内存上限（如 50MB）和磁盘上限（如 200MB）
- [ ] 搜索结果不长期驻留，有 LRU 淘汰策略
- [ ] 歌单歌曲列表分页加载，不一次性缓存全部历史
- [ ] 歌词对象按歌曲 ID 缓存，有数量上限

### [ ] 对象生命周期
- [ ] 播放对象在切歌时释放旧实例（不累积）
- [ ] 大响应 JSON 解析后不长期保留原始字符串
- [ ] 后台任务（`TaskScheduler`、`EventQueue`）可追踪、可取消

### [ ] 依赖体积
- [ ] 新增依赖体积 < 1MB（压缩后）
- [ ] 不引入替代已有功能的重复库
- [ ] 优先用系统 API（Windows Media Foundation）而非第三方解码库

### [ ] 诊断能力
- [ ] 内存变化可通过 `native/diagnostics/MemorySnapshot.cpp` 观测
- [ ] 缓存命中/未命中有日志或计数器

## 参考
- `docs/PROJECT_LOGIC.zh-CN.md` 第 7 条（播放、图片、异步约束）
- `native/diagnostics/MemorySnapshot.cpp`
- `native/async/TaskScheduler.cpp`

## 禁止事项
- 无上限缓存图片、搜索结果、歌单歌曲或歌词对象
- 新增大体积依赖绕过首版问题
- 播放链路做整曲解码缓存
