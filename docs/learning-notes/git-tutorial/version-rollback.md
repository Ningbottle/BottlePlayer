---
tags:
  - git
  - 版本回退
---

# 7. 版本回退：reset、revert、checkout

有时候提交了错误的代码，需要回退到之前的版本。Git 提供了三种方式，各有适用场景。

## git reset：回退提交

`reset` 会移动 HEAD 指针到指定的提交，并根据参数决定对工作区和暂存区的影响。

### 三种模式

```bash
# soft：只移动 HEAD，暂存区和工作区不变
git reset --soft HEAD~1
# 效果：回退 1 次 commit，但改动还在暂存区，可以重新 commit

# mixed（默认）：移动 HEAD + 清空暂存区，工作区不变
git reset --mixed HEAD~1
git reset HEAD~1   # 等价写法
# 效果：回退 1 次 commit，改动还在工作区，但需要重新 add

# hard：移动 HEAD + 清空暂存区 + 覆盖工作区
git reset --hard HEAD~1
# 效果：彻底回退，所有改动消失（危险操作）
```

`HEAD~N` 表示往回 N 次提交，也可以用 commit hash：

```bash
git reset --soft a1b2c3d   # 回到指定 commit
```

> [!warning] `reset --hard` 的代价
> `--hard` 会丢弃所有未提交的修改。如果不小心执行了，可以用 `git reflog` 找回（见下文）。

## git revert：反转提交

`revert` 不会删除提交记录，而是创建一个**新的提交**，内容恰好是指定提交的"反向操作"。

```bash
# 反转最近一次提交
git revert HEAD

# 反转指定提交
git revert a1b2c3d
```

执行后 Git 会打开编辑器让你写 commit message，默认是 `Revert "原始message"`。

### reset vs revert

| 特性 | reset | revert |
|------|-------|--------|
| 历史是否改变 | 是（提交被删除） | 否（新增反转提交） |
| 是否安全 | 仅本地安全 | 始终安全 |
| 适用场景 | 本地还没 push 的错误提交 | 已经 push 到远程的错误提交 |

**核心原则：已经推送到远程的提交，用 revert；还没推送的提交，用 reset。**

## git checkout：恢复文件

`checkout` 可以恢复单个文件到某个版本，不影响其他文件：

```bash
# 把工作区的文件恢复到暂存区的版本（撤销工作区的修改）
git checkout -- main.cpp

# 把工作区的文件恢复到某次提交的版本
git checkout a1b2c3d -- main.cpp
```

> [!tip] 现代替代命令
> `git checkout -- main.cpp` 可以用 `git restore main.cpp` 替代（Git 2.23+），语义更清晰。

## git reflog：找回丢失的提交

如果你不小心 `reset --hard` 了，或者 rebase 搞砸了，`reflog` 是你的救命稻草：

```bash
# 查看 HEAD 的移动历史
git reflog

# 输出示例：
# a1b2c3d HEAD@{0}: reset: moving to HEAD~1
# e4f5g6h HEAD@{1}: commit: feat: 重要功能
# ...

# 找回丢失的提交
git reset --hard e4f5g6h
```

`reflog` 记录了 HEAD 指针的每一次移动，即使是被 reset 删除的提交也还在本地（默认保留 90 天）。

## 实用场景速查

| 场景 | 命令 |
|------|------|
| 撤销工作区的修改 | `git restore main.cpp` |
| 从暂存区移除文件（不删除文件） | `git restore --staged main.cpp` |
| 回退还没 push 的 commit | `git reset --soft HEAD~1` |
| 回退已经 push 的 commit | `git revert HEAD` |
| 彻底回退到某版本 | `git reset --hard a1b2c3d`（慎用） |
| 找回被误删的提交 | `git reflog` + `git reset --hard` |
