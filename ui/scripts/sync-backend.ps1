# 把构建产物 EchoCAPI.dll 同步到 src-tauri（Rust 通过 libloading 加载它）。
# 用法：pnpm backend:sync
# 说明：当前是 FFI 架构，不再是 sidecar exe；build.rs 也会做同样的拷贝，
#       这个脚本主要给“只重建了 C++、没重建 Rust”的场景手动兜底。
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot   # ui/

# DLL 候选路径：本仓库构建产物优先，兼容多种 preset
$candidates = @(
    (Join-Path $root '..\native\out\bottlemusic-check\EchoCAPI.dll'),
    (Join-Path $root '..\native\out\bottlemusic-release\EchoCAPI.dll')
)

$src = $null
foreach ($c in $candidates) { if (Test-Path $c) { $src = $c; break } }

if (-not $src) {
    Write-Warning 'EchoCAPI.dll not found. Tried:'
    foreach ($c in $candidates) { Write-Warning "  $c" }
    Write-Warning "Skipping sync. Run 'pnpm backend:build' or build native/ via CMake manually."
    exit 0
}

# Rust 在 debug 跑时从 target/debug/ 加载 DLL（见 lib.rs 的 exe_dir 候选），
# 但开发期最稳的是直接放进 src-tauri 便于 build.rs 与 IDE 一致引用。
$dstDir = Join-Path $root 'src-tauri\libs'
New-Item -ItemType Directory -Force -Path $dstDir | Out-Null
$dst = Join-Path $dstDir 'EchoCAPI.dll'

if ((Test-Path $dst) -and ((Get-Item $src).LastWriteTime -eq (Get-Item $dst).LastWriteTime)) {
    Write-Host '[backend:sync] already up-to-date'
    exit 0
}

Copy-Item -Path $src -Destination $dst -Force
$size = (Get-Item $dst).Length / 1MB
Write-Host ('[backend:sync] {0:N2} MB <- {1}' -f $size, $src)
