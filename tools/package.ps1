param(
    [Parameter(Mandatory=$true)][string]$QtRoot,
    [string]$BuildDirectory = 'output/qt-build',
    [string]$Version = '1.1.0-qt-preview',
    [string]$PackageDirectory = 'output/packages'
)
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^[A-Za-z0-9._-]+$') { throw 'Invalid package version' }
$repoRoot = Split-Path $PSScriptRoot -Parent
$buildRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDirectory))
$packageRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot $PackageDirectory))
$name = "BottleMusicQt-$Version-x64"
$stage = Join-Path $packageRoot $name
if (Test-Path -LiteralPath $stage) { throw "Package directory exists; choose another PackageDirectory: $stage" }
$deploy = Join-Path $QtRoot 'bin/windeployqt.exe'
foreach ($required in @($deploy, (Join-Path $buildRoot 'BottleMusicQt.exe'), (Join-Path $buildRoot 'EchoCAPI.dll'), (Join-Path $buildRoot 'sqlite3.dll'))) {
    if (!(Test-Path -LiteralPath $required)) { throw "Missing package dependency: $required" }
}
New-Item -ItemType Directory -Force -Path $stage | Out-Null
foreach ($file in @('BottleMusicQt.exe', 'EchoCAPI.dll', 'sqlite3.dll', 'BottleMusic.ico')) {
    Copy-Item -LiteralPath (Join-Path $buildRoot $file) -Destination $stage
}
foreach ($directory in @('fonts', 'licenses')) {
    Copy-Item -LiteralPath (Join-Path $buildRoot $directory) -Destination $stage -Recurse
}
Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE') -Destination (Join-Path $stage 'licenses/BottleMusic-LICENSE')
& $deploy --release --qmldir (Join-Path $repoRoot 'desktop/qt/qml') --dir $stage (Join-Path $stage 'BottleMusicQt.exe')
if ($LASTEXITCODE -ne 0) { throw "Qt runtime deployment failed ($LASTEXITCODE)" }
$archive = Join-Path $packageRoot "$name.zip"
if (Test-Path -LiteralPath $archive) { throw "Archive already exists: $archive" }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $archive
$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$archive.sha256", "$hash  $name.zip`n", [Text.UTF8Encoding]::new($false))
Write-Output "Created $archive"
