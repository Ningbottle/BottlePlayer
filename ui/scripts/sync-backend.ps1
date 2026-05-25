# 把 EchoCompatServer.exe 同步到 src-tauri/binaries/，加 target-triple 后缀。
# 用法：pnpm backend:sync
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot   # ui/

# 候选路径：worktree 自带 build 优先，回退到主 worktree 标准路径
$candidates = @(
  (Join-Path $root '..\native\out\bottlemusic-check\EchoCompatServer.exe'),
  'D:\KuGouMusic\EchoMusic-main\native\out\bottlemusic-check\EchoCompatServer.exe'
)

$src = $null
foreach ($c in $candidates) {
  if (Test-Path $c) { $src = $c; break }
}

if (-not $src) {
  Write-Warning "EchoCompatServer.exe not found. Tried:"
  foreach ($c in $candidates) { Write-Warning "  $c" }
  Write-Warning "Skipping sync. Run 'pnpm backend:build' or do a CMake build manually."
  exit 0
}

$triple = (rustc -vV | Select-String 'host:' | ForEach-Object { ($_.ToString().Split(':')[1]).Trim() })
$dstDir = Join-Path $root 'src-tauri\binaries'
New-Item -ItemType Directory -Force -Path $dstDir | Out-Null
$dst = Join-Path $dstDir "EchoCompatServer-$triple.exe"

if ((Test-Path $dst) -and ((Get-Item $src).LastWriteTime -eq (Get-Item $dst).LastWriteTime)) {
  Write-Host "[backend:sync] already up-to-date"
  exit 0
}

Copy-Item -Path $src -Destination $dst -Force
$size = (Get-Item $dst).Length / 1MB
Write-Host ("[backend:sync] {0:N2} MB <- {1}" -f $size, $src)
