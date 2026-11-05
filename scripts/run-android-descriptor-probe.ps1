[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$DeviceSerial)
$ErrorActionPreference='Stop'
$bondRoot=Split-Path -Parent $PSScriptRoot
$bondAdb=(Join-Path ([Environment]::GetFolderPath('UserProfile')) 'Android/platform-tools/adb.exe')
$bondRemote='/data/local/tmp/broken_bond_descriptor_probe'
Push-Location $bondRoot
try {
    $bondObject=Get-Item build/android-runtime/CMakeFiles/android_descriptor_probe.dir/descriptor_probe.cpp.o
    foreach ($bondSource in @('tools/android-runtime/descriptor_probe.cpp','tools/android-runtime/texture_probe.cpp','src/gpu/backend/vulkan/bounded_descriptors.h')) {
        if ((Get-Item -LiteralPath $bondSource).LastWriteTimeUtc -gt $bondObject.LastWriteTimeUtc) { throw "Rebuild DescriptorProbe after editing $bondSource." }
    }
    if ((Get-Item tools/android-runtime/descriptor_probe.hlsl).LastWriteTimeUtc -gt
            (Get-Item build/android-runtime/descriptor_probe.spv).LastWriteTimeUtc) { throw 'Rebuild the descriptor diagnostic shader.' }
    & $bondAdb -s $DeviceSerial get-state
    if ($LASTEXITCODE -ne 0) { throw 'The requested phone is unavailable.' }
    & $bondAdb -s $DeviceSerial shell mkdir -p $bondRemote
    if ($LASTEXITCODE -ne 0) { throw 'Cannot create the owned descriptor diagnostic directory.' }
    foreach ($bondFile in @('build/android-runtime/android_descriptor_probe','build/android-runtime/descriptor_probe.spv','build/android-runtime/texture_probe.spv',
            (Join-Path ([Environment]::GetFolderPath('UserProfile')) 'Android/ndk/28.2.13676358/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so'))) {
        & $bondAdb -s $DeviceSerial push $bondFile "$bondRemote/$([IO.Path]::GetFileName($bondFile))"
        if ($LASTEXITCODE -ne 0) { throw "Cannot transfer $bondFile." }
    }
    & $bondAdb -s $DeviceSerial shell rm -f "$bondRemote/descriptor-result.json"
    if ($LASTEXITCODE -ne 0) { throw 'Cannot clear the owned descriptor result.' }
    & $bondAdb -s $DeviceSerial shell chmod 700 "$bondRemote/android_descriptor_probe"
    if ($LASTEXITCODE -ne 0) { throw 'Cannot make the descriptor diagnostic executable.' }
    & $bondAdb -s $DeviceSerial shell "cd $bondRemote && LD_LIBRARY_PATH=$bondRemote timeout 120 ./android_descriptor_probe $bondRemote" *> build/android-descriptors/device.log
    $bondExit=$LASTEXITCODE
    Get-Content build/android-descriptors/device.log -Tail 24
    if ($bondExit -ne 0) { throw "Descriptor diagnostic failed with exit $bondExit." }
    & $bondAdb -s $DeviceSerial pull "$bondRemote/descriptor-result.json" build/android-descriptors/device-result.json
    if ($LASTEXITCODE -ne 0) { throw 'Descriptor diagnostic did not write a fresh result.' }
    $bondResult=Get-Content build/android-descriptors/device-result.json -Raw | ConvertFrom-Json
    if ($bondResult.immutable_snapshots -ne 105 -or $bondResult.sampled_values_checked -ne 2520 -or
            $bondResult.register_remap_checks -ne 41 -or $bondResult.unpopulated_rejections -ne 3 -or
            $bondResult.descriptor_indexing_enabled -or $bondResult.game_renderer_started -or $bondResult.guest_entry_executed) {
        throw 'The descriptor diagnostic did not complete its expected checks.'
    }
    $bondResult | ConvertTo-Json -Compress
} finally { Pop-Location }
