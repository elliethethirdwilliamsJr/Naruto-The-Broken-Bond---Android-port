[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$DeviceSerial)
$ErrorActionPreference = 'Stop'
$bondRoot = Split-Path -Parent $PSScriptRoot
$bondAdb = (Join-Path ([Environment]::GetFolderPath('UserProfile')) 'Android/platform-tools/adb.exe')
$bondRemote = '/data/local/tmp/broken_bond_renderer_probe'
Push-Location $bondRoot
try {
    New-Item -ItemType Directory -Force build/android-renderer | Out-Null
    & $bondAdb -s $DeviceSerial get-state
    if ($LASTEXITCODE -ne 0) { throw 'The requested phone is unavailable.' }
    & $bondAdb -s $DeviceSerial shell mkdir -p $bondRemote
    if ($LASTEXITCODE -ne 0) { throw 'Cannot create the owned renderer diagnostic directory.' }
    foreach ($bondFile in @('build/android-runtime/android_renderer_probe', 'sdk/out/android-arm64/librexruntime.so',
            (Join-Path ([Environment]::GetFolderPath('UserProfile')) 'Android/ndk/28.2.13676358/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so'))) {
        & $bondAdb -s $DeviceSerial push $bondFile "$bondRemote/$([IO.Path]::GetFileName($bondFile))"
        if ($LASTEXITCODE -ne 0) { throw "Cannot transfer $bondFile." }
    }
    & $bondAdb -s $DeviceSerial shell rm -f "$bondRemote/result.jsonl"
    if ($LASTEXITCODE -ne 0) { throw 'Cannot clear the owned renderer result.' }
    & $bondAdb -s $DeviceSerial shell chmod 700 "$bondRemote/android_renderer_probe"
    if ($LASTEXITCODE -ne 0) { throw 'Cannot make the renderer diagnostic executable.' }
    & $bondAdb -s $DeviceSerial shell "cd $bondRemote && LD_LIBRARY_PATH=$bondRemote timeout 120 ./android_renderer_probe result.jsonl" *> build/android-renderer/device.log
    $bondExit = $LASTEXITCODE
    Get-Content build/android-renderer/device.log -Tail 24
    & $bondAdb -s $DeviceSerial pull "$bondRemote/result.jsonl" build/android-renderer/device-result.jsonl
    if ($LASTEXITCODE -ne 0) { throw 'Renderer diagnostic did not write a fresh result.' }
    Get-Content build/android-renderer/device-result.jsonl
    if ($bondExit -ne 0) { throw "Renderer diagnostic failed with exit $bondExit; evidence retained." }
    $bondChecks = @(Get-Content build/android-renderer/device-result.jsonl | ForEach-Object { $_ | ConvertFrom-Json })
    if ($bondChecks[-1].kind -ne 'verdict' -or !$bondChecks[-1].passed -or
            @($bondChecks | Where-Object { $_.kind -ne 'verdict' -and !$_.ok }).Count -gt 0) {
        throw 'The production renderer checks did not all pass.'
    }
} finally { Pop-Location }
