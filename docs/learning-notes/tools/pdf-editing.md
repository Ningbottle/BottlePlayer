# PDF编辑处理

## 📋 基本信息
- **技能名称**: nano-pdf + ocr-and-documents
- **类别**: 文档处理 (productivity)
- **用途**: PDF文本提取、编辑、OCR识别

## 🎯 主要功能

### 1. PDF文本编辑 (nano-pdf)
- 自然语言指令编辑PDF
- 修正错别字、标题
- 更新日期、内容

### 2. PDF文本提取 (ocr-and-documents)
- 从PDF提取文本
- 扫描件OCR识别
- 表格、公式提取

### 3. PDF操作
- 分割、合并PDF
- 搜索文本内容
- 提取图片

---

## 💡 使用场景

### 编辑PDF内容
```
"把PDF第1页的标题改成'Q3报告'，修正副标题中的错别字"
```

### 提取PDF文本
```
"提取这个PDF文档的所有文本内容"
```

### 扫描件OCR
```
"识别这个扫描版PDF中的文字"
```

### 分割合并
```
"把PDF的前5页提取出来"
"合并这三个PDF文件"
```

---

## 🔧 详细技术指南

### 一、PDF文本编辑 (nano-pdf)

#### 安装
```bash
# 使用 uv (推荐 — Hermes中已可用)
uv pip install nano-pdf

# 或使用 pip
pip install nano-pdf
```

#### 基本用法
```bash
nano-pdf edit <file.pdf> <page_number> "<instruction>"
```

#### 示例
```bash
# 修改第1页的标题
nano-pdf edit deck.pdf 1 "Change the title to 'Q3 Results' and fix the typo in the subtitle"

# 更新特定页面的日期
nano-pdf edit report.pdf 3 "Update the date from January to February 2026"

# 修改内容
nano-pdf edit contract.pdf 2 "Change the client name from 'Acme Corp' to 'Acme Industries'"
```

#### 注意事项
- 页码可能是0-based或1-based — 如果编辑到了错误的页面，用±1重试
- 编辑后始终验证输出PDF（用 `read_file` 检查文件大小，或打开它）
- 该工具底层使用LLM — 需要API密钥（查看 `nano-pdf --help` 了解配置）
- 适用于文本更改；复杂布局修改可能需要不同方法

---

### 二、PDF文本提取 (ocr-and-documents)

#### 远程URL可用？

如果文档有URL，**始终先尝试 `web_extract`**：

```python
web_extract(urls=["https://arxiv.org/pdf/2402.03300"])
web_extract(urls=["https://example.com/report.pdf"])
```

这会通过Firecrawl处理PDF到markdown的转换，无需本地依赖。

仅在以下情况使用本地提取：文件是本地的、web_extract失败、或需要批处理。

#### 选择本地提取器

| 特性 | pymupdf (~25MB) | marker-pdf (~3-5GB) |
|------|-----------------|---------------------|
| **文本型PDF** | ✅ | ✅ |
| **扫描PDF (OCR)** | ❌ | ✅ (90+种语言) |
| **表格** | ✅ (基础) | ✅ (高精度) |
| **公式 / LaTeX** | ❌ | ✅ |
| **代码块** | ❌ | ✅ |
| **表单** | ❌ | ✅ |
| **页眉页脚移除** | ❌ | ✅ |
| **阅读顺序检测** | ❌ | ✅ |
| **图片提取** | ✅ (嵌入式) | ✅ (带上下文) |
| **图片→文字 (OCR)** | ❌ | ✅ |
| **EPUB** | ✅ | ✅ |
| **Markdown输出** | ✅ (通过pymupdf4llm) | ✅ (原生，更高质量) |
| **安装大小** | ~25MB | ~3-5GB (PyTorch + 模型) |
| **速度** | 即时 | ~1-14s/页 (CPU), ~0.2s/页 (GPU) |

**决策**: 除非需要OCR、公式、表单或复杂布局分析，否则使用pymupdf。

如果用户需要marker功能但系统缺少~5GB可用磁盘：
> "此文档需要OCR/高级提取(marker-pdf)，需要~5GB用于PyTorch和模型。您的系统有[X]GB可用空间。选项：清理空间、提供URL使用web_extract、或尝试pymupdf（适用于文本型PDF但不适用于扫描文档或公式）。"

