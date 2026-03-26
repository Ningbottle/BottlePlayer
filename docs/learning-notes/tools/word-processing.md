# Word文档处理

## 📋 基本信息
- **技能名称**: word-documents
- **类别**: 文档处理 (productivity)
- **用途**: 读取、创建、格式化 .doc/.docx Word文档
- **标签**: Word, DOCX, DOC, Documents, Chinese, Academic, Formatting

## 🎯 主要功能

### 1. 文档读取
| 格式 | 工具 | 说明 |
|------|------|------|
| .docx (现代格式) | `python-docx` | 解析文档结构 |
| .doc (旧格式) | `catdoc` 或 `antiword` | CLI回退方案 |
| 纯文本提取 | `catdoc file.doc` | 快速，无需Python |
| 文件类型检查 | `file document.doc` | 确认格式 |

### 2. 文档创建
- 从模板创建新文档
- 支持中文学术论文格式
- 自定义样式和格式

### 3. 文档格式化
- 应用预设样式
- 调整字体、段落
- 设置页眉页脚

---

## 💡 使用场景

### 学术论文
```
"帮我创建一个生态毒理学课程论文，格式要严格符合模板要求"
```

### 商业报告
```
"创建一个季度销售报告，包含图表和表格"
```

### 文档转换
```
"把这个Markdown文件转换为Word文档"
```

---

## 🔧 详细技术指南

### 读取 .docx 文件 (现代格式)

```python
from docx import Document
doc = Document('paper.docx')
for p in doc.paragraphs:
    if p.text.strip():
        print(p.text)
```

### 读取 .doc 文件 (旧格式 - Word 97-2003)

`python-docx` **无法**读取 .doc 文件 — 会抛出 `ValueError: not a Word file`。使用 CLI 工具：

```bash
# 先检查文件类型
file document.doc

# 最佳回退: catdoc (通常在 MSYS/Git Bash 中预装)
catdoc document.doc

# 替代方案: antiword
antiword document.doc
```

**⚠️ 重要提示**: 如果 `file` 报告 `Composite Document File V2 Document`，这是真正的 .doc — 不要尝试用 `python-docx` 处理它。

---

## 📝 中文学术论文格式规范

### 字体和字号标准

| 元素 | 字体 | 字号 | 样式 |
|------|------|------|------|
| 封面标题 (生态毒理学课程论文) | 黑体 | 三号 (16pt) | 加粗，居中 |
| 封面字段 (题/学/专/等) | 宋体 | 四号 (14pt) | 常规 |
| 论文标题 | 黑体 | 三号 (16pt) | 加粗，居中 |
| 作者行 | 宋体 | 小四 (12pt) | 居中，逗号分隔 |
| 摘要标题 | 黑体 | 小四 (12pt) | 加粗，顶部对齐，字间空格 |
| 摘要正文 | 宋体/TNR | 小四 (12pt) | 单倍行距 |
| 关键词标题 | 黑体 | 小四 (12pt) | 加粗，顶部对齐 |
| 章节标题 (一、二、…) | 黑体 | 四号 (14pt) | 加粗 |
| 正文段落 | **仿宋** | 小四 (12pt) | 首行缩进2字符，1.5倍行距 |
| 英文文本 | Times New Roman | 同上 | 全文统一 |
| 图表标题 | 黑体+TNR | 五号 (10.5pt) | 加粗(中文)，居中 |
| 参考文献 | 宋体+TNR | 五号 (10.5pt) | 单倍行距 |
| 页边距 | 上下2.54cm，左右3.18cm | — | A4纸 |

### 设置中文字体 (python-docx)

```python
from docx.shared import Pt, Cm
from docx.oxml.ns import qn

def set_font(run, cn_font='宋体', en_font='Times New Roman', size=12, bold=False):
    run.font.size = Pt(size)
    run.font.bold = bold
    run.font.name = en_font
    # 关键：设置东亚字体用于中文字符
    run.element.rPr.rFonts.set(qn('w:eastAsia'), cn_font)
```

