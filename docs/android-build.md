# Android developer build notes

The working local checkout produced and installed APK 0.4.0. **A fresh clone has not yet passed the full Android build.** These notes document the tested environment and remaining setup gaps, not a verified one-command setup.

The [upstream build guide](build-and-run.md) provides desktop context. Its releases, supported platforms, and performance claims do not describe an Android release.

## Prerequisites and pins

- Windows x64, PowerShell, Git, Python on PATH, JDK 17 tools on PATH.
- Android SDK platform/build tools 35, platform-tools, NDK `28.2.13676358` (Clang 19.0.1). ARM64 native target API 28; Java/manifest platform API 35.
- CMake 4.2.0, Ninja 3.22.1, host LLVM 19.1.5, MSVC/Windows SDK headers and libraries, DXC and SPIR-V tools.
- Your supported original executable and extracted assets in ignored `game/`. See the [README](../README.md) for the hash.
- Space for assets, generated C++, shader translations, and native intermediates. Scripts use one C++ job, disabled PCH, and `-O1` to fit the approximately 6 GB development laptop.

| Dependency | Tested revision |
| --- | --- |
| Upstream starting point | `37b72e657c24022df8057f0cb1b718b81830c77a` |
| ReXGlue SDK | `c94f5ebdcb3c9d1a460ca48e04f9758448f8d518` |
| Plume | `d72379344dacd3dbf9f810f92ddc87e6de1845b1` |
| Rise recomp sibling shader tools | `2bfdcd643692f910b88ebbfcf46b0356d69a63c9` |
| libucontext | `49e671dd52ff6791295d8161ad3b6da7dc5f6f9d` |

## Checkout and dependency layout

```powershell
git clone --recurse-submodules https://github.com/elliethethirdwilliamsJr/Naruto-The-Broken-Bond---Android-port.git
Set-Location Naruto-The-Broken-Bond---Android-port
git switch develop
```

Keep the SDK pinned. Apply the ordered patches through `scripts/build-sdk.ps1` as documented upstream; do not move the submodule to latest. Windows libmspack symlinks must resolve to actual source targets, not text placeholders.

The current developer scripts expect:

```text
parent/
  tools/
    cmake-4.2.0-windows-x86_64/bin/
    llvm/clang+llvm-19.1.5-x86_64-pc-windows-msvc/bin/
    msvc/                         # MSVC and Windows SDK
  naruto-rise-recomp/tools/xenosrecomp/thirdparty/
  Naruto-The-Broken-Bond---Android-port/
    build/deps/plume/
    game/default.xex
```

`scripts/enter-portable-build.ps1` sets the process-local host toolchain. Android defaults use the user's profile `Android` directory. `build-android-runtime.ps1` accepts `-NdkRoot`; the Python APK packer accepts `--sdk`. Adjust remaining script paths or CMake cache values for a different layout. The scripts do not install MSVC/Windows SDK for you.

Plume must be clean at its pin with Vulkan submodules initialized; `tools/native-renderer/plume-engine.cmake` verifies exact dependency pins and patches build copies. The Rise sibling supplies expected DXC/fmt/smol-v/zstd shader-tool dependencies. Follow upstream dependency preparation: cloning the sibling alone does not install every binary. The Android runtime script fetches pinned libucontext when absent.

## Original code and shader preparation

Build/patch the host SDK first, following upstream instructions. With `build/sdk-install/bin/rexglue.exe` and the supported executable available:

```powershell
./scripts/build-local-desktop.ps1 -Stage Codegen
```

This runs `rexglue codegen ./broken_bond_manifest.toml` and creates ignored `generated/default/` source. SDK/native stages also exist in that script after dependencies and patches are prepared.

The game host needs an audited mobile shader cache, regenerated from your own game. Desktop cache bytes are not interchangeable with this mobile candidate. Current working stages are:

1. Native baseline: `build/native-development/shader_cache/work`.
2. Mobile transforms: `tools/native-renderer/mobile_descriptors.py` and `mobile_clip.py`, through the shader build tools and their CLI options.
3. Candidate: `build/android-descriptors/corpus`; manifest `build/android-descriptors/shader_manifest_bounded.json`; geometry passes `build/android-clip/passes-geometry/spirv`.
4. Audit: `tools/android-runtime/audit_descriptor_profile.py` checks computation preservation, capabilities, and module identities, producing `build/android-descriptors/corpus-audit.json`.
5. Pack: `python tools/android-runtime/pack_renderer_cache.py` uses that audit, the baseline index, and local `broken_bond_smolv_encode.exe` / `broken_bond_shader_compress.exe`, producing `build/android-renderer-cache/shader_cache_spirv.bin`.

Inspect the shader scripts' CLI/help and source for their required inputs. Automating this complete mobile corpus preparation from a clean checkout remains open; no complete clean-build recipe is claimed here.

## Build and package

After generated code, audited mobile cache, and dependencies exist:

```powershell
./scripts/build-android-runtime.ps1 -Stage GameHost
./scripts/build-android-test-apk.ps1 -BuildNative
```

The second command builds included probe targets and the host, then calls `tools/android-apk/build_apk.py`. Packaging uses SDK aapt2, javac, D8, zipalign, and apksigner directly without Gradle, and checks native-object freshness. For a nondefault SDK, run the packer with `--sdk` after native builds.

Output: `build/android-apk/BrokenBond-Native-Test.apk`, package `org.brokenbond.nativecheck`. Debug keys, APKs, and build reports remain local. This app still contains diagnostic activities. Android Vulkan is an explicit development configuration; the upstream Windows shipping D3D12 policy is preserved.

## Install and stage assets

```powershell
adb install --no-incremental -r build/android-apk/BrokenBond-Native-Test.apk
adb shell mkdir -p /sdcard/Android/data/org.brokenbond.nativecheck/files/game
adb push game/. /sdcard/Android/data/org.brokenbond.nativecheck/files/game/
adb shell am start -W -n org.brokenbond.nativecheck/.MainActivity
```

Use an authorized USB-debugging device; add `-s DEVICE` if multiple devices are connected. Preserve the extracted layout, with `default.xex` directly in `files/game`. These example transfer commands do not perform the separate SHA-256 asset verification used locally. Selecting an executable in the diagnostic UI does not import a complete game folder.

The main activity exposes original-game launch. Its game activity supplies the landscape surface and touch overlay. Normal launches use the driver cache without heavy traces or a diagnostic watchdog.

## Diagnostics and setup work remaining

- Targets are defined in `tools/android-runtime/CMakeLists.txt`; `android_touch_input_probe` checks real SDK input.
- `check_driver_cache.py` checks cold/warm/corrupt behavior. `launch_game_capture.py` and `collect_game_boot.py` support bounded diagnosis; inspect `--help` before use.
- The experimental fast pipeline compile option is opt-in and does not establish smooth gameplay.
- Next setup work: parameterize parent/sibling tool paths, automate mobile shader preparation, and verify a fresh checkout end to end.

See [progress](android-port-progress.md) and [validation](android-validation.md) for observed results and limits.
