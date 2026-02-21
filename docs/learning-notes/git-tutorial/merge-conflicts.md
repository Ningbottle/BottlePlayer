---
tags:
  - git
  - 合并
  - 冲突
---

# 5. 合并与冲突解决

分支开发完之后，最终要把代码合并回主线。这个过程就是 merge，而冲突（conflict）是合并时最常遇到的问题。

## git merge：合并分支

```bash
# 先切到目标分支
git switch main

# 把 feature 分支合并进来
git merge feature-login
```

### 两种合并方式

**Fast-forward（快进）**：当 main 在你创建分支之后没有任何新提交时，Git 只需把 main 指针直接移到 feature 分支的最新 commit。没有新的 commit 产生。

```
合并前：
  C1 ── C2 ── C3   (main)
              │
              └── C4 ── C5   (feature)

合并后：
  C1 ── C2 ── C3 ── C4 ── C5   (main, feature)
```

**真正的合并（Three-way merge）**：当 main 在分支创建之后也有了新提交时，Git 需要找到两个分支的共同祖先，然后三方合并，生成一个新的"合并 commit"。

```
合并前：
  C1 ── C2 ── C3 ── C6   (main)
              │
              └── C4 ── C5   (feature)

合并后：
  C1 ── C2 ── C3 ── C6 ── C7   (main)
              │              /
              └── C4 ── C5 ──
                          (feature)
```

C7 就是合并 commit，它有两个父提交（C6 和 C5）。

## 冲突（Conflict）

当两个分支修改了**同一个文件的同一部分**时，Git 无法自动判断该保留哪个版本，就会产生冲突。

### 冲突时的文件内容

Git 会在冲突文件中插入标记：

```cpp
<<<<<<< HEAD
// main 分支的版本
void login(const string& username) {
=======
// feature 分支的版本
void login(const string& username, const string& password) {
>>>>>>> feature-login
```

### 解决冲突的步骤

```bash
# 1. 执行 merge，发现冲突
git merge feature-login

# 2. Git 会告诉你哪些文件有冲突
# CONFLICT (content): Merge conflict in main.cpp

# 3. 打开冲突文件，手动编辑
#    删除 <<<<<<< ======= >>>>>>> 标记
#    选择保留哪段代码（或合并两段）

# 4. 编辑完成后，标记为已解决
git add main.cpp

# 5. 完成合并提交
git commit -m "merge: 合并 feature-login 分支"
```

> [!tip] 解决冲突的原则
> - 不要盲目选择"保留我的版本"或"保留对方的版本"
> - 读懂两边的代码，理解各自的目的
> - 如果不确定，找对应分支的开发者确认
> - 解决完冲突后一定要编译测试一遍

## git merge --abort：放弃合并

如果你发现自己搞不定冲突，可以放弃这次合并：

```bash
git merge --abort
```

这会恢复到执行 merge 之前的状态，所有冲突标记都会消失。

## rebase（变基）：另一种合并方式

除了 `merge`，还可以用 `rebase` 来合并分支。rebase 会把你的提交"搬到"目标分支的最顶端，让历史看起来是一条直线。

```bash
# 在 feature 分支上执行
git switch feature-login
git rebase main
```

**merge vs rebase 的选择**：

| 方式 | 历史形状 | 适用场景 |
|------|----------|----------|
| merge | 保留分支分叉 | 公共分支（main/develop） |
| rebase | 线性历史 | 个人功能分支，想让历史干净 |

> [!warning] rebase 的黄金法则
> **永远不要 rebase 已经推送到远程的公共分支。** rebase 会改写提交历史，如果别人基于你的旧提交工作，会造成严重混乱。
