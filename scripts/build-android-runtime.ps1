[CmdletBinding()]
param(
    [ValidateSet('Configure', 'Build', 'FiberProbe', 'RuntimeProbe', 'GuestProbe', 'TextureProbe', 'ClipProbe', 'DescriptorProbe', 'RendererProbe', 'GameHost')][string]$Stage = 'Build',
    [string]$NdkRoot = (Join-Path ([Environment]::GetFolderPath('UserProfile')) 'Android/ndk/28.2.13676358')
)

$ErrorActionPreference = 'Stop'
$bondRoot = Split-Path -Parent $PSScriptRoot
$bondResearch = Split-Path -Parent $bondRoot
$bondCmake = Join-Path $bondResearch 'tools/cmake-4.2.0-windows-x86_64/bin/cmake.exe'
$bondNinja = (Join-Path ([Environment]::GetFolderPath('UserProfile')) 'Android/cmake/3.22.1/bin/ninja.exe')
$bondProcess = Get-Process -Id $PID
$bondPreviousPriority = $bondProcess.PriorityClass
$bondPreviousJobs = $env:CMAKE_BUILD_PARALLEL_LEVEL
Push-Location $bondRoot
try {
    if ($Stage -eq 'GuestProbe' -and !(Test-Path -LiteralPath generated/default/sources.cmake)) {
        throw 'Original game code has not been generated. Run the verified desktop Codegen stage first.'
    }
    $bondProcess.PriorityClass = 'BelowNormal'
    $env:CMAKE_BUILD_PARALLEL_LEVEL = '1'
    $bondUcontext = Join-Path $bondRoot 'build/deps/libucontext'
    $bondUcontextPin = '49e671dd52ff6791295d8161ad3b6da7dc5f6f9d'
    if (!(Test-Path -LiteralPath $bondUcontext)) {
        & git init $bondUcontext
        if ($LASTEXITCODE -ne 0) { throw 'Cannot initialize the fiber dependency checkout.' }
        & git -C $bondUcontext remote add origin https://github.com/kaniini/libucontext.git
        & git -C $bondUcontext fetch --depth 1 origin $bondUcontextPin
        if ($LASTEXITCODE -ne 0) { throw 'Cannot fetch the pinned fiber dependency.' }
        & git -C $bondUcontext checkout --detach FETCH_HEAD
        if ($LASTEXITCODE -ne 0) { throw 'Cannot check out the pinned fiber dependency.' }
    }
    if ((& git -C $bondUcontext rev-parse HEAD) -ne $bondUcontextPin) {
        throw 'The libucontext dependency differs from the tested pin.'
    }
    $bondRendererOption = @()
    if ($Stage -eq 'RendererProbe') { $bondRendererOption = @('-DBROKENBOND_ANDROID_RENDERER=ON') }
    if ($Stage -eq 'GameHost') { $bondRendererOption = @('-DBROKENBOND_ANDROID_RENDERER=ON', '-DBROKENBOND_ANDROID_GAME_HOST=ON') }
    & $bondCmake -S tools/android-runtime -B build/android-runtime -G Ninja `
        "-DCMAKE_TOOLCHAIN_FILE=$NdkRoot/build/cmake/android.toolchain.cmake" `
        "-DCMAKE_MAKE_PROGRAM=$bondNinja" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 `
        -DANDROID_STL=c++_shared -DCMAKE_BUILD_TYPE=MinSizeRel `
        '-DCMAKE_C_FLAGS_MINSIZEREL=-O1 -DNDEBUG' '-DCMAKE_CXX_FLAGS_MINSIZEREL=-O1 -DNDEBUG' @bondRendererOption
    if ($LASTEXITCODE -ne 0) { throw 'Android runtime configuration failed.' }
    if ($Stage -eq 'Build') {
        & $bondCmake --build build/android-runtime --target rexruntime --parallel 1
        if ($LASTEXITCODE -ne 0) { throw 'Android runtime compilation/link failed.' }
    }
    if ($Stage -eq 'FiberProbe') {
        & $bondCmake --build build/android-runtime --target android_fiber_probe --parallel 1
        if ($LASTEXITCODE -ne 0) { throw 'Android fiber probe compilation/link failed.' }
    }
    if ($Stage -eq 'RuntimeProbe') {
        & $bondCmake --build build/android-runtime --target android_runtime_probe --parallel 1
        if ($LASTEXITCODE -ne 0) { throw 'Android runtime probe compilation/link failed.' }
    }
    if ($Stage -eq 'GuestProbe') {
        & $bondCmake --build build/android-runtime --target android_guest_probe --parallel 1
        if ($LASTEXITCODE -ne 0) { throw 'Android translated-game compilation/link failed.' }
    }
    if ($Stage -eq 'TextureProbe') {
        & $bondCmake --build build/android-runtime --target android_texture_probe --parallel 1
        if ($LASTEXITCODE -ne 0) { throw 'Android texture fallback probe compilation/link failed.' }
    }
    if ($Stage -eq 'ClipProbe') {
        & $bondCmake --build build/android-runtime --target android_clip_probe --parallel 1
        if ($LASTEXITCODE -ne 0) { throw 'Android clipping probe compilation/link failed; check BOND_ANDROID_DXC if the target is missing.' }
    }
    if ($Stage -eq 'DescriptorProbe') {
        & $bondCmake --build build/android-runtime --target android_descriptor_probe --parallel 1
        if ($LASTEXITCODE -ne 0) { throw 'Android bounded descriptor probe compilation/link failed.' }
    }
    if ($Stage -eq 'RendererProbe') {
        & $bondCmake --build build/android-runtime --target android_renderer_probe broken_bond_renderer_jni --parallel 1
        if ($LASTEXITCODE -ne 0) { throw 'Android production renderer compilation/link failed.' }
    }
    if ($Stage -eq 'GameHost') {
        & $bondCmake --build build/android-runtime --target broken_bond_game --parallel 1
        if ($LASTEXITCODE -ne 0) { throw 'Android original-game host compilation/link failed.' }
    }
} finally {
    $env:CMAKE_BUILD_PARALLEL_LEVEL = $bondPreviousJobs
    $bondProcess.PriorityClass = $bondPreviousPriority
    Pop-Location
}
