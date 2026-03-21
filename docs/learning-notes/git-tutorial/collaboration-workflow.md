---
tags:
  - git
  - 协作
  - 规范
---

# 9. 协作工作流：gitignore 与 commit 规范

## .gitignore：告诉 Git 忽略哪些文件

项目中有些文件不需要版本控制：编译产物、临时文件、IDE 配置、密钥文件等。在仓库根目录创建 `.gitignore` 文件，列出要忽略的模式：

```gitignore
# 编译产物
*.o
*.out
build/
cmake-build-*/

# IDE 配置
.vscode/
.idea/
*.swp

# 系统文件
.DS_Store
Thumbs.db

# 敏感信息
.env
*.key
credentials.json

# 临时文件
*.tmp
*.log
```

### 匹配规则

| 模式 | 含义 |
|------|------|
| `*.log` | 忽略所有 .log 文件 |
| `build/` | 忽略 build 目录 |
| `/config.json` | 只忽略根目录的 config.json |
| `doc/*.txt` | 忽略 doc/ 下的 .txt，但不忽略子目录 |
| `!important.log` | 不忽略 important.log（即使 *.log 被忽略） |

### 已经被跟踪的文件怎么忽略

如果文件已经被 commit 过了，加入 `.gitignore` 不会生效。需要先取消跟踪：

```bash
# 从仓库中移除（但保留本地文件）
git rm --cached config.json

# 如果是一整个目录
git rm -r --cached build/
```

## Commit 规范

### Conventional Commits 格式

```
<type>(<scope>): <subject>

<body>

<footer>
```

其中：

- **type**：必选，说明提交类型（见下表）
- **scope**：可选，影响范围（如模块名）
- **subject**：必选，简短描述
- **body**：可选，详细描述
- **footer**：可选，关联 issue 或 BREAKING CHANGE

| type | 含义 |
|------|------|
| `feat` | 新功能 |
| `fix` | 修 bug |
| `docs` | 文档变更 |
| `style` | 代码格式调整（不影响逻辑） |
| `refactor` | 重构 |
| `perf` | 性能优化 |
| `test` | 测试相关 |
| `chore` | 构建/工具/依赖 |
| `revert` | 回滚提交 |

示例：

```
feat(auth): 添加 JWT token 刷新机制

当 token 过期时自动刷新，避免用户被强制登出。
刷新失败时回退到登录页面。

Closes #42
```

### 实际项目中的好 commit 粒度

每次 commit 应该是一个**逻辑上完整的最小改动**：

```
✅ feat: 实现用户注册的接口
✅ feat: 添加注册表单的前端验证
✅ fix: 修复注册时邮箱格式校验的正则错误

❌ update（太模糊）
❌ 把整个项目一次提交（太大）
❌ 每写一行就 commit（太碎）
```

## 分支策略

### GitHub Flow（推荐个人或小团队）

```
main ──────────────────────────→ 永远可部署
  \                          /
   feature/xxx ── PR ── merge
```

规则很简单：`main` 永远是可部署的，所有开发都在功能分支上进行，完成后通过 Pull Request 合并回 main。

### Git Flow（更正式，适合有版本发布周期的项目）

```
main ────── release/1.0 ──────→ 发布
  \           /
   develop ──
    \      /
     feature/xxx
```

多了 `develop`（开发主线）和 `release/*`（发布准备分支），适合需要维护多个版本的项目。

## Pull Request（PR）工作流

PR 不只是"请求合并代码"，它是团队协作的核心环节：

```bash
# 1. 从 main 创建功能分支
git switch -c feature/user-auth

# 2. 开发完成后推送
git push -u origin feature/user-auth

# 3. 在 GitHub 上创建 PR
#    - 标题清晰描述改了什么
#    - 描述中列出改动要点
#    - 关联相关 issue

# 4. Code Review
#    - 团队成员审查代码
#    - 根据反馈修改
#    - 继续 push 到同一分支（PR 会自动更新）

# 5. Review 通过后合并
#    - 推荐使用 "Squash and merge"（把多次提交压缩成一个）
#    - 或删除功能分支
```
