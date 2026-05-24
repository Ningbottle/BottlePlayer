param(
  [string]$BuildDirectory = "C:\Users\ICe\.codex\memories\bottlemusic-check-codex",
  [string]$Configuration = "Release",
  [string]$Version = "0.1.0",
  [string]$OutputDirectory = "D:\KuGouMusic\EchoMusic-main\native\dist",
  [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$nativeRoot = Join-Path $repoRoot "native"
$buildDir = $BuildDirectory
$packageName = "BottleMusic-$Version-win-x64-$Configuration"
$stageDir = Join-Path $OutputDirectory $packageName
$zipPath = Join-Path $OutputDirectory "$packageName.zip"

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
if (Test-Path -LiteralPath $stageDir) {
  Remove-Item -LiteralPath $stageDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $stageDir | Out-Null

if (-not $SkipBuild) {
  $vsDevCmd = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat"
  $cmake = "D:\QT\Tools\CMake_64\bin\cmake.exe"
  $cmd = "call `"$vsDevCmd`" -arch=x64 -host_arch=x64 && `"$cmake`" --build `"$buildDir`" --config $Configuration --target EchoWin32 EchoCompatServer EchoNativeSmokeTests"
  cmd.exe /d /s /c $cmd
  if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE"
  }
}

$binaries = @(
  "EchoWin32.exe",
  "EchoCompatServer.exe",
  "EchoNativeSmokeTests.exe"
)

foreach ($binary in $binaries) {
  $source = Join-Path $buildDir $binary
  if (-not (Test-Path -LiteralPath $source)) {
    throw "Required binary not found: $source"
  }
  Copy-Item -LiteralPath $source -Destination $stageDir
}

$docsDir = Join-Path $stageDir "docs"
New-Item -ItemType Directory -Force -Path $docsDir | Out-Null
Copy-Item -LiteralPath (Join-Path $repoRoot "docs\README.zh-CN.md") -Destination $docsDir
Copy-Item -LiteralPath (Join-Path $repoRoot "docs\RELEASE_ENGINEERING.zh-CN.md") -Destination $docsDir -ErrorAction SilentlyContinue
Copy-Item -LiteralPath (Join-Path $repoRoot "docs\MEMORY_BUDGET.zh-CN.md") -Destination $docsDir -ErrorAction SilentlyContinue

$assetsSource = Join-Path $repoRoot "assets"
if (Test-Path -LiteralPath $assetsSource) {
  Copy-Item -LiteralPath $assetsSource -Destination (Join-Path $stageDir "assets") -Recurse
}

$manifest = [pscustomobject]@{
  product = "BottleMusic"
  version = $Version
  platform = "win-x64"
  configuration = $Configuration
  built_at = (Get-Date).ToString("o")
  entrypoint = "EchoWin32.exe"
  signing = "unsigned-dev-package"
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 -Path (Join-Path $stageDir "package.json")

if (Test-Path -LiteralPath $zipPath) {
  Remove-Item -LiteralPath $zipPath -Force
}
Compress-Archive -Path (Join-Path $stageDir "*") -DestinationPath $zipPath -Force

[pscustomobject]@{
  Package = $zipPath
  Stage = $stageDir
  Version = $Version
  Configuration = $Configuration
}
