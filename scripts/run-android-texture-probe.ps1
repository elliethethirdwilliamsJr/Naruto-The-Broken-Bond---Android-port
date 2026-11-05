[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$DeviceSerial,
    [string]$NdkRoot = (Join-Path ([Environment]::GetFolderPath('UserProfile')) 'Android/ndk/28.2.13676358')
)

$ErrorActionPreference = 'Stop'
$bondRoot = Split-Path -Parent $PSScriptRoot
$bondAdb = Join-Path (Split-Path -Parent (Split-Path -Parent $NdkRoot)) 'platform-tools/adb.exe'
$bondRemote = '/data/local/tmp/broken_bond_texture_probe'
$bondCxx = Join-Path $NdkRoot 'toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so'
Push-Location $bondRoot
try {
    $bondObjects = @(Get-ChildItem -LiteralPath build/android-runtime/CMakeFiles/android_texture_probe.dir -Recurse -Filter '*.o')
    foreach ($bondSource in @('tools/android-runtime/texture_probe.cpp', 'src/gpu/textures/formats.cpp')) {
        $bondFile = Get-Item -LiteralPath $bondSource
        $bondObject = @($bondObjects | Where-Object { $_.Name -eq ($bondFile.Name + '.o') })
        if ($bondObject.Count -ne 1 -or $bondFile.LastWriteTimeUtc -gt $bondObject[0].LastWriteTimeUtc) {
            throw "Missing or stale object for $bondSource. Build the TextureProbe stage before deployment."
        }
    }
    if ((Get-Item tools/android-runtime/texture_probe.comp).LastWriteTimeUtc -gt
            (Get-Item build/android-runtime/texture_probe.spv).LastWriteTimeUtc) { throw 'Rebuild the diagnostic shader.' }
    & $bondAdb -s $DeviceSerial get-state
    if ($LASTEXITCODE -ne 0) { throw 'The requested USB-debugging device is unavailable.' }
    & $bondAdb -s $DeviceSerial shell mkdir -p $bondRemote
    if ($LASTEXITCODE -ne 0) { throw 'Cannot create the owned texture diagnostic directory.' }
    foreach ($bondFile in @('build/android-runtime/android_texture_probe', 'build/android-runtime/texture_probe.spv', $bondCxx)) {
        & $bondAdb -s $DeviceSerial push $bondFile "$bondRemote/$([IO.Path]::GetFileName($bondFile))"
        if ($LASTEXITCODE -ne 0) { throw "Cannot transfer $bondFile." }
    }
    & $bondAdb -s $DeviceSerial shell rm -f "$bondRemote/texture-result.json"
    if ($LASTEXITCODE -ne 0) { throw 'Cannot clear the owned previous result file.' }
    & $bondAdb -s $DeviceSerial shell chmod 700 "$bondRemote/android_texture_probe"
    if ($LASTEXITCODE -ne 0) { throw 'Cannot make the diagnostic executable.' }
    & $bondAdb -s $DeviceSerial shell "cd $bondRemote && LD_LIBRARY_PATH=$bondRemote timeout 120 ./android_texture_probe $bondRemote" `
        *> build/android-texture-device.log
    $bondExit = $LASTEXITCODE
    Get-Content build/android-texture-device.log -Tail 35
    if ($bondExit -ne 0) { throw "Texture diagnostic failed with exit $bondExit; see build/android-texture-device.log." }
    & $bondAdb -s $DeviceSerial pull "$bondRemote/texture-result.json" build/android-texture-device-result.json
    if ($LASTEXITCODE -ne 0) { throw 'The texture diagnostic did not produce a fresh result file.' }
    $bondResult = Get-Content build/android-texture-device-result.json -Raw | ConvertFrom-Json
    if ($bondResult.formats_tested -ne 4 -or $bondResult.images_tested -ne 8 -or
            $bondResult.subresources_tested -ne 108 -or $bondResult.sampled_values_checked -ne 17664 -or
            $bondResult.policy_and_rejection_checks -ne 16 -or $bondResult.game_renderer_started -or
            $bondResult.guest_entry_executed) { throw 'Incomplete texture diagnostic result.' }
    $bondResult | ConvertTo-Json -Compress
} finally {
    Pop-Location
}
