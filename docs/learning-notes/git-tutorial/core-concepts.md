---
tags:
  - git
  - 核心概念
---

# 2. 核心概念：工作区、暂存区、仓库

理解 Git 最关键的一点，是搞清楚三个区域之间的关系。所有命令本质上都是在操作这三个区域之间的数据流转。

## 三个区域

```
工作区（Working Directory）
  │
  │  git add
  ▼
暂存区（Staging Area / Index）
  │
  │  git commit
  ▼
本地仓库（Repository / .git）
```

### 工作区（Working Directory）

就是你看到的文件夹。你在里面创建、修改、删除文件，这些都是"工作区"里的操作。Git 并不会自动追踪这些变化——你需要主动告诉 Git。

### 暂存区（Staging Area）

也叫"索引"（Index）。它是一个准备提交的清单。你用 `git add` 把工作区的修改放入暂存区，相当于"这些改动我确认了，准备提交"。

暂存区的意义在于：你可以分多次 `add` 不同的文件，精确控制每次 commit 包含哪些修改，而不是一股脑全部提交。

### 本地仓库（Repository）

执行 `git commit` 后，暂存区的内容会被永久记录到本地仓库中，生成一个带有唯一哈希值的提交记录。这个记录就是你的"版本快照"。

## 第四个区域：远程仓库

当你使用 GitHub / Gitee 时，还多了一个远程仓库（Remote）。它和本地仓库是镜像关系：

```
本地仓库 ──git push──→ 远程仓库
本地仓库 ←──git pull── 远程仓库
```

## 文件的生命周期

一个文件在 Git 中有两种状态：

**未跟踪（Untracked）**：文件存在于工作区，但 Git 不知道它。新创建的文件默认是这个状态。

**已跟踪（Tracked）**：Git 已经在追踪这个文件。已跟踪的文件又有三种子状态：

- **未修改（Unmodified）**：上次 commit 之后没有改动过
- **已修改（Modified）**：文件被改了，但还没有 `git add`
- **已暂存（Staged）**：文件被改了，并且已经 `git add`，等待 commit

完整的状态流转：

```
新文件 ──git add──→ 已暂存 ──git commit──→ 已跟踪（未修改）
                                            │
                                    修改文件 │
                                            ▼
                                      已修改 ──git add──→ 已暂存
```

## 用一个例子串起来

```bash
# 1. 初始化仓库
mkdir my-project && cd my-project
git init
# 此时 .git/ 目录被创建，本地仓库诞生了

# 2. 在工作区创建文件
echo "hello" > main.cpp
# main.cpp 是 Untracked 的

# 3. 放入暂存区
git add main.cpp
# main.cpp 变成 Staged

# 4. 提交到仓库
git commit -m "first commit"
# main.cpp 变成 Tracked + Unmodified

# 5. 修改文件
echo "world" >> main.cpp
# main.cpp 变成 Modified

# 6. 再次 add + commit
git add main.cpp
git commit -m "add world"
```

> [!tip] 核心记忆
> - `git add` = 工作区 → 暂存区
> - `git commit` = 暂存区 → 本地仓库
> - `git push` = 本地仓库 → 远程仓库
> - `git pull` = 远程仓库 → 工作区
