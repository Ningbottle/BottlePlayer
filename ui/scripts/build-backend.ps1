# 编 C++ EchoCompatServer 并同步到 sidecar 位置。
# 用法：pnpm backend:build
$ErrorActionPreference = 'Stop'

$root      = Split-Path -Parent $PSScriptRoot                   # ui/
$nativeDir = Resolve-Path "$root\..\native"
$buildDir  = "$nativeDir\out\bottlemusic-check"

if (-not (Test-Path "$buildDir\CMakeCache.txt")) {
  Write-Host "[backend:build] configuring CMake (bottlemusic-check preset)..."
  cmake -S "$nativeDir" --preset bottlemusic-check
  if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
}

Write-Host "[backend:build] building EchoCompatServer..."
$vsdev = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat"
if (Test-Path $vsdev) {
  cmd /c "`"$vsdev`" -arch=x64 -host_arch=x64 2>nul && cmake --build `"$buildDir`" --config Debug --target EchoCompatServer"
} else {
  cmake --build "$buildDir" --config Debug --target EchoCompatServer
}
if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }

# 同步到 sidecar 位置
& (Join-Path $PSScriptRoot 'sync-backend.ps1')