---

### 三、pymupdf (轻量级)

#### 安装
```bash
pip install pymupdf pymupdf4llm
```

#### 通过辅助脚本使用
```bash
python scripts/extract_pymupdf.py document.pdf              # 纯文本
python scripts/extract_pymupdf.py document.pdf --markdown    # Markdown
python scripts/extract_pymupdf.py document.pdf --tables      # 表格
python scripts/extract_pymupdf.py document.pdf --images out/ # 提取图片
python scripts/extract_pymupdf.py document.pdf --metadata    # 标题、作者、页数
python scripts/extract_pymupdf.py document.pdf --pages 0-4   # 特定页面
```

#### 内联使用
```bash
python3 -c "
import pymupdf
doc = pymupdf.open('document.pdf')
for page in doc:
    print(page.get_text())
"
```

---

### 四、marker-pdf (高质量OCR)

#### 安装
```bash
# 先检查磁盘空间
python scripts/extract_marker.py --check

pip install marker-pdf
```

#### 通过辅助脚本使用
```bash
python scripts/extract_marker.py document.pdf                # Markdown
python scripts/extract_marker.py document.pdf --json         # JSON带元数据
python scripts/extract_marker.py document.pdf --output_dir out/  # 保存图片
python scripts/extract_marker.py scanned.pdf                 # 扫描PDF (OCR)
python scripts/extract_marker.py document.pdf --use_llm      # LLM增强精度
```

#### CLI使用 (marker-pdf安装时自带)
```bash
marker_single document.pdf --output_dir ./output
marker /path/to/folder --workers 4    # 批量处理
```

---

### 五、Arxiv论文

```python
# 仅摘要 (快速)
web_extract(urls=["https://arxiv.org/abs/2402.03300"])

# 完整论文
web_extract(urls=["https://arxiv.org/pdf/2402.03300"])

# 搜索
web_search(query="arxiv GRPO reinforcement learning 2026")
```

---

### 六、分割、合并和搜索

pymupdf原生处理这些功能 — 使用 `execute_code` 或内联Python：

#### 分割：提取前5页到新PDF
```python
import pymupdf
doc = pymupdf.open("report.pdf")
new = pymupdf.open()
for i in range(5):
    new.insert_pdf(doc, from_page=i, to_page=i)
new.save("pages_1-5.pdf")
```

#### 合并多个PDF
```python
import pymupdf
result = pymupdf.open()
for path in ["a.pdf", "b.pdf", "c.pdf"]:
    result.insert_pdf(pymupdf.open(path))
result.save("merged.pdf")
```

#### 跨所有页面搜索文本
```python
import pymupdf
doc = pymupdf.open("report.pdf")
for i, page in enumerate(doc):
    results = page.search_for("revenue")
    if results:
        print(f"Page {i+1}: {len(results)} match(es)")
        print(page.get_text("text"))
```

无需额外依赖 — pymupdf一个包涵盖分割、合并、搜索和文本提取。

---

## ⚠️ 注意事项

1. **web_extract始终是URL的首选**
2. **pymupdf是安全默认** — 即时、无模型、到处可用
3. **marker-pdf用于OCR、扫描文档、公式、复杂布局** — 仅在需要时安装
4. **两个辅助脚本都接受 `--help` 查看完整用法**
5. **marker-pdf首次使用时下载~2.5GB模型到 `~/.cache/huggingface/`**
6. **Word文档 (.docx)**: 参见 `word-documents` 技能 (python-docx用于读取、创建、格式化)
7. **.doc文件 (旧格式)**: 使用 `catdoc` CLI — python-docx无法读取.doc格式
8. **PowerPoint**: 参见 `powerpoint` 技能 (使用python-pptx)

---

## 📚 相关技能
- [[Word文档处理]] - Word文档处理
- [[OCR文字识别]] - 专门OCR识别
- [[PowerPoint制作]] - PPT演示文稿
- [[arXiv论文搜索]] - 学术论文获取

## 🔗 外部资源
- [pymupdf 官方文档](https://pymupdf.readthedocs.io/)
- [marker-pdf GitHub](https://github.com/VikParuchuri/marker)
- [nano-pdf PyPI](https://pypi.org/project/nano-pdf/)

---

*最后更新: 2026年6月16日*