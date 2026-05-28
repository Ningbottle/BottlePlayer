---
name: cpp-build-verify
description: C++ 构建验证与 smoke test 流程。每次修改 native/ 目录后运行 CMake 构建并执行基础验证。
---

## 何时使用
修改了 `native/` 目录下的 C++ 代码（特别是 `core/`、`compat_server/`、`async/`、`diagnostics/`）后，需要验证：
1. CMake 构建是否通过
2. 相关 target 是否能链接
3. 是否存在编译期类型错误

## 执行步骤

### 1. 构建前检查
- 确认 `native/CMakeLists.txt` 包含修改的文件
- 检查 `native/CMakePresets.json` 中当前使用的 preset 名称

### 2. 执行构建
```powershell
# 在项目根目录执行
& native\run_build.cmd
# 或直接使用 CMake
cmake --build native/build --preset <current-preset>
```

### 3. 验证要点
- **零 warning 零容忍**：重点关注 `SongUrlService`、`DeviceService`、`UserService` 相关的 warning
- 检查链接错误：如果新增符号未导出，检查头文件是否包含 `__declspec(dllexport)` 或 CMake 的 `EXPORT` 设置

### 4. Smoke Test（最小运行时验证）
如果构建成功，运行 CompatServer 检查是否能正常启动：
```powershell
# 如果生成了 CompatServer.exe
.\native\build\compat_server\CompatServer.exe --help
# 或通过 run_build_log.cmd 查看日志
```

## 常见失败模式
- **找不到头文件**：检查 `target_include_directories` 是否包含新目录
- **符号未解析**：检查是否缺少 `.cpp` 文件添加到 `add_library`/`add_executable`
- **C++/WinRT 版本不匹配**：确认 Windows SDK 版本 >= 10.0.19041

## 约束
- 构建失败时不继续后续步骤（不跑测试、不改 UI）
- 优先修复编译错误再处理 warning
