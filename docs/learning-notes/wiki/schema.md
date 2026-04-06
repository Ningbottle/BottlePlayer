# Wiki Schema

## Domain

C++ 编程语言、Linux 系统编程、数据结构与算法、网络库设计（muduo）、AI/SDK 集成。
这是一个学习导向的知识库，从课程笔记、技术文章、源码分析中编译和提炼知识。

## Conventions

- File names: 小写英文，连字符分隔，无空格（如 `cpp-object-model.md`）
- 每个 wiki 页面必须以 YAML frontmatter 开头（格式见下方）
- 使用 `[[wikilinks]]` 链接其他页面，每页至少 2 个出站链接
- 更新页面时必须 bump `updated` 日期
- 每个新页面必须添加到 `index.md` 对应分区
- 每个操作必须追加到 `log.md`
- **来源标记：** 在综合 3+ 来源的页面中，在段落末尾添加 `^[raw/articles/source-file.md]`
  标记该段落的来源，方便读者回溯。单来源页面仅需 `sources:` frontmatter 即可。

## Frontmatter

```yaml
---
title: Page Title
created: YYYY-MM-DD
updated: YYYY-MM-DD
type: entity | concept | comparison | query
tags: [from taxonomy below]
sources: [raw/articles/source-name.md]
# 可选质量信号：
confidence: high | medium | low    # 论据支撑程度
contested: true                    # 存在未解决的矛盾
contradictions: [other-page-slug]  # 与哪些页面冲突
---
```

`confidence` 和 `contested` 是可选的，但对观点密集或快速变化的主题建议使用。
Lint 会将 `contested: true` 和 `confidence: low` 的页面标记为待审查。

### raw/ Frontmatter

原始资料也需要 frontmatter，方便重新摄入时检测变化：

```yaml
---
source_url: https://example.com/article   # 原始 URL（如有）
ingested: YYYY-MM-DD
sha256: <raw content 的 hex digest>
---
```

`sha256:` 仅计算 frontmatter 之后的正文部分。重新摄入同一 URL 时：
- hash 相同 → 跳过
- hash 不同 → 标记 drift 并更新

## Tag Taxonomy

以下标签已定义。使用新标签前必须先添加到此列表。

### Languages（编程语言）
- `cpp` `c` `stl` `template` `pointer` `memory` `class` `inheritance` `polymorphism` `operator-overloading` `raii` `smart-pointer` `move-semantics`

### Systems（系统编程）
- `linux` `network` `socket` `buffer` `reactor` `muduo` `multiplexing` `epoll` `thread` `mutex` `condition-variable` `event-loop`

### Algorithms（算法与数据结构）
- `data-structure` `sort` `tree` `binary-tree` `stack` `queue` `list` `vector` `string` `recursion` `heap` `hash` `graph` `dynamic-programming` `complexity`

### Meta（元信息）
- `comparison` `learning-path` `interview` `project` `ai-sdk` `llm` `resource` `tool` `framework`

规则：页面上的每个 tag 都必须出现在此分类中。如需新标签，先在此处添加，再使用。

## Page Thresholds

- **创建页面：** 当一个实体/概念在 2+ 来源中出现，或是单个来源的核心内容
- **追加到已有页面：** 当来源提及已覆盖的内容
- **不创建页面：** 对顺带提及、次要细节、或超出 domain 范围的内容
- **拆分页面：** 超过 ~200 行时，按子主题拆分并交叉链接
- **归档页面：** 内容完全过时 → 移至 `_archive/`，从 index 中移除

## Entity Pages

每个重要实体一个页面。包含：
- 概述 / 是什么
- 关键事实和日期
- 与其他实体的关系（`[[wikilinks]]`）
- 来源引用

## Concept Pages

每个概念或主题一个页面。包含：
- 定义 / 解释
- 当前知识状态
- 开放问题或争论
- 相关概念（`[[wikilinks]]`）

## Comparison Pages

并排对比分析。包含：
- 对比什么、为什么对比
- 对比维度（表格格式优先）
- 结论或综合
- 来源

## Update Policy

新信息与现有内容冲突时：
1. 检查日期 — 新来源通常取代旧来源
2. 如确实矛盾，记录双方立场（附日期和来源）
3. 在 frontmatter 标记：`contradictions: [page-name]`
4. 在 lint 报告中标记为待用户审查

## Obsidian Integration

本 wiki 目录可直接作为 Obsidian vault 使用：
- `[[wikilinks]]` 渲染为可点击链接
- Graph View 可视化知识网络
- YAML frontmatter 支持 Dataview 查询
- `raw/assets/` 存放图片，通过 `![[image.png]]` 引用

推荐 Dataview 查询示例：
```dataview
TABLE tags, confidence, updated
FROM "wiki/concepts"
SORT updated DESC
```
