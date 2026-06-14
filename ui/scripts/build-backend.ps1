# 构建原生后端 EchoCAPI.dll（FFI 架构，由 Rust 通过 libloading 加载）。
# 用法：pnpm backend:build
# 说明：实际目标是 CMakeLists.txt 里的 `EchoCAPI`（SHARED DLL），
#       build.rs 会把它拷入 target/debug 供 Rust 运行时加载。
$ErrorActionPreference = 'Stop'

$root      = Split-Path -Parent $PSScriptRoot                   # ui/
$nativeDir = Resolve-Path "$root\..\native"
$preset    = 'bottlemusic-check'
$buildDir  = Join-Path $nativeDir "out\$preset"

# 自动探测 Visual Studio 开发环境（仅配置一次时需要，用于把 MSVC 编译器喂给 CMake）
function Find-VsDevCmd {
    $candidates = @(
        'C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat',
        'C:\Program Files\Microsoft Visual Studio\17\Common7\Tools\VsDevCmd.bat',
        'C:\Program Files (x86)\Microsoft Visual Studio\17\Common7\Tools\VsDevCmd.bat',
        'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat',
        'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat',
        'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat'
    )
    foreach ($c in $candidates) { if (Test-Path $c) { return $c } }
    return $null
}

function Invoke-VsDevCmd([string]$cmdLine) {
    $vsdev = Find-VsDevCmd
    if ($vsdev) {
        cmd /c "`"$vsdev`" -arch=x64 -host_arch=x64 2>nul && $cmdLine"
    } else {
        # 没有 VS 也试一下：用户可能已经把 MSVC/CMake 配进了 PATH
        cmd /c $cmdLine
    }
}

# 1. CMake 配置（首次或 CMakeCache 缺失时）
if (-not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))) {
    Write-Host "[backend:build] configuring CMake ($preset preset)..."
    Invoke-VsDevCmd "cmake -S `"$nativeDir`" --preset $preset"
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed (exit $LASTEXITCODE)" }
}

# 2. 构建 EchoCAPI（SHARED DLL）
Write-Host "[backend:build] building EchoCAPI.dll..."
Invoke-VsDevCmd "cmake --build `"$buildDir`" --config Debug --target EchoCAPI"
if ($LASTEXITCODE -ne 0) { throw "cmake build failed (exit $LASTEXITCODE)" }

# 3. 同步 DLL（由 sync-backend.ps1 处理）
& (Join-Path $PSScriptRoot 'sync-backend.ps1')
