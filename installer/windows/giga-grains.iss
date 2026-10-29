; Adapted from sst-cmake's innosetup/installer.iss, under its MIT license:
;
;   MIT License
;
;   Copyright (c) 2025 Surge Synthesizer Team
;
;   Permission is hereby granted, free of charge, to any person obtaining a copy
;   of this software and associated documentation files (the "Software"), to deal
;   in the Software without restriction, including without limitation the rights
;   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
;   copies of the Software, and to permit persons to whom the Software is
;   furnished to do so, subject to the following conditions:
;
;   The above copyright notice and this permission notice shall be included in all
;   copies or substantial portions of the Software.
;
;   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
;   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
;   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
;   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
;   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
;   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
;   SOFTWARE.
;
; Giga Grains - granular instrument and effect
; SPDX-License-Identifier: AGPL-3.0-or-later
; Copyright 2026, the Giga Grains authors (see git history)
;
; The Windows installer (spec section 6.5), compiled by Inno Setup 6's ISCC.exe. The gg-installer
; target runs it (cmake/products.cmake):
;
;   ISCC.exe /DStagedAssets=<gg-products> /DLicenses=<gg-products\licenses> /DVersion=<X.Y.Z>
;            /DFullVersion=<v> /DOutDir=<dir> giga-grains.iss
;
;   StagedAssets  the products, as gg-products gathers them: Giga Grains.clap, Giga Grains.vst3
;                 (a folder bundle), Giga Grains.exe and Giga Grains FX.exe.
;   Licenses      the license set; its LICENSE (the AGPL) is the license page.
;   Version       the numeric version (the file's version resource).
;   FullVersion   the full version string, <v> (the version shown and the file name).
;   OutDir        where to write giga-grains-<v>-windows-x64.exe.
;
; Single-product, unlike the template: one CLAP and one VST3 bundle, each holding both plugins,
; the two standalone apps, and the license set, always installed. Every component is on by
; default. No wizard images and no data component (the template's Data); M2's factory content
; has its place reserved below (spec section 6.9). This file is ASCII only.

#ifndef StagedAssets
  #error StagedAssets is not defined: pass /DStagedAssets=<the gg-products folder>
#endif
#ifndef Licenses
  #error Licenses is not defined: pass /DLicenses=<the license set folder>
#endif
#ifndef Version
  #error Version is not defined: pass /DVersion=<X.Y.Z>
#endif
#ifndef FullVersion
  #error FullVersion is not defined: pass /DFullVersion=<the full version string>
#endif
#ifndef OutDir
  #error OutDir is not defined: pass /DOutDir=<the output folder>
#endif

; x64compatible needs Inno Setup 6.3 or later.
#if VER < EncodeVer(6,3,0)
  #error Inno Setup 6.3 or later is required to compile this script
#endif
#pragma message "Inno Setup " + DecodeVer(VER) + ": Giga Grains " + FullVersion

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Setup]
OutputDir={#OutDir}
OutputBaseFilename=giga-grains-{#FullVersion}-windows-x64
; A fixed GUID, generated once: never change it, or Windows sees a different product.
AppId={{4B7D66BA-D746-4B8A-AC5D-59DFBB8E4F53}
AppName=Giga Grains
AppPublisher=sethbc
AppPublisherURL=https://github.com/sethbc/giga-grains
AppSupportURL=https://github.com/sethbc/giga-grains
AppUpdatesURL=https://github.com/sethbc/giga-grains/releases
AppVersion={#FullVersion}
VersionInfoVersion={#Version}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
CloseApplicationsFilter=*.exe,*.vst3,*.clap
; {app} is this folder; the directory page is hidden, so the apps and licenses always go here.
DefaultDirName={autopf}\Giga Grains
DefaultGroupName=Giga Grains
DisableDirPage=yes
DisableProgramGroupPage=yes
LicenseFile={#Licenses}\LICENSE
Compression=lzma2/ultra64
SolidCompression=yes
LZMAUseSeparateProcess=yes
UninstallDisplayIcon={uninstallexe}
WizardStyle=modern

[Types]
Name: "full"; Description: "Full installation"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "clap"; Description: "Giga Grains CLAP (both plugins)"; Types: full custom
Name: "vst3"; Description: "Giga Grains VST3 (both plugins)"; Types: full custom
Name: "app"; Description: "Giga Grains app (standalone)"; Types: full custom
Name: "appfx"; Description: "Giga Grains FX app (standalone)"; Types: full custom
Name: "licenses"; Description: "Licenses and notices"; Types: full custom; Flags: fixed
; M2 (spec section 6.9): the factory content, installed only when the tree has it.
; Name: "content"; Description: "Factory content (samples and presets)"; Types: full custom

[InstallDelete]
; A bundle is a folder: an older version's files must not linger in the new one.
Type: filesandordirs; Name: "{commoncf}\VST3\Giga Grains.vst3"; Components: vst3

[Files]
Source: "{#StagedAssets}\Giga Grains.clap"; DestDir: "{commoncf}\CLAP"; Components: clap; Flags: ignoreversion
Source: "{#StagedAssets}\Giga Grains.vst3\*"; DestDir: "{commoncf}\VST3\Giga Grains.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StagedAssets}\Giga Grains.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "{#StagedAssets}\Giga Grains FX.exe"; DestDir: "{app}"; Components: appfx; Flags: ignoreversion
Source: "{#Licenses}\*"; DestDir: "{app}\licenses"; Components: licenses; Flags: ignoreversion
; M2 (spec section 6.9), with /DContent=<the content folder>:
; Source: "{#Content}\*"; DestDir: "{commonappdata}\sethbc\Giga Grains"; Components: content; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Giga Grains"; Filename: "{app}\Giga Grains.exe"; Components: app
Name: "{group}\Giga Grains FX"; Filename: "{app}\Giga Grains FX.exe"; Components: appfx
