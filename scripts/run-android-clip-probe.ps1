[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$DeviceSerial,
    [string]$NdkRoot = (Join-Path ([Environment]::GetFolderPath('UserProfile')) 'Android/ndk/28.2.13676358')
)
$ErrorActionPreference = 'Stop'
$bondRoot = Split-Path -Parent $PSScriptRoot
$bondAdb = Join-Path (Split-Path -Parent (Split-Path -Parent $NdkRoot)) 'platform-tools/adb.exe'
$bondRemote = '/data/local/tmp/broken_bond_clip_probe'
$bondCxx = Join-Path $NdkRoot 'toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so'
Push-Location $bondRoot
try {
    $bondSource = Get-Item tools/android-runtime/clip_probe.cpp
    $bondObject = Get-Item build/android-runtime/CMakeFiles/android_clip_probe.dir/clip_probe.cpp.o
    if ($bondSource.LastWriteTimeUtc -gt $bondObject.LastWriteTimeUtc) { throw 'Build the current ClipProbe stage before deployment.' }
    $bondShaders = @{
        'clip_probe.vert.spv' = 'tools/android-runtime/clip_probe.hlsl'
        'clip_probe.frag.spv' = 'tools/android-runtime/clip_probe.hlsl'
        'clip_planes_gs.spv' = 'src/gpu/backend/clip_planes.hlsl'
    }
    foreach ($bondName in $bondShaders.Keys) {
        if ((Get-Item -LiteralPath $bondShaders[$bondName]).LastWriteTimeUtc -gt
                (Get-Item -LiteralPath "build/android-runtime/$bondName").LastWriteTimeUtc) { throw "Rebuild $bondName before deployment." }
        if ((Get-Item src/gpu/backend/clip_interface.hlsli).LastWriteTimeUtc -gt
                (Get-Item -LiteralPath "build/android-runtime/$bondName").LastWriteTimeUtc) { throw "Rebuild $bondName after the interface edit." }
    }
    & $bondAdb -s $DeviceSerial get-state
    if ($LASTEXITCODE -ne 0) { throw 'The requested USB-debugging device is unavailable.' }
    & $bondAdb -s $DeviceSerial shell mkdir -p $bondRemote
    if ($LASTEXITCODE -ne 0) { throw 'Cannot create the owned clipping diagnostic directory.' }
    $bondFiles = @('build/android-runtime/android_clip_probe', $bondCxx) + @($bondShaders.Keys | ForEach-Object { "build/android-runtime/$_" })
    foreach ($bondFile in $bondFiles) {
        & $bondAdb -s $DeviceSerial push $bondFile "$bondRemote/$([IO.Path]::GetFileName($bondFile))"
        if ($LASTEXITCODE -ne 0) { throw "Cannot transfer $bondFile." }
    }
    & $bondAdb -s $DeviceSerial shell rm -f "$bondRemote/clip-result.json"
    if ($LASTEXITCODE -ne 0) { throw 'Cannot clear the owned previous clipping result.' }
    & $bondAdb -s $DeviceSerial shell chmod 700 "$bondRemote/android_clip_probe"
    if ($LASTEXITCODE -ne 0) { throw 'Cannot make the clipping diagnostic executable.' }
    & $bondAdb -s $DeviceSerial shell "cd $bondRemote && LD_LIBRARY_PATH=$bondRemote timeout 180 ./android_clip_probe $bondRemote" `
        *> build/android-clip/device.log
    $bondExit = $LASTEXITCODE
    Get-Content build/android-clip/device.log -Tail 28
    if ($bondExit -ne 0) { throw "Clipping diagnostic failed with exit $bondExit; see build/android-clip/device.log." }
    & $bondAdb -s $DeviceSerial pull "$bondRemote/clip-result.json" build/android-clip/device-result.json
    if ($LASTEXITCODE -ne 0) { throw 'The clipping diagnostic did not produce a fresh result file.' }
    $bondResult = Get-Content build/android-clip/device-result.json -Raw | ConvertFrom-Json
    if ($bondResult.render_comparisons -ne 2560 -or $bondResult.channel_comparisons -ne 7864320 -or
            $bondResult.interpolators_checked -ne 18 -or $bondResult.max_polygon_vertices -ne 9 -or
            $bondResult.max_channel_error -gt 1 -or $bondResult.game_renderer_started -or
            $bondResult.guest_entry_executed) { throw 'Incomplete clipping diagnostic result.' }
    $bondResult | ConvertTo-Json -Compress
} finally { Pop-Location }
