param(
    [string]$BuildDirectory = 'output/native-build',
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')][string]$BuildType = 'Release'
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$nativeBuild = [IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDirectory))
$nativeDeps = Join-Path $repoRoot 'native/vcpkg_installed/x64-windows'
foreach ($required in @('share/nlohmann_json/nlohmann_jsonConfig.cmake', 'bin/sqlite3.dll')) {
    if (!(Test-Path -LiteralPath (Join-Path $nativeDeps $required))) { throw "Install native/vcpkg.json dependencies first: $required" }
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$installation) { throw 'MSVC x64 tools were not found' }
$devcmd = Join-Path $installation 'Common7/Tools/VsDevCmd.bat'
New-Item -ItemType Directory -Force -Path $nativeBuild | Out-Null
foreach ($compilerFile in Get-ChildItem -LiteralPath (Join-Path $nativeBuild 'CMakeFiles') -Filter CMakeCXXCompiler.cmake -Recurse -ErrorAction SilentlyContinue) {
    if ([IO.File]::ReadAllText($compilerFile.FullName).Contains('娉ㄦ剰')) { throw 'Broken MSVC include prefix in cache. Choose a fresh BuildDirectory.' }
}
function BatchQuote([string]$value) {
    if ($value.Contains('"') -or $value.Contains("`n") -or $value.Contains("`r")) { throw 'Invalid command argument' }
    return '"' + $value.Replace('%', '%%') + '"'
}
$arguments = @('-S', (Join-Path $repoRoot 'native'), '-B', $nativeBuild, '-G', 'Ninja', "-DCMAKE_BUILD_TYPE=$BuildType", "-DCMAKE_TRY_COMPILE_CONFIGURATION=$BuildType", '-DECHO_NATIVE_BUILD_TESTS=ON', '-DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON', '-DCMAKE_DISABLE_FIND_PACKAGE_Qt5=ON')
if ($BuildType -eq 'Release') { $arguments += '-DCMAKE_CXX_FLAGS_RELEASE=/O2 /DNDEBUG /Z7' }
$batch = @('@echo off', 'setlocal DisableDelayedExpansion', ('call ' + (BatchQuote $devcmd) + ' -arch=x64 -host_arch=x64'), 'if errorlevel 1 exit /b 1', 'chcp 65001 > nul', 'set "_CL_=/Z7"', ('cmake ' + (($arguments | ForEach-Object { BatchQuote $_ }) -join ' ')), 'if errorlevel 1 exit /b 1', ('cmake --build ' + (BatchQuote $nativeBuild) + ' --parallel 6'), 'exit /b %errorlevel%')
$batchFile = Join-Path $nativeBuild 'build-native.cmd'
[IO.File]::WriteAllText($batchFile, ($batch -join "`r`n"), [Text.UTF8Encoding]::new($false))
& cmd /d /c $batchFile
if ($LASTEXITCODE -ne 0) { throw "Native build failed ($LASTEXITCODE)" }
Copy-Item -LiteralPath (Join-Path $nativeDeps 'bin/sqlite3.dll') -Destination $nativeBuild
