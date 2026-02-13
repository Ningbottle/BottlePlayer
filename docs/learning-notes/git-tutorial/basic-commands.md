---
tags:
  - git
  - 基础命令
---

# 3. 基础命令：add、commit、status、log

这篇覆盖日常使用频率最高的四个命令。

## git init：初始化仓库

```bash
git init
```

在当前目录创建一个 `.git/` 隐藏文件夹，这个文件夹就是你的本地仓库。初始化后，当前目录就变成了"工作区"。

## git status：查看状态

```bash
git status
```

这是你用得最多的命令之一。它会告诉你：

- 哪些文件是新的（Untracked）
- 哪些文件被修改了但还没 add（Modified）
- 哪些文件已经 add 了等待 commit（Staged）
- 当前在哪个分支上

```bash
# 示例输出
On branch main

No commits yet

Untracked files:
  (use "git add <file>..." to include in what will be committed)
        main.cpp
        readme.md
```

`git status -s` 可以显示简短格式：

```
?? main.cpp      # ?? 表示未跟踪
 M main.cpp      #  M 表示已修改但未暂存（注意 M 前面有空格）
M  main.cpp      # M  表示已暂存（注意 M 后面有空格）
```

## git add：放入暂存区

```bash
# 添加单个文件
git add main.cpp

# 添加多个文件
git add main.cpp readme.md

# 添加当前目录所有改动（最常用）
git add .

# 添加所有改动（包括删除）
git add -A
```

> [!warning] 慎用 `git add .`
> 在项目根目录执行 `git add .` 会把所有文件都加入暂存区，包括你不想要的临时文件、编译产物等。建议先 `git status` 看一眼，或者配置好 `.gitignore`（见 [[git的学习和使用/9.协作工作流-gitignore与commit规范]]）。

## git commit：提交到仓库

```bash
# 基本提交
git commit -m "描述这次改了什么"

# 查看将要提交的内容（dry run）
git commit -m "xxx" --dry-run

# 跳过暂存区，直接提交所有已跟踪文件的修改（不包括新文件）
git commit -am "xxx"
```

### commit message 怎么写

好的 commit message 应该回答"这次改动做了什么、为什么做"：

```
✅ feat: 添加用户登录接口
✅ fix: 修复内存泄漏问题
✅ refactor: 重构日志模块
❌ update          ← 太模糊
❌ 改了点东西       ← 等于没说
```

推荐的格式（Conventional Commits）：

| 前缀 | 含义 |
|------|------|
| `feat:` | 新功能 |
| `fix:` | 修 bug |
| `docs:` | 文档变更 |
| `style:` | 代码格式（不影响逻辑） |
| `refactor:` | 重构（不是新功能也不是修 bug） |
| `test:` | 添加或修改测试 |
| `chore:` | 构建/工具/依赖变更 |

## git log：查看提交历史

```bash
# 基本查看
git log

# 一行显示（推荐日常使用）
git log --oneline

# 显示最近 3 条
git log --oneline -3

# 图形化显示分支历史
git log --oneline --graph --all

# 显示每次提交的文件变更
git log -p

# 按作者筛选
git log --author="名字"

# 按时间筛选
git log --since="2026-01-01" --until="2026-06-01"
```

`git log` 输出的每一条记录包含：

```
commit a1b2c3d4e5f6...    ← 提交的哈希值（唯一标识）
Author: 名字 <邮箱>
Date:   Mon Jun 1 10:00:00 2026 +0800

    feat: 添加用户登录接口
```

前 7 位哈希值（如 `a1b2c3d`）就够用了，后面的命令中会频繁用到它来定位某次提交。

## 日常循环

你 90% 的 Git 操作就是这个循环：

```
修改文件 → git status → git add → git commit → git push
```

养成习惯：**每次有意义的修改后都 commit**，不要攒了一堆改动才提交一次。小的、频繁的 commit 比大的、稀疏的 commit 好管理得多。
