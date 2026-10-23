# Builds iXpat for Windows from a plain PowerShell prompt.
#   .\build.ps1                       x64 debug
#   .\build.ps1 -Preset x64-release   x64 release
#   .\build.ps1 -Test                 build, then run the unit tests
#   .\build.ps1 -Preset arm64-release ARM64 cross build (needs the VS ARM64 build tools component)
# Finds Visual Studio (Build Tools or any edition), enters its developer environment, and uses the
# CMake and Ninja that ship with it, so nothing else has to be installed or on PATH.
param(
    [ValidateSet('x64-debug', 'x64-release', 'arm64-release')] [string] $Preset = 'x64-debug',
    [switch] $Test,
    [switch] $Clean
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

$installer = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer'
$vswhere = Join-Path $installer 'vswhere.exe'
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found at $vswhere; install Visual Studio 2022 Build Tools with the C++ workload" }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'No Visual Studio installation with the C++ x64 build tools was found' }

$arch = if ($Preset -like 'arm64*') { 'amd64_arm64' } else { 'x64' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvarsall.bat'
$cmakeBin = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
$ninjaBin = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
foreach ($p in @($vcvars, (Join-Path $cmakeBin 'cmake.exe'), (Join-Path $ninjaBin 'ninja.exe'))) {
    if (-not (Test-Path $p)) { throw "Missing $p (install the 'C++ CMake tools for Windows' component)" }
}

if ($Clean) { Remove-Item -Recurse -Force (Join-Path $root "build\$Preset") -ErrorAction SilentlyContinue }

$steps = "cmake --preset $Preset && cmake --build --preset $Preset"
if ($Test) {
    if ($Preset -like 'arm64*') { throw 'Tests run on the host architecture only; use an x64 preset with -Test' }
    $steps += " && ctest --preset $Preset"
}
# vcvarsall.bat looks for vswhere on PATH; cmd.exe here may not search the current directory.
$cmd = "set `"PATH=$installer;$cmakeBin;$ninjaBin;%PATH%`" && call `"$vcvars`" $arch >nul && cd /d `"$root`" && $steps"
& "$env:SystemRoot\System32\cmd.exe" /d /c $cmd
exit $LASTEXITCODE
