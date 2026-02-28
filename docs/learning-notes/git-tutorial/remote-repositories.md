---
tags:
  - git
  - 远程仓库
  - 协作
---

# 6. 远程仓库：clone、push、pull、fetch

远程仓库是托管在 GitHub、Gitee 等平台上的仓库。它让你和团队成员可以共享代码、协同开发。

## git remote：管理远程连接

```bash
# 查看已配置的远程仓库
git remote

# 查看详细地址
git remote -v

# 添加远程仓库
git remote add origin git@github.com:用户名/仓库名.git

# 修改远程地址
git remote set-url origin git@github.com:用户名/新仓库名.git

# 删除远程连接
git remote remove origin
```

`origin` 是远程仓库的默认别名，你可以起任何名字，但 `origin` 是约定俗成的。

## git clone：克隆远程仓库

```bash
# 克隆到当前目录
git clone git@github.com:用户名/仓库名.git

# 克隆到指定目录
git clone git@github.com:用户名/仓库名.git 目标文件夹

# 只克隆最近一次提交（节省空间）
git clone --depth 1 git@github.com:用户名/仓库名.git
```

`clone` 会自动配置好远程连接（origin），并创建 `main` 分支跟踪远程的 `main`。

## git push：推送到远程

```bash
# 推送当前分支到 origin
git push

# 第一次推送时需要设置上游分支
git push -u origin main
# -u 等价于 --set-upstream，设置完后以后直接 git push 就行

# 推送指定分支
git push origin feature-login

# 强制推送（慎用！会覆盖远程历史）
git push --force
```

> [!warning] 关于 `--force`
> `git push --force` 会用你的本地历史覆盖远程历史，别人的提交可能丢失。**永远不要对公共分支使用 force push。** 如果确实需要，用 `--force-with-lease`（更安全，会检查远程是否有你不知道的提交）。

## git fetch：拉取但不合并

```bash
# 获取远程所有更新（不修改工作区）
git fetch

# 获取指定远程分支的更新
git fetch origin main

# 获取所有远程分支
git fetch --all
```

`fetch` 只把远程的更新下载到本地的 `.git` 目录中，不会修改你的工作区文件。你可以先看看远程改了什么，再决定是否合并。

```bash
# fetch 之后，可以查看远程和本地的差异
git log HEAD..origin/main --oneline
```

## git pull：拉取并合并

```bash
# 拉取并合并当前分支的远程更新
git pull
```

`git pull` 本质上等于 `git fetch` + `git merge`。它把远程的更新拉下来，然后自动合并到你的当前分支。

如果想用 rebase 代替 merge：

```bash
git pull --rebase
```

## 完整的协作流程

```bash
# 1. 克隆项目
git clone git@github.com:团队/项目.git
cd 项目

# 2. 创建功能分支
git switch -c feature/我的功能

# 3. 开发并提交
git add .
git commit -m "feat: 实现我的功能"

# 4. 推送前，先拉取最新代码
git pull --rebase origin main

# 5. 如果有冲突，解决后继续
git add .
git rebase --continue

# 6. 推送到远程
git push -u origin feature/我的功能

# 7. 在 GitHub 上创建 Pull Request
# 8. Code Review 通过后，合并到 main
```

## 上游分支（Upstream）

"上游分支"是本地分支对应的远程分支。设置后，`git push` 和 `git pull` 就不需要每次都指定远程名和分支名了。

```bash
# 设置上游分支
git push -u origin feature-login
# 之后就可以直接
git push
git pull

# 查看上游分支
git branch -vv
```
