---
tags:
  - git
  - 标签
  - 版本发布
---

# 8. 标签（tag）与版本发布

标签是给某次提交打上一个固定的名字，通常用于标记版本发布点。和分支不同，标签一旦创建就不会移动。

## 创建标签

```bash
# 创建轻量标签（只是给 commit 起个别名）
git tag v1.0.0

# 给指定提交打标签
git tag v1.0.0 a1b2c3d

# 创建附注标签（推荐，包含作者、日期和说明）
git tag -a v1.0.0 -m "Release version 1.0.0"
```

附注标签（annotated tag）包含完整的元信息，在 Git 中是作为一个独立对象存储的。轻量标签（lightweight tag）只是一个指向 commit 的引用。正式发布版本时，应该用附注标签。

## 查看标签

```bash
# 列出所有标签
git tag

# 按模式匹配
git tag -l "v1.*"

# 查看标签详情
git show v1.0.0
```

## 推送标签

标签默认不会随 `git push` 推送，需要手动推：

```bash
# 推送单个标签
git push origin v1.0.0

# 推送所有标签
git push origin --tags
```

## 删除标签

```bash
# 删除本地标签
git tag -d v1.0.0

# 删除远程标签
git push origin --delete v1.0.0
```

## 基于标签检出代码

```bash
# 检出标签对应的代码（会进入 detached HEAD 状态）
git checkout v1.0.0

# 如果要在标签基础上开发，创建新分支
git checkout -b hotfix/v1.0.1 v1.0.0
```

## 版本号规范（Semantic Versioning）

大多数项目遵循语义化版本号 `MAJOR.MINOR.PATCH`：

| 部分 | 何时递增 |
|------|----------|
| MAJOR | 有不兼容的 API 变更 |
| MINOR | 向下兼容地添加新功能 |
| PATCH | 向下兼容地修复 bug |

示例：`v1.0.0` → `v1.0.1`（修 bug）→ `v1.1.0`（加功能）→ `v2.0.0`（大改）
