---
tags:
  - git
  - 安装配置
---

# 1. 安装与配置

## Linux 下安装

对于不同的 Linux 发行版：

```bash
# Ubuntu / Debian
sudo apt install git -y

# CentOS / RHEL
sudo yum install git -y

# Arch Linux
sudo pacman -S git
```

安装完成后验证：

```bash
git --version
# 输出类似：git version 2.43.0
```

## Windows 下安装

1. 前往 [Git 官网](https://git-scm.com/download/win) 下载安装包
2. 安装时保持默认选项即可，建议勾选 "Git Bash Here"
3. 安装完成后，在任意文件夹右键即可看到 "Git Bash Here" 选项

也可以在 Windows 终端中验证：

```bash
git --version
```

## 初始配置

安装完成后，**必须**配置用户信息，否则 commit 时会报错：

```bash
# 配置用户名（全局）
git config --global user.name "你的名字"

# 配置邮箱（全局）
git config --global user.email "你的邮箱"
```

验证配置：

```bash
git config --list
```

### 配置级别说明

Git 有三个配置级别，优先级从高到低：

| 级别 | 作用范围 | 配置文件位置 | 设置参数 |
|------|----------|-------------|----------|
| system | 所有用户 | `/etc/gitconfig` | `--system` |
| global | 当前用户 | `~/.gitconfig` | `--global` |
| local | 当前仓库 | `.git/config` | `--local`（默认） |

日常使用中，`--global` 就够了。如果某个仓库需要特殊配置（比如工作项目用不同邮箱），可以在该仓库内用 `--local` 覆盖。

## 配置 SSH 密钥（连接远程仓库必需）

如果要使用 GitHub 或 Gitee，需要配置 SSH 密钥：

```bash
# 生成密钥对
ssh-keygen -t ed25519 -C "你的邮箱"
# 一路回车即可

# 查看公钥
cat ~/.ssh/id_ed25519.pub
```

将输出的公钥内容复制，粘贴到 GitHub → Settings → SSH and GPG keys → New SSH key 中。

验证连接：

```bash
ssh -T git@github.com
# 成功时输出：Hi 用户名! You've successfully authenticated...
```
