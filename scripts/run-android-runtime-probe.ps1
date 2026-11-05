[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$DeviceSerial,
    [ValidateSet('Runtime', 'Fiber', 'Guest')][string]$Probe = 'Runtime',
    [string]$NdkRoot = (Join-Path ([Environment]::GetFolderPath('UserProfile')) 'Android/ndk/28.2.13676358')
)

$ErrorActionPreference = 'Stop'
$bondRoot = Split-Path -Parent $PSScriptRoot
$bondAdb = Join-Path (Split-Path -Parent (Split-Path -Parent $NdkRoot)) 'platform-tools/adb.exe'
$bondRemote = '/data/local/tmp/broken_bond_runtime_probe'
$bondCxx = Join-Path $NdkRoot 'toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so'
$bondName = switch ($Probe) {
    'Runtime' { 'android_runtime_probe' }
    'Guest' { 'android_guest_probe' }
    'Fiber' { 'android_fiber_probe' }
}
$bondResultName = if ($Probe -eq 'Guest') { 'guest-result.json' } else { 'runtime-result.json' }
Push-Location $bondRoot
try {
    # An edit during a long game-code build may be newer than its already-built
    # diagnostic object, even when the final executable was linked afterward.
    $bondProbeSources = switch ($Probe) {
        'Guest' { @('runtime_probe.cpp', 'guest_checks.cpp') }
        'Runtime' { @('runtime_probe.cpp') }
        'Fiber' { @('fiber_probe.cpp') }
    }
    foreach ($bondSource in $bondProbeSources) {
        $bondSourceFile = Get-Item -LiteralPath "tools/android-runtime/$bondSource"
        $bondObjectPath = "build/android-runtime/CMakeFiles/$bondName.dir/$bondSource.o"
        if (!(Test-Path -LiteralPath $bondObjectPath) -or
                $bondSourceFile.LastWriteTimeUtc -gt (Get-Item -LiteralPath $bondObjectPath).LastWriteTimeUtc) {
            throw "Diagnostic object is missing or older than $bondSource. Rebuild the $($Probe)Probe stage before deployment."
        }
    }
    & $bondAdb -s $DeviceSerial get-state
    if ($LASTEXITCODE -ne 0) { throw 'The requested USB-debugging device is not available.' }
    & $bondAdb -s $DeviceSerial shell mkdir -p "$bondRemote/game"
    if ($LASTEXITCODE -ne 0) { throw 'Cannot create the private diagnostic directory.' }
    & $bondAdb -s $DeviceSerial push "build/android-runtime/$bondName" "$bondRemote/$bondName"
    if ($LASTEXITCODE -ne 0) { throw 'Cannot transfer the diagnostic executable.' }
    & $bondAdb -s $DeviceSerial push $bondCxx "$bondRemote/libc++_shared.so"
    if ($LASTEXITCODE -ne 0) { throw 'Cannot transfer the NDK C++ runtime.' }
    if ($Probe -ne 'Fiber') {
        $bondExpectedXex = '8F70E79443E36B38E44B6A105DAD51FB8909FB615D982753CABAAC9B15F0D576'
        if ((Get-FileHash -LiteralPath game/default.xex -Algorithm SHA256).Hash -ne $bondExpectedXex) {
            throw 'The executable differs from the supported game revision.'
        }
        & $bondAdb -s $DeviceSerial push sdk/out/android-arm64/librexruntime.so "$bondRemote/librexruntime.so"
        if ($LASTEXITCODE -ne 0) { throw 'Cannot transfer the ARM64 SDK runtime.' }
        & $bondAdb -s $DeviceSerial push game/default.xex "$bondRemote/game/default.xex"
        if ($LASTEXITCODE -ne 0) { throw 'Cannot transfer the supported original XEX.' }
        if ($Probe -eq 'Guest') {
            & $bondAdb -s $DeviceSerial push build/android-runtime/libbroken_bond_guest.so "$bondRemote/libbroken_bond_guest.so"
            if ($LASTEXITCODE -ne 0) { throw 'Cannot transfer the complete ARM64 translated-game library.' }
        }
        & $bondAdb -s $DeviceSerial shell rm -f "$bondRemote/$bondResultName"
        if ($LASTEXITCODE -ne 0) { throw 'Cannot clear the previous diagnostic result.' }
    }
    & $bondAdb -s $DeviceSerial shell chmod 700 "$bondRemote/$bondName"
    if ($LASTEXITCODE -ne 0) { throw 'Cannot make the diagnostic executable.' }
    $bondArgument = if ($Probe -ne 'Fiber') { $bondRemote } else { '' }
    & $bondAdb -s $DeviceSerial shell "cd $bondRemote && LD_LIBRARY_PATH=$bondRemote timeout 120 ./$bondName $bondArgument" `
        *> "build/android-$($Probe.ToLower())-device.log"
    $bondExit = $LASTEXITCODE
    Get-Content -LiteralPath "build/android-$($Probe.ToLower())-device.log" -Tail 35
    if ($bondExit -ne 0) { throw "Android $Probe diagnostic failed with exit $bondExit; see its device log." }
    if ($Probe -ne 'Fiber') {
        $bondLocalResult = "build/android-$($Probe.ToLower())-device-result.json"
        & $bondAdb -s $DeviceSerial pull "$bondRemote/$bondResultName" $bondLocalResult
        if ($LASTEXITCODE -ne 0) { throw 'The runtime did not produce a fresh result file.' }
        $bondResult = Get-Content -LiteralPath $bondLocalResult -Raw | ConvertFrom-Json
        if (!$bondResult.runtime_setup -or !$bondResult.xex_loaded -or $bondResult.guest_entry_executed) {
            throw 'The runtime diagnostic result did not match the no-game-entry contract.'
        }
        if ($Probe -eq 'Guest' -and ($bondResult.registered_functions_checked -ne 32765 -or
                $bondResult.original_routines_executed -ne 5 -or $bondResult.original_routine_cases -ne 8 -or
                $bondResult.guest_threads_completed -ne 3)) {
            throw 'The translated-game diagnostic did not complete every expected check.'
        }
        $bondResult | ConvertTo-Json -Compress
    }
} finally {
    Pop-Location
}
