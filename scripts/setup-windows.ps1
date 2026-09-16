# Installs the native toolchain for DAW IA on Windows 10/11 (x64).
#
#   - Git
#   - Python 3.12 and uv (services/)
#   - Visual Studio 2022 Build Tools, "Desktop development with C++" workload
#     (MSVC, Windows SDK, CMake, Ninja)
#   - clang-format 18, same major version as CI (Ubuntu 24.04)
#
# Idempotent: already installed components are skipped.
#
# Usage (PowerShell 5.1 or 7):
#   powershell -ExecutionPolicy Bypass -File scripts\setup-windows.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\setup-windows.ps1 -DryRun
#
# Windows may show UAC prompts during installation. Accept them.

[CmdletBinding()]
param(
    # Print what would be installed, change nothing.
    [switch] $DryRun
)

$ErrorActionPreference = 'Stop'

$ClangFormatVersion = '18.1.8'
$VcWorkload = 'Microsoft.VisualStudio.Workload.VCTools'
$VsInstallerDir = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer'
$VsWhere = Join-Path $VsInstallerDir 'vswhere.exe'

function Write-Step([string] $Message) {
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Invoke-Action([string] $Description, [scriptblock] $Action) {
    if ($DryRun) {
        Write-Host "    [dry-run] $Description" -ForegroundColor Yellow
        return
    }
    Write-Host "    $Description"
    & $Action
}

function Update-SessionPath {
    $machine = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $user = [Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = "$machine;$user"
}

function Test-Command([string] $Name) {
    return [bool] (Get-Command $Name -ErrorAction SilentlyContinue)
}

function Install-WingetPackage([string] $Id, [string] $Command) {
    if ($Command -and (Test-Command $Command)) {
        Write-Host "    $Id already available ($Command found)"
        return
    }
    Invoke-Action "winget install $Id" {
        winget install --id $Id -e --silent --accept-source-agreements --accept-package-agreements
        # -1978335189 (0x8A15002B): already installed, no upgrade available.
        if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne -1978335189) {
            throw "winget install $Id failed with exit code $LASTEXITCODE"
        }
        Update-SessionPath
    }
}

function Get-VsBuildTools {
    if (-not (Test-Path $VsWhere)) { return $null }
    $json = & $VsWhere -products Microsoft.VisualStudio.Product.BuildTools -format json -all
    $instances = @($json | ConvertFrom-Json)
    if ($instances.Count -eq 0) { return $null }
    return $instances[0]
}

function Test-VcWorkload([string] $InstallPath) {
    $found = & $VsWhere -products * -requires $VcWorkload -property installationPath -all
    return @($found) -contains $InstallPath
}

# ---------------------------------------------------------------------------
Write-Step 'Checking prerequisites'
if (-not (Test-Command 'winget')) {
    throw 'winget not found. Install "App Installer" from the Microsoft Store, then run this script again.'
}
if ([Environment]::OSVersion.Version.Major -lt 10) {
    throw 'Windows 10 or later is required.'
}

# ---------------------------------------------------------------------------
Write-Step 'Git'
Install-WingetPackage -Id 'Git.Git' -Command 'git'

Write-Step 'Python 3.12'
Install-WingetPackage -Id 'Python.Python.3.12' -Command 'python'

Write-Step 'uv'
Install-WingetPackage -Id 'astral-sh.uv' -Command 'uv'

# ---------------------------------------------------------------------------
Write-Step 'Visual Studio 2022 Build Tools (C++ workload)'
$vs = Get-VsBuildTools
if ($null -eq $vs) {
    Invoke-Action 'winget install Microsoft.VisualStudio.2022.BuildTools' {
        $override = "--wait --passive --norestart --add $VcWorkload --includeRecommended"
        winget install --id Microsoft.VisualStudio.2022.BuildTools -e `
            --accept-source-agreements --accept-package-agreements --override $override
        if ($LASTEXITCODE -ne 0) {
            throw "Build Tools installation failed with exit code $LASTEXITCODE. See %TEMP%\dd_*.log."
        }
    }
}
elseif (-not $vs.isComplete -or -not (Test-VcWorkload $vs.installationPath)) {
    # Incomplete install (for example a failed download) or missing workload.
    Invoke-Action "complete Build Tools at $($vs.installationPath)" {
        $setup = Join-Path $VsInstallerDir 'setup.exe'
        $arguments = @(
            'modify', '--installPath', "`"$($vs.installationPath)`"",
            '--add', $VcWorkload, '--includeRecommended', '--passive', '--norestart'
        )
        $process = Start-Process -FilePath $setup -ArgumentList $arguments -Verb RunAs -Wait -PassThru
        if ($process.ExitCode -ne 0) {
            throw "Build Tools modify failed with exit code $($process.ExitCode). See %TEMP%\dd_*.log."
        }
    }
}
else {
    Write-Host "    Build Tools $($vs.installationVersion) with C++ workload already installed"
}

# ---------------------------------------------------------------------------
Write-Step "clang-format $ClangFormatVersion"
$currentClangFormat = $null
if (Test-Command 'clang-format') {
    $currentClangFormat = (clang-format --version) -replace '.*version\s+([0-9.]+).*', '$1'
}
if ($currentClangFormat -eq $ClangFormatVersion) {
    Write-Host "    clang-format $ClangFormatVersion already installed"
}
else {
    Invoke-Action "uv tool install clang-format==$ClangFormatVersion" {
        uv tool install --force "clang-format==$ClangFormatVersion"
        if ($LASTEXITCODE -ne 0) { throw "clang-format installation failed" }
        uv tool update-shell
        Update-SessionPath
    }
}

# ---------------------------------------------------------------------------
Write-Step 'Summary'
if ($DryRun) {
    Write-Host '    Dry run: nothing was changed.'
    exit 0
}

$vs = Get-VsBuildTools
$vcvars = Join-Path $vs.installationPath 'VC\Auxiliary\Build\vcvars64.bat'
$probe = "`"$vcvars`" >nul 2>&1 && cl 2>&1 | findstr /r /c:""[0-9][0-9]\.[0-9][0-9]\."" && cmake --version | findstr /c:""cmake version"" && ninja --version"
cmd /c $probe
git --version
python --version
uv --version
clang-format --version

Write-Host ''
Write-Host 'Done. Next steps:' -ForegroundColor Green
Write-Host '  1. Open "x64 Native Tools Command Prompt for VS 2022" from the Start menu.'
Write-Host '  2. cd /d <repository>'
Write-Host '  3. git submodule update --init external/JUCE external/tracktion_engine external/clap external/vst3sdk'
Write-Host '  4. git -C external/vst3sdk submodule update --init base pluginterfaces public.sdk cmake'
Write-Host '  5. cmake --preset windows-msvc && cmake --build --preset windows-msvc && ctest --preset windows-msvc'