**⚠️ 重要**: 没有 `qn('w:eastAsia')`，中文字符会使用默认字体渲染（通常是宋体，但不保证）。必须显式设置。

### 标准页面设置

```python
from docx.shared import Cm

doc = Document()
for section in doc.sections:
    section.top_margin = Cm(2.54)
    section.bottom_margin = Cm(2.54)
    section.left_margin = Cm(3.18)
    section.right_margin = Cm(3.18)
```

### 段落格式化

```python
from docx.enum.text import WD_ALIGN_PARAGRAPH

p = doc.add_paragraph()
p.alignment = WD_ALIGN_PARAGRAPH.CENTER  # 或 LEFT, RIGHT, JUSTIFY
pf = p.paragraph_format
pf.space_before = Pt(6)
pf.space_after = Pt(6)
pf.line_spacing = 1.5  # 或 1.0 为单倍行距
pf.first_line_indent = Cm(0.74)  # 约12pt字体的2个中文字符
```

---

## 🔄 工作流：重新格式化论文到模板

当给定源论文 + 格式模板时：

1. **读取两个文档** — 从源文档提取内容，从模板提取规则
2. **解析模板规则** — 识别每种元素类型的字体/字号/样式
3. **创建新 .docx** — 使用 python-docx，程序化应用规则
4. **保留所有内容** — 摘要、章节、参考文献、图表标题
5. **保存到新文件** — 永远不要覆盖原文件

### 常见模板元素映射

- 封面页 → 标题、作者、学号、院系、日期
- 目录 → 自动生成或手动占位符
- 摘要 / Abstract → 中文 + 英文版本
- 关键词 → 分号分隔
- 章节结构 → 编号章节，统一标题样式
- 参考文献 → 特定引用格式 (作者-年份，编号等)

---

## ⚠️ 重要陷阱

1. **.doc vs .docx**: 选择读取器前必须用 `file` 命令检查。`python-docx` 在 .doc 文件上会静默失败或抛出晦涩错误。

2. **中文字体渲染**: 必须使用 `qn('w:eastAsia')` — 单独用 `run.font.name` 只设置拉丁字体。

3. **编码问题**: 中文 Word 的 .doc 文件可能使用 GBK 编码。`catdoc` 可以处理；`antiword` 可能不行。

4. **大文档**: `python-docx` 将整个文档加载到内存。对于非常大的文件 (>100MB)，考虑流式处理。

5. **模板 .doc 文件**: 一些模板是 .doc (不是 .docx)。用 `catdoc` 读取内容，然后用 python-docx 在 .docx 中重新创建 — 不要尝试原地编辑 .doc。

6. **行距值**: `paragraph_format.line_spacing` 接受浮点数 (如 1.5) 或 Pt 值。中文学术论文通常使用 1.5 倍或固定 20-25pt。

7. **⚠️ 关键 — 字体准确性**: 中文学术模板为每种元素指定**精确**字体。常见混淆：仿宋 vs 宋体。正文通常是**仿宋**（不是宋体）。逐字阅读模板 — 错误字体 = 论文被拒。**不确定时，在写代码前重新阅读原始模板文档。**

8. **封面页布局**: 中文学术封面页有非常具体的布局 — 标题可能需要两行处理，第二行左对齐，特定左缩进，字段间精确间距。不要近似 — 视觉上匹配模板。

9. **用户质量标准**: 学术论文的格式化任务对错误**零容忍**。用户会拒绝不完全匹配模板的输出。交付前对照模板验证输出。

---

## 📚 相关技能
- [[OCR文字识别]] - 扫描件文字提取
- [[PDF编辑处理]] - PDF文档处理
- [[PowerPoint制作]] - PPT演示文稿
- [[Notion管理]] - Notion文档管理

## 🔗 外部资源
- [python-docx 官方文档](https://python-docx.readthedocs.io/)
- [Word格式规范](https://example.com)
- [中文学术论文模板](https://example.com)

---

*最后更新: 2026年6月16日*