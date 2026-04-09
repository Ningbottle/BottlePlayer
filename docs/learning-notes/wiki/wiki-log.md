# Wiki Log

> Chronological record of all wiki actions. Append-only.
> Format: `## [YYYY-MM-DD] action | subject`
> Actions: ingest, update, query, lint, create, archive, delete
> When this file exceeds 500 entries, rotate: rename to log-YYYY.md, start fresh.

## [2026-06-18] create | Wiki initialized
- Domain: C++编程、Linux系统编程、数据结构与算法、网络库设计（muduo）、AI/SDK集成
- Structure: SCHEMA.md + index.md + log.md + raw/ + entities/ + concepts/ + comparisons/ + queries/
- Obsidian plugin "Karpathy LLM Wiki" v1.19.0 installed
- Obsidian plugin "Dataview" v0.5.68 installed

## [2026-06-18] ingest | C++ 学习入口（MOC）
- Source: `1.c++学习/00-学习入口.md` → `raw/articles/cpp-learning-roadmap.md`
- Created 5 wiki pages:
  - `concepts/c-language-basics.md` — C 语言基础
  - `concepts/cpp-learning-path.md` — C++ 学习路径 MOC
  - `concepts/cpp-object-model.md` — C++ 对象模型
  - `concepts/data-structures-intro.md` — 数据结构与算法
  - `concepts/stl-containers.md` — STL 容器与模板
  - `entities/muduo-library.md` — muduo 网络库
- Cross-references: 每个页面 ≥2 个 [[wikilinks]]，形成完整知识图谱
- Updated index.md: 6 pages total
