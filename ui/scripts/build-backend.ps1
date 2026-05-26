# 缂?C++ EchoCompatServer 骞跺悓姝ュ埌 sidecar 浣嶇疆銆?
# 鐢ㄦ硶锛歱npm backend:build
$ErrorActionPreference = 'Stop'

$root      = Split-Path -Parent $PSScriptRoot                   # ui/
$nativeDir = Resolve-Path "$root\..\native"
$buildDir  = "$nativeDir\out\bottlemusic-check"

if (-not (Test-Path "$buildDir\CMakeCache.txt")) {
  Write-Host "[backend:build] configuring CMake (bottlemusic-check preset)..."
  $vsdev = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat"
  if (Test-Path $vsdev) {
    cmd /c ""$vsdev" -arch=x64 -host_arch=x64 2>nul && cmake -S "$nativeDir" --preset bottlemusic-check"
  } else {
    cmake -S "$nativeDir" --preset bottlemusic-check
  }
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

# 鍚屾鍒?sidecar 浣嶇疆
& (Join-Path $PSScriptRoot 'sync-backend.ps1')

