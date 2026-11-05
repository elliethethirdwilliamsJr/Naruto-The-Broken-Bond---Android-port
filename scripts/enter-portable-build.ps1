# Local, process-scoped toolchain. Does not modify the system PATH or registry.
$ErrorActionPreference = 'Stop'
$bondResearch = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$bondTools = Join-Path $bondResearch 'tools'
$bondMsvc = Join-Path $bondTools 'msvc'
$bondCompiler = Join-Path $bondTools 'llvm/clang+llvm-19.1.5-x86_64-pc-windows-msvc/bin'
$bondCmake = Join-Path $bondTools 'cmake-4.2.0-windows-x86_64/bin'
$bondNinja = (Join-Path ([Environment]::GetFolderPath('UserProfile')) 'Android/cmake/3.22.1/bin')
$bondPython = Split-Path -Parent (Get-Command python -ErrorAction Stop).Source
$bondVcVersion = (Get-ChildItem -LiteralPath (Join-Path $bondMsvc 'VC/Tools/MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1).Name
$bondSdkVersion = (Get-ChildItem -LiteralPath (Join-Path $bondMsvc 'Windows Kits/10/Include') -Directory | Sort-Object Name -Descending | Select-Object -First 1).Name
$bondVc = Join-Path $bondMsvc "VC/Tools/MSVC/$bondVcVersion"
$bondKit = Join-Path $bondMsvc 'Windows Kits/10'
foreach ($bondRequired in @("$bondCompiler/clang.exe", "$bondCmake/cmake.exe", "$bondNinja/ninja.exe", "$bondVc/include/vector", "$bondKit/Include/$bondSdkVersion/um/windows.h")) {
    if (!(Test-Path -LiteralPath $bondRequired)) { throw "Portable build prerequisite missing: $bondRequired" }
}
$env:VSCMD_ARG_HOST_ARCH = 'x64'
$env:VSCMD_ARG_TGT_ARCH = 'x64'
$env:VCToolsVersion = $bondVcVersion
$env:VCToolsInstallDir = "$bondVc/"
$env:WindowsSDKVersion = "$bondSdkVersion\"
$env:WindowsSdkDir = "$bondKit/"
$env:WindowsSdkBinPath = "$bondKit/bin/"
$env:INCLUDE = "$bondVc/include;$bondKit/Include/$bondSdkVersion/ucrt;$bondKit/Include/$bondSdkVersion/shared;$bondKit/Include/$bondSdkVersion/um;$bondKit/Include/$bondSdkVersion/winrt;$bondKit/Include/$bondSdkVersion/cppwinrt"
$env:LIB = "$bondVc/lib/x64;$bondKit/Lib/$bondSdkVersion/ucrt/x64;$bondKit/Lib/$bondSdkVersion/um/x64"
$env:PATH = "$bondCompiler;$bondCmake;$bondNinja;$bondPython;$bondVc/bin/Hostx64/x64;$bondKit/bin/$bondSdkVersion/x64;$env:PATH"
$env:CMAKE_BUILD_PARALLEL_LEVEL = '1'
$env:CTEST_PARALLEL_LEVEL = '1'
Write-Host "Portable LLVM 19.1.5 / MSVC $bondVcVersion / SDK $bondSdkVersion; one build job."
