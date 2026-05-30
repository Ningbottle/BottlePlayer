# BottleMusic 发布指南

## 一、构建 Release 版本

### 1.1 前置条件
- VS Developer PowerShell（MSVC C++20 工具链）
- Node.js 18+、pnpm 11+
- Rust 工具链（Tauri 依赖）

### 1.2 构建步骤

```powershell
# 1. 编译 C++ DLL（Release 配置）
cmake --preset bottlemusic-release -S native
cmake --build native/out/bottlemusic-release --config Release --target EchoCAPI

# 2. 将 DLL 拷贝到 Tauri 资源目录
Copy-Item native/out/bottlemusic-release/EchoCAPI.dll ui/src-tauri/libs/ -Force

# 3. 构建 Tauri 应用（含 NSIS 安装包）
cd ui
pnpm install
pnpm tauri build
```

### 1.3 构建产物位置
```
ui/src-tauri/target/release/bundle/
├── nsis/
│   └── BottleMusic_0.1.0_x64-setup.exe    # NSIS 安装包
└── msi/
    └── BottleMusic_0.1.0_x64_en-US.msi    # MSI 安装包（可选）
```

---

## 二、GitHub Release 发布流程

### 2.1 创建 Git Tag

```powershell
# 确保在 main 分支且工作区干净
git add .
git commit -m "chore: prepare v0.1.0 release"
git tag -a v0.1.0 -m "Release v0.1.0"
git push origin main --tags
```

### 2.2 在 GitHub 创建 Release

1. 访问 `https://github.com/YOUR_USERNAME/KuGouMusic/releases`
2. 点击 "Draft a new release"
3. 选择刚推送的 tag（如 `v0.1.0`）
4. 填写 Release 标题和说明（见下方模板）
5. 上传构建产物（`.exe` 安装包）
6. 点击 "Publish release"

---

## 三、Release Notes 模板

```markdown
# BottleMusic v0.1.0

> 酷狗概念版 PC 非官方客户端 · 首个公开测试版

## ⚠️ 重要声明

本项目**仅用于个人学习和技术研究**，内含酷狗音乐逆向工程相关内容：
- 本项目与酷狗音乐官方无关，非官方客户端
- 音乐数据和版权归原平台及版权方所有
- 请尊重知识产权，支持正版音乐
- **仅供学习研究，禁止用于商业用途**

## 📥 下载

| 文件 | 说明 |
|------|------|
| `BottleMusic_0.1.0_x64-setup.exe` | Windows 64位 安装包（推荐） |

**系统要求**：Windows 10/11 x64，需已安装 WebView2 运行时（Win10 1803+ 通常已内置）

## ✨ 已实现功能

### 核心功能
- 🔐 **扫码登录** — 酷狗 APP 扫码，支持 VIP 状态同步
- 🔍 **搜索** — 歌曲、歌手、歌单搜索
- 🎵 **在线播放** — 支持 VIP 歌曲播放（需 VIP 账号）
- 📋 **歌单** — 加载用户收藏/自建歌单
- 📝 **歌词** — 实时歌词同步高亮
- 🎁 **每日 VIP** — 自动领取每日免费 VIP（听歌/广告）

### 界面特色
- 📰 **Newsprint 报纸风** — 独特的报纸排版美学
- 🎨 **毛玻璃播放栏** — 底部播放栏采用模糊玻璃效果
- 📱 **响应式布局** — 支持 1024x700 至 2560x1620 分辨率

### 技术特性
- 💾 **内存优化** — 实测约 51MB，远低于 220MB 预算
- ⚡ **FFI 架构** — Tauri + C++ DLL 直接调用，零 HTTP 开销
- 🔒 **隐私保护** — 敏感信息（token/设备指纹）不泄漏到前端

## 🐛 已知问题

| 问题 | 状态 | 说明 |
|------|------|------|
| 播放核心 | 待优化 | 当前使用 WebView HTML5 Audio，C++ Media Foundation 仅预留接口 |
| 图片缓存 | 待优化 | 内存有 16MB 预算，磁盘缓存暂无上限 |
| 部分歌曲不可播 | 预期行为 | 版权受限歌曲返回 `status:3`，已做友好提示 |

## 🔧 技术栈

- **前端**：Tauri 2.0 + Vue 3 + Vanilla CSS
- **后端**：C++20 EchoCAPI.dll（FFI 直注）
- **构建**：Vite 6 + pnpm 11 + CMake + MSVC + Cargo

## 📦 从源码构建

```powershell
# 克隆仓库
git clone https://github.com/YOUR_USERNAME/KuGouMusic.git
cd KuGouMusic

