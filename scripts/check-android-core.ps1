[CmdletBinding()]
param(
    [string]$NdkRoot = (Join-Path ([Environment]::GetFolderPath('UserProfile')) 'Android/ndk/28.2.13676358'),
    [string]$DeviceSerial = ''
)

# An ARM64 compile probe, not an APK or a linked/working Android engine.
$ErrorActionPreference = 'Stop'
$bondRoot = Split-Path -Parent $PSScriptRoot
$bondCompiler = Join-Path $NdkRoot 'toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe'
if (!(Test-Path -LiteralPath $bondCompiler)) { throw 'Android NDK compiler not found.' }
Push-Location $bondRoot
try {
    $bondOut = Join-Path $bondRoot 'build/android-core-probe'
    New-Item -ItemType Directory -Path $bondOut -Force | Out-Null
    $bondFlags = @('--target=aarch64-linux-android28', '-std=c++23', '-O0', '-c',
        '-DSPDLOG_COMPILED_LIB', '-DSPDLOG_FMT_EXTERNAL', '-Isdk/include', '-Isdk',
        '-Ibuild/sdk/include', '-Isdk/thirdparty/fmt/include', '-Isdk/thirdparty/spdlog/include',
        '-Isdk/thirdparty/simde', '-Isdk/thirdparty/tomlplusplus/include',
        '-Isdk/thirdparty/utfcpp/source', '-Igenerated/default')
    $bondSample = Get-ChildItem -LiteralPath generated/default -Filter 'broken_bond_recomp.*.cpp' |
        Sort-Object Length, Name | Select-Object -First 1
    if (!$bondSample) { throw 'Run original-game code generation before this probe.' }
    $bondSources = @('sdk/src/core/threading_posix.cpp', 'sdk/src/core/memory_posix.cpp', $bondSample.FullName)
    $bondObjects = @()
    foreach ($bondSource in $bondSources) {
        $bondStem = [IO.Path]::GetFileNameWithoutExtension($bondSource)
        $bondObject = Join-Path $bondOut "$bondStem.o"
        $bondLog = Join-Path $bondOut "$bondStem.log"
        & $bondCompiler @bondFlags $bondSource -o $bondObject *> $bondLog
        if ($LASTEXITCODE -ne 0) {
            Get-Content -LiteralPath $bondLog -Tail 35
            throw "Android ARM64 object compilation failed: $bondSource"
        }
        $bondObjects += @{ source = $bondSource; object = $bondObject; bytes = (Get-Item -LiteralPath $bondObject).Length }
        Write-Host "ARM64 object compiled: $bondStem"
    }
    $bondDiagnostic = Join-Path $bondOut 'core_probe'
    $bondLinkFlags = $bondFlags | Where-Object { $_ -ne '-c' }
    & $bondCompiler @bondLinkFlags -static-libstdc++ tools/android-core-probe/main.cpp `
        -o $bondDiagnostic *> (Join-Path $bondOut 'core-probe-build.log')
    if ($LASTEXITCODE -ne 0) { throw "Android standalone diagnostic link failed; see $bondOut/core-probe-build.log" }
    $bondResult = @{ target = 'aarch64-linux-android28'; ndk = $NdkRoot; objects = $bondObjects;
        engine_linked = $false; diagnostic_linked = $true; game_launched = $false; apk_built = $false }
    if ($DeviceSerial) {
        $bondAdb = Join-Path (Split-Path -Parent (Split-Path -Parent $NdkRoot)) 'platform-tools/adb.exe'
        if (!(Test-Path -LiteralPath $bondAdb)) { throw 'ADB executable not found beside the NDK.' }
        $bondRemote = '/data/local/tmp/broken_bond_port_probe/core_probe'
        & $bondAdb -s $DeviceSerial shell mkdir -p /data/local/tmp/broken_bond_port_probe
        if ($LASTEXITCODE -ne 0) { throw 'Cannot create the Android diagnostic directory.' }
        & $bondAdb -s $DeviceSerial push $bondDiagnostic $bondRemote
        if ($LASTEXITCODE -ne 0) { throw 'Cannot transfer the Android diagnostic.' }
        & $bondAdb -s $DeviceSerial shell chmod 700 $bondRemote
        if ($LASTEXITCODE -ne 0) { throw 'Cannot make the Android diagnostic executable.' }
        & $bondAdb -s $DeviceSerial shell $bondRemote > (Join-Path $bondOut 'device-result.json') `
            2> (Join-Path $bondOut 'device-stderr.log')
        if ($LASTEXITCODE -ne 0) { throw "Android diagnostic failed; see $bondOut/device-stderr.log" }
        $bondResult.device_diagnostic = Get-Content -LiteralPath (Join-Path $bondOut 'device-result.json') -Raw | ConvertFrom-Json
        Write-Host ($bondResult.device_diagnostic | ConvertTo-Json -Compress)
    }
    $bondResult |
        ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $bondOut 'result.json') -Encoding utf8
} finally {
    Pop-Location
}
