# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Installs what a Windows build of Giga Grains needs, and optionally clones, builds and tests it:
#
#   .\scripts\bootstrap-windows.ps1 [-Verify] [-Clone DIR]
#
# With winget it installs whatever is missing of CMake (Kitware.CMake), Ninja
# (Ninja-build.Ninja), Git (Git.Git) and Visual Studio 2022 Build Tools with the C++ x64 tools,
# a Windows SDK and the C++ Clang (ClangCL) component. Each is reported as found or installed; a
# second run changes nothing.
#
#   -Verify      installs nothing: checks each (vswhere for Visual Studio, the PATH for the
#                rest) and exits 1 if any is missing. CI's Windows leg runs this on the hosted
#                image, which has Visual Studio already.
#   -Clone DIR   then clone the repository (with its submodules) into DIR, or update the clone
#                there, enter the developer shell (scripts\dev-shell-windows.ps1) and run
#                `cmake --preset windows-clangcl`, `cmake --build --preset windows-clangcl` and
#                `ctest --preset windows-clangcl`
#
# Exit status: 0 on success, 1 when a check, an install or a step fails, 2 for bad usage. Install
# the Visual Studio Build Tools from an elevated shell if winget asks for it.

param(
    [switch]$Verify,
    [string]$Clone = ''
)

$ErrorActionPreference = 'Stop'
$RepoUrl = if ($env:GG_REPO_URL) { $env:GG_REPO_URL } else { 'https://github.com/sethbc/giga-grains.git' }
$VsComponents = @(
    'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
    'Microsoft.VisualStudio.Component.Windows11SDK.22621',
    'Microsoft.VisualStudio.Component.VC.Llvm.Clang',
    'Microsoft.VisualStudio.Component.VC.Llvm.ClangToolset'
)

function Get-VsInstall {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $null }
    $path = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($path) { return ($path | Select-Object -First 1) }
    return $null
}

function Update-SessionPath {
    $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
}

function Test-Tool([string]$Name) {
    return [bool](Get-Command $Name -ErrorAction SilentlyContinue)
}

function Test-Ninja($Vs) {
    if (Test-Tool 'ninja') { return $true }
    # Visual Studio's CMake component carries one.
    return ($Vs -and (Test-Path (Join-Path $Vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe')))
}

function Test-WindowsSdk {
    $inc = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Include'
    return ((Test-Path $inc) -and [bool](Get-ChildItem $inc -Directory -ErrorAction SilentlyContinue | Select-Object -First 1))
}

function Test-Clang($Vs) {
    return ($Vs -and (Test-Path (Join-Path $Vs 'VC\Tools\Llvm\x64\bin\clang-cl.exe')))
}

function Install-WithWinget([string]$Id, [string[]]$ExtraArgs = @()) {
    Write-Host "installing: $Id"
    & winget install --id $Id -e --accept-source-agreements --accept-package-agreements @ExtraArgs
    if ($LASTEXITCODE -ne 0) { Write-Error "winget install $Id failed ($LASTEXITCODE)"; exit 1 }
}

$vs = Get-VsInstall
$status = @(
    @{ Name = 'CMake';  Ok = (Test-Tool 'cmake'); Id = 'Kitware.CMake' },
    @{ Name = 'Ninja';  Ok = (Test-Ninja $vs);    Id = 'Ninja-build.Ninja' },
    @{ Name = 'Git';    Ok = (Test-Tool 'git');   Id = 'Git.Git' }
)
$vsOk = [bool]$vs
$sdkOk = Test-WindowsSdk
$clangOk = Test-Clang $vs

foreach ($s in $status) {
    if ($s.Ok) { Write-Host "found: $($s.Name)" } else { Write-Host "missing: $($s.Name)" }
}
Write-Host $(if ($vsOk)    { "found: Visual Studio with the x64 C++ tools ($vs)" } else { 'missing: Visual Studio with the x64 C++ tools' })
Write-Host $(if ($sdkOk)   { 'found: a Windows SDK' } else { 'missing: a Windows SDK' })
Write-Host $(if ($clangOk) { 'found: the C++ Clang (ClangCL) component' } else { 'missing: the C++ Clang (ClangCL) component' })

$allOk = $vsOk -and $sdkOk -and $clangOk -and (-not ($status | Where-Object { -not $_.Ok }))

if ($Verify) {
    if ($allOk) { Write-Host 'bootstrap-windows: everything is present'; exit 0 }
    Write-Host 'bootstrap-windows: -Verify found something missing'
    exit 1
}

if (-not $allOk) {
    foreach ($s in $status) {
        if (-not $s.Ok) { Install-WithWinget $s.Id }
    }
    if (-not ($vsOk -and $sdkOk -and $clangOk)) {
        $add = ($VsComponents | ForEach-Object { "--add $_" }) -join ' '
        # Build Tools if no Visual Studio is installed; if one is, modify it in place.
        if ($vs) {
            Write-Host "adding the missing components to $vs"
            $setup = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\setup.exe'
            $modifyArgs = @('modify', '--installPath', $vs, '--passive', '--norestart', '--wait')
            foreach ($c in $VsComponents) { $modifyArgs += @('--add', $c) }
            & $setup @modifyArgs
            if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne 3010) { Write-Error "the Visual Studio modify failed ($LASTEXITCODE)"; exit 1 }
        } else {
            Install-WithWinget 'Microsoft.VisualStudio.2022.BuildTools' @('--override', "--passive --norestart --wait $add")
        }
    }
    Update-SessionPath
    $vs = Get-VsInstall
    if (-not ((Test-Tool 'cmake') -and (Test-Ninja $vs) -and (Test-Tool 'git') -and $vs -and (Test-Clang $vs))) {
        Write-Error 'bootstrap-windows: something is still missing after the installs; open a new shell and run this again'
        exit 1
    }
} else {
    Write-Host 'nothing to install'
}

if ($Clone) {
    Write-Host "== Clone, build and test: $Clone"
    if (Test-Path (Join-Path $Clone '.git')) {
        Write-Host 'found a clone: updating its submodules'
        & git -C $Clone submodule update --init --recursive
    } else {
        & git clone --recurse-submodules $RepoUrl $Clone
    }
    if ($LASTEXITCODE -ne 0) { Write-Error 'git failed'; exit 1 }
    Set-Location $Clone
    . (Join-Path $Clone 'scripts\dev-shell-windows.ps1')
    foreach ($step in @(
            @('cmake', '--preset', 'windows-clangcl'),
            @('cmake', '--build', '--preset', 'windows-clangcl'),
            @('ctest', '--preset', 'windows-clangcl'))) {
        & $step[0] $step[1..($step.Length - 1)]
        if ($LASTEXITCODE -ne 0) { Write-Error "$($step -join ' ') failed ($LASTEXITCODE)"; exit 1 }
    }
}
Write-Host 'bootstrap-windows: done'
