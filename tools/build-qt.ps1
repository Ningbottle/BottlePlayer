param(
    [Parameter(Mandatory=$true)][string]$QtRoot,
    [Parameter(Mandatory=$true)][string]$NativeDirectory,
    [string]$BuildDirectory = 'output/qt-cpp-build',
    [string]$ShaderToolsRoot = '',
    [string]$FontDirectory = '',
    [string[]]$Targets = @('all')
)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path $PSScriptRoot -Parent
$buildRoot=[IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDirectory))
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$installation){throw 'MSVC x64 tools were not found'}
$devcmd=Join-Path $installation 'Common7/Tools/VsDevCmd.bat'
if(!$ShaderToolsRoot){$ShaderToolsRoot=$QtRoot}
foreach($required in @((Join-Path $QtRoot 'lib/cmake/Qt6/Qt6Config.cmake'),(Join-Path $NativeDirectory 'EchoCAPI.dll'),(Join-Path $NativeDirectory 'sqlite3.dll'))){
    if(!(Test-Path -LiteralPath $required)){throw "Missing build dependency: $required"}
}
New-Item -ItemType Directory -Force -Path $buildRoot | Out-Null
# MSVC on this host has Chinese resources only. The UTF-8 console must be
# selected BEFORE CMake identifies /showIncludes, and retained for Ninja.
# A cached mojibake prefix silently disables header dependencies and can mix ABIs.
foreach($compilerFile in Get-ChildItem -LiteralPath (Join-Path $buildRoot 'CMakeFiles') -Filter CMakeCXXCompiler.cmake -Recurse -ErrorAction SilentlyContinue){
    if([IO.File]::ReadAllText($compilerFile.FullName).Contains('娉ㄦ剰')){throw 'This build cache has a broken MSVC include prefix. Choose a fresh BuildDirectory.'}
}
function BatchQuote([string]$value){if($value.Contains('"') -or $value.Contains("`n") -or $value.Contains("`r")){throw 'Invalid command argument'};return '"'+$value.Replace('%','%%')+'"'}
$arguments=@('-S',(Join-Path $repoRoot 'desktop/qt'),'-B',$buildRoot,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_TRY_COMPILE_CONFIGURATION=Release',"-DCMAKE_PREFIX_PATH=$QtRoot","-DECHO_NATIVE_DIR=$NativeDirectory","-DQt6ShaderTools_DIR=$ShaderToolsRoot/lib/cmake/Qt6ShaderTools","-DQt6ShaderToolsTools_DIR=$ShaderToolsRoot/lib/cmake/Qt6ShaderToolsTools",'-DCMAKE_CXX_FLAGS_RELEASE=/O2 /DNDEBUG /Z7')
if($FontDirectory){$arguments+="-DBOTTLE_FONT_DIR=$FontDirectory"}
$batch=@('@echo off','setlocal DisableDelayedExpansion',('call '+(BatchQuote $devcmd)+' -arch=x64 -host_arch=x64'),'if errorlevel 1 exit /b 1','chcp 65001 > nul','set "_CL_=/Z7"',('cmake '+(($arguments | ForEach-Object {BatchQuote $_}) -join ' ')),'if errorlevel 1 exit /b 1',('cmake --build '+(BatchQuote $buildRoot)+' --parallel 6 --target '+(($Targets|ForEach-Object {BatchQuote $_}) -join ' ')),'exit /b %errorlevel%')
$batchFile=Join-Path $buildRoot 'build-qt.cmd'
[IO.File]::WriteAllText($batchFile,($batch -join "`r`n"),[Text.UTF8Encoding]::new($false))
& cmd /d /c $batchFile
if($LASTEXITCODE -ne 0){throw "Qt build failed ($LASTEXITCODE)"}