# 编译 C++ DLL
cmake --preset bottlemusic-release -S native
cmake --build native/out/bottlemusic-release --config Release --target EchoCAPI
Copy-Item native/out/bottlemusic-release/EchoCAPI.dll ui/src-tauri/libs/ -Force

# 构建前端
cd ui
pnpm install
pnpm tauri build
```

## 🙏 致谢

- [MakcRe/KuGouMusicApi](https://github.com/MakcRe/KuGouMusicApi) — 接口参考实现

## 📄 许可证

本项目采用 MIT 许可证，详见 [LICENSE](LICENSE)。

---

**⚠️ 免责声明**：本项目仅供学习研究，使用者需自行承担风险。开发者不对因使用本软件导致的任何问题负责。
```

---

## 四、GitHub Actions 自动构建（可选）

创建 `.github/workflows/release.yml`：

```yaml
name: Build and Release

on:
  push:
    tags:
      - 'v*'

permissions:
  contents: write

jobs:
  build:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4
      
      - name: Setup Node.js
        uses: actions/setup-node@v4
        with:
          node-version: '18'
          
      - name: Setup pnpm
        uses: pnpm/action-setup@v2
        with:
          version: '8'
          
      - name: Setup Rust
        uses: dtolnay/rust-toolchain@stable
        
      - name: Install dependencies
        run: pnpm install
        working-directory: ui
        
      - name: Build C++ DLL
        shell: pwsh
        run: |
          # 需要预装 VS Build Tools
          cmake --preset bottlemusic-release -S native
          cmake --build native/out/bottlemusic-release --config Release --target EchoCAPI
          Copy-Item native/out/bottlemusic-release/EchoCAPI.dll ui/src-tauri/libs/ -Force
          
      - name: Build Tauri App
        run: pnpm tauri build
        working-directory: ui
        
      - name: Create Release
        uses: softprops/action-gh-release@v1
        with:
          files: |
            ui/src-tauri/target/release/bundle/nsis/*.exe
          draft: true
          prerelease: true
        env:
          GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}
```

---

## 五、发布检查清单

### 发布前
- [ ] 更新 `tauri.conf.json` 中的版本号
- [ ] 更新 `package.json` 中的版本号
- [ ] 确保 `.gitignore` 排除了构建产物
- [ ] 运行测试确保功能正常
- [ ] 检查 README 中的功能列表是否准确

### 构建时
- [ ] 使用 Release 配置构建 C++ DLL
- [ ] 确认 DLL 拷贝到 `ui/src-tauri/libs/`
- [ ] 运行 `pnpm tauri build` 生成安装包
- [ ] 测试安装包能否正常安装和运行

### 发布时
- [ ] 创建带注释的 Git tag
- [ ] 填写完整的 Release Notes
- [ ] 上传 `.exe` 安装包
- [ ] 添加免责声明

### 发布后
- [ ] 验证下载链接可用
- [ ] 测试安装流程
- [ ] 监控 Issues 反馈

---

## 六、常见问题

### Q: 为什么没有自动更新？
A: 当前版本为手动更新。后续可集成 Tauri Updater 插件实现自动更新。

### Q: 安装时被杀毒软件拦截？
A: 因为没有代码签名，Windows SmartScreen 可能警告。点击"更多信息" → "仍要运行"即可。正式发布建议购买代码签名证书。

### Q: 如何卸载？
A: 通过 Windows 设置 → 应用 → BottleMusic 卸载，或运行安装目录的卸载程序。
