[CmdletBinding()]
param(
    [switch]$BuildNative,
    [string]$DeviceSerial = ''
)
$ErrorActionPreference = 'Stop'
$bondRoot = Split-Path -Parent $PSScriptRoot
Push-Location $bondRoot
try {
    if ($BuildNative) {
        foreach ($bondStage in @('GuestProbe','TextureProbe','ClipProbe','DescriptorProbe','RendererProbe','GameHost')) {
            & ./scripts/build-android-runtime.ps1 -Stage $bondStage
        }
    }
    & python -I tools/android-apk/build_apk.py
    if ($LASTEXITCODE -ne 0) { throw 'Android diagnostic APK packaging failed.' }
    if ($DeviceSerial) {
        $bondAdb = (Join-Path ([Environment]::GetFolderPath('UserProfile')) 'Android/platform-tools/adb.exe')
        & $bondAdb -s $DeviceSerial install --no-incremental -r build/android-apk/BrokenBond-Native-Test.apk
        if ($LASTEXITCODE -ne 0) { throw 'Android diagnostic APK installation failed.' }
        & $bondAdb -s $DeviceSerial shell am start -W -n org.brokenbond.nativecheck/.MainActivity
        if ($LASTEXITCODE -ne 0) { throw 'Android diagnostic activity did not start.' }
    }
} finally { Pop-Location }
