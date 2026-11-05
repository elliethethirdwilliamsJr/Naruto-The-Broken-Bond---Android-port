[CmdletBinding()]
param(
    [ValidateSet('Sdk', 'Codegen', 'Native', 'All')][string]$Stage = 'All',
    [ValidateRange(1, 4)][int]$ShaderJobs = 2
)

# Local development baseline. One C++ job, a bounded DXC pool; never launches the game.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'enter-portable-build.ps1')
$bondRoot = Split-Path -Parent $PSScriptRoot
$bondPreviousShaderJobs = $env:BROKENBOND_SHADER_JOBS
$bondBuildProcess = Get-Process -Id $PID
$bondPreviousPriority = $bondBuildProcess.PriorityClass
Push-Location $bondRoot
try {
    $bondBuildProcess.PriorityClass = 'BelowNormal'
    $env:BROKENBOND_SHADER_JOBS = [string]$ShaderJobs
    Write-Host "Build priority BelowNormal; one C++ job; $ShaderJobs shader workers."
    $bondExpected = '8F70E79443E36B38E44B6A105DAD51FB8909FB615D982753CABAAC9B15F0D576'
    if ((Get-FileHash -LiteralPath 'game/default.xex' -Algorithm SHA256).Hash -ne $bondExpected) {
        throw 'The game executable differs from the supported revision.'
    }
    if ($Stage -in @('Sdk', 'All')) {
        & cmake -S sdk -B build/sdk -G 'Ninja Multi-Config' `
            -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ `
            '-DCMAKE_C_FLAGS=-march=x86-64-v2 -gline-tables-only' `
            '-DCMAKE_CXX_FLAGS=-march=x86-64-v2 -gline-tables-only' `
            "-DCMAKE_INSTALL_PREFIX=$bondRoot/build/sdk-install" `
            -DREXGLUE_BUILD_TESTS=OFF -DREXGLUE_ENABLE_TRACY=OFF -DREXGLUE_BUILD_XENOS_PLUGIN=OFF `
            -DREXGLUE_USE_VULKAN=OFF -DREXGLUE_USE_D3D12=ON `
            -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON -DCMAKE_RC_COMPILER=llvm-rc -DCMAKE_MT=llvm-mt
        if ($LASTEXITCODE -ne 0) { throw 'SDK configuration failed.' }
        & cmake --build build/sdk --config Release --target install --parallel 1
        if ($LASTEXITCODE -ne 0) { throw 'SDK compilation failed.' }
    }
    if ($Stage -in @('Codegen', 'All')) {
        & ./build/sdk-install/bin/rexglue.exe codegen ./broken_bond_manifest.toml
        if ($LASTEXITCODE -ne 0) { throw 'Original game code generation failed.' }
    }
    if ($Stage -in @('Native', 'All')) {
        $bondShaderDeps = Join-Path (Split-Path -Parent $bondRoot) 'naruto-rise-recomp/tools/xenosrecomp/thirdparty'
        $bondSpirv = (Join-Path ([Environment]::GetFolderPath('UserProfile')) 'Android/ndk/28.2.13676358/shader-tools/windows-x86_64/spirv-val.exe')
        # Vulkan-only is an explicit development configuration for the Android effort.
        & cmake --preset windows-release -B build/native-development `
            "-DCMAKE_PREFIX_PATH=$bondRoot/build/sdk-install" `
            "-Drexglue_DIR=$bondRoot/build/sdk-install/lib/cmake/rexglue" `
            -DBROKENBOND_NATIVE_RENDERER=ON -DBROKENBOND_NATIVE_D3D12=OFF `
            -DBROKENBOND_NATIVE_VULKAN=ON -DBROKENBOND_NATIVE_TESTS=OFF `
            -DBROKENBOND_ENABLE_SENTRY=OFF -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON `
            '-DCMAKE_C_FLAGS_RELEASE=-O1 -DNDEBUG' '-DCMAKE_CXX_FLAGS_RELEASE=-O1 -DNDEBUG' `
            "-DCMAKE_INSTALL_PREFIX=$bondRoot/build/native-install" `
            -DCMAKE_RC_COMPILER=llvm-rc -DCMAKE_MT=llvm-mt `
            "-DNATIVE_RENDERER_PLUME_SOURCE=$bondRoot/build/deps/plume" `
            "-DBROKENBOND_XENOS_RECOMP_THIRDPARTY=$bondShaderDeps" `
            "-DBROKENBOND_SPIRV_VAL=$bondSpirv"
        if ($LASTEXITCODE -ne 0) { throw 'Native development configuration failed.' }
        & cmake --build build/native-development --target broken_bond --parallel 1
        if ($LASTEXITCODE -ne 0) { throw 'Native game compilation failed.' }
    }
} finally {
    $env:BROKENBOND_SHADER_JOBS = $bondPreviousShaderJobs
    $bondBuildProcess.PriorityClass = $bondPreviousPriority
    Pop-Location
}
