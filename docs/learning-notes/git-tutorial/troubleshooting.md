---
tags:
  - git
  - 实战
  - 问题排查
---

# 10. 常见场景与问题排查

这篇汇总日常使用 Git 时最常遇到的问题和解决方案。

## 场景 1：提交信息写错了

```bash
# 修改最近一次的 commit message
git commit --amend -m "新的提交信息"

# 注意：如果已经 push，需要 force push
git push --force
```

## 场景 2：忘了 add 某个文件

```bash
# 把遗漏的文件补进上一次 commit（不修改 message）
git add 遗漏的文件
git commit --amend --no-edit
```

## 场景 3：想暂存（stash）当前的修改

有时候正在开发到一半，突然需要切换到别的分支修 bug，但又不想 commit 半成品：

```bash
# 暂存当前修改
git stash

# 暂存时添加说明
git stash save "正在开发登录功能"

# 查看暂存列表
git stash list

# 恢复最近一次暂存
git stash pop

# 恢复指定暂存
git stash apply stash@{2}

# 删除指定暂存
git stash drop stash@{2}
```

## 场景 4：想看两次提交之间改了什么

```bash
# 对比两个 commit 的差异
git diff a1b2c3d e4f5g6h

# 对比当前工作区和最近一次 commit 的差异
git diff

# 对比暂存区和最近一次 commit 的差异
git diff --staged

# 对比某个文件在两个版本间的差异
git diff a1b2c3d e4f5g6h -- main.cpp
```

## 场景 5：想查看某个文件的历史

```bash
# 查看文件的提交历史
git log --follow -p main.cpp

# 查看文件每一行是谁在什么时候改的（blame）
git blame main.cpp

# 只看某几行
git blame -L 10,20 main.cpp
```

## 场景 6：想找到是哪个 commit 引入了 bug

```bash
# 二分查找法（bisect）
git bisect start
git bisect bad           # 当前版本有 bug
git bisect good a1b2c3d  # 这个版本是好的

# Git 会自动切到中间的 commit，你测试后告诉它：
git bisect good  # 这个版本没问题 → bug 在后面
git bisect bad   # 这个版本也有 → bug 在前面

# 找到后退出
git bisect reset
```

## 场景 7：误删了文件

```bash
# 恢复被 git rm 删除的文件
git checkout HEAD -- 被删的文件

# 如果是 reset --hard 导致的
git reflog
git reset --hard HEAD@{1}
```

## 场景 8：想把多个 commit 合并成一个

```bash
# 交互式 rebase，合并最近 3 个 commit
git rebase -i HEAD~3
```

在弹出的编辑器中：

```
pick a1b2c3d feat: 第一步
squash e4f5g6h feat: 第二步（合并到上一个）
squash h7i8j9k feat: 第三步（合并到上一个）
```

`squash`（或 `s`）会把这一行合并到上一行的 commit 中。

> [!warning] 不要对已推送的提交做 rebase
> 交互式 rebase 会改写 commit hash。如果这些提交已经推送到远程，会影响所有协作者。

## 场景 9：.gitignore 不生效

```bash
# 原因：文件已经被 Git 跟踪了
# 解决：先取消跟踪再重新 add
git rm --cached -r .
git add .
```

## 场景 10：查看仓库的统计信息

```bash
# 查看仓库大小
git count-objects -vH

# 查看每个贡献者的提交数
git shortlog -sn

# 查看最近的提交活跃度
git log --oneline --since="1 month ago" | wc -l
```

## 速查表

| 我想... | 用这个命令 |
|---------|-----------|
| 看当前状态 | `git status` |
| 看某个文件改了什么 | `git diff 文件名` |
| 看完整的修改历史 | `git log -p` |
| 看某行是谁改的 | `git blame 文件名` |
| 暂存当前修改 | `git stash` |
| 恢复暂存 | `git stash pop` |
| 回退没 push 的提交 | `git reset --soft HEAD~1` |
| 回退已 push 的提交 | `git revert HEAD` |
| 找回误操作 | `git reflog` |
| 二分查 bug | `git bisect` |
