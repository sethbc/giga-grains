# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Enters Visual Studio 2022's x64 developer shell in this PowerShell session and puts Visual
# Studio's own LLVM (VC\Tools\Llvm\x64\bin) first on the PATH, so `clang-cl` is Visual Studio's,
# as the windows-clangcl preset needs (INTENT §23 #76). Dot-source it, so the environment stays
# in your session:
#
#   . .\scripts\dev-shell-windows.ps1
#   cmake --preset windows-clangcl
#
# It stops with an error when Visual Studio lacks the x64 C++ tools or the C++ Clang component
# (scripts\bootstrap-windows.ps1 installs both).

$gg_prevEap = $ErrorActionPreference
$ErrorActionPreference = 'Stop'
try {
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found: install Visual Studio 2022 or its Build Tools (scripts\bootstrap-windows.ps1)" }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw "no Visual Studio with the x64 C++ tools (scripts\bootstrap-windows.ps1 installs them)" }
$llvm = Join-Path $vs 'VC\Tools\Llvm\x64\bin'
if (-not (Test-Path (Join-Path $llvm 'clang-cl.exe'))) { throw "no clang-cl in $llvm: add the 'C++ Clang tools for Windows' component" }

Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$env:Path = "$llvm;$env:Path"
& (Join-Path $llvm 'clang-cl.exe') --version
} finally { $ErrorActionPreference = $gg_prevEap }
