---
tags:
  - git
  - 分支
---

# 4. 分支的创建、切换、删除

分支是 Git 最强大的功能之一。它让你可以在不影响主线的情况下，开辟一条独立的开发线路。

## 什么是分支

Git 的分支本质上就是一个指向某次 commit 的**轻量级指针**。创建分支几乎不占空间，因为 Git 只是创建了一个新的指针，并没有复制文件。

```
        C1 ── C2 ── C3   (main)
                    │
                    └── C4 ── C5   (feature-login)
```

默认分支通常叫 `main`（或 `master`）。你在上面做的每一次 commit 都让 `main` 指针往前移动一格。当你创建新分支并在新分支上提交时，新分支的指针往前走，`main` 停在原地。

## 分支命令

### 查看分支

```bash
# 查看本地分支（* 标记当前分支）
git branch

# 查看所有分支（包括远程）
git branch -a

# 查看分支及其最近一次提交
git branch -v
```

### 创建分支

```bash
# 创建新分支（不切换过去）
git branch feature-login

# 创建并切换到新分支（最常用）
git checkout -b feature-login

# 等价的现代写法
git switch -c feature-login
```

> [!tip] `checkout` vs `switch`
> `git switch` 是 Git 2.23+ 引入的新命令，功能更单一、更安全。`git checkout` 身兼多职（切换分支、恢复文件、切换 commit），容易混淆。新项目建议用 `git switch`。

### 切换分支

```bash
git checkout feature-login
# 或
git switch feature-login
```

切换分支时，Git 会把工作区的文件替换成目标分支的版本。如果你有未提交的修改，Git 会阻止切换（防止丢失修改）。

### 删除分支

```bash
# 删除已合并的分支（安全删除）
git branch -d feature-login

# 强制删除未合并的分支（慎用）
git branch -D feature-login

# 删除远程分支
git push origin --delete feature-login
```

## 分支命名建议

好的分支名应该一眼看出这条分支在做什么：

| 类型 | 格式 | 示例 |
|------|------|------|
| 新功能 | `feature/功能名` | `feature/user-login` |
| 修 bug | `fix/问题描述` | `fix/memory-leak` |
| 紧急修复 | `hotfix/问题描述` | `hotfix/crash-on-startup` |
| 实验性 | `experiment/描述` | `experiment/new-parser` |

## 典型工作流

```bash
# 1. 从 main 拉出最新代码
git switch main
git pull

# 2. 创建功能分支
git switch -c feature/user-login

# 3. 在功能分支上开发、提交
# ...多次 add + commit...

# 4. 开发完成，切回 main
git switch main

# 5. 合并功能分支（见下一篇）
git merge feature/user-login

# 6. 删除功能分支
git branch -d feature/user-login
```
