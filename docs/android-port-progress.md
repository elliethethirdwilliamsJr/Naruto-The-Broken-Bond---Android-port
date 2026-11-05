# Android port progress

Checkpoint: **2026-11-05**, development APK **0.4.0**, version code **8**.

The original game reaches its intro, menus, player selection, and a training fight on the tested phone. Basic touch input works. The latest user reports approximately **1 FPS**. Reaching a scene establishes execution and rendering progress; it does not establish a playable port.

## Scope and provenance

This work builds on the existing Broken Bond recompilation at upstream commit `37b72e657c24022df8057f0cb1b718b81830c77a`. ReXGlue statically translates the original executable, then the translated code is compiled for ARM64. The original commercial engine's source has not been recovered. This is an adaptation of an existing recompilation and renderer, with Android runtime, graphics, input, and packaging work.

The SDK submodule remains pinned at `c94f5ebdcb3c9d1a460ca48e04f9758448f8d518`. SDK changes are delivered through `patches/series`. All 28 patches were replayed against the clean pin: 49 modified SDK code files matched the development checkout after LF normalization. Windows-materialized libmspack symlink targets were separately checked against their original targets.

## Milestones reached

1. **Original code on ARM64.** Generated 188 C++ units representing 32,479 original routines. Probes exercised original routines and guest threads; the full host reached the original entry point. Generated translations remain local build inputs.
2. **Runtime services.** ARM64 fibers with libucontext, SDK kernel/VFS, asset access, audio, and host integration. Main libraries: `libbroken_bond_guest.so`, `libbroken_bond_game.so`, `librexruntime.so`.
3. **Mobile graphics compatibility.** BC1/3/4/5 decode/upload fallback, bounded 64-entry descriptors for the tested device's feature limits, and geometry-based six-plane clipping preserving 18 interpolators. The audited candidate contains 5,009 original shader translations plus 19 passes; game-derived binaries remain private build inputs.
4. **Original display.** Intro with sound, original menu and player selection, and a Sasuke vs Naruto training fight observed. Actual captures are linked below.
5. **Android presentation.** Landscape activity, corrected rotation, and removal of repeated swapchain recreation on expected `SUBOPTIMAL`. Real size/settings changes and out-of-date/lost surfaces still recreate it.
6. **Shutdown stability.** Idempotent POSIX thread join/cleanup and dispatcher shutdown, checked with repeated/concurrent waits and renderer shutdown regressions.
7. **Touch input.** Java overlay through JNI to a real SDK input driver: Start, Back, D-pad, A, B. Brief-tap latching, stable packets, button chords, queued keystrokes, pause reset, and merged player-zero physical/touch input.
8. **Pipeline persistence.** Device/UUID-validated Vulkan driver cache, 128 MB bound, serialized driver access, atomic saves. Cold/warm/corrupt-cache probes preserve pixel results. Full-game warm-start timing is not yet measured.

## Rendering and startup fixes

The tested Mali driver could lose geometry-stage output with dynamic primitive topology. Vulkan now bakes exact list/strip topology into distinct private pipeline identities. Public schema-1 and D3D12 keys remain unchanged. The original-shader regression covers 16 pixel cases across eight pipelines, with clipping enabled and disabled.

Long shader compilation could exceed the CPU frame gate. Its deadline now extends only on actual completed compilation progress. GPU completion waits remain bounded; completed fences are never fabricated.

Normal launches have no diagnostic 60-second watchdog or heavy tracing. Diagnostic limits and the experimental disable-pipeline-optimization flag remain opt-in; that flag is **not** the normal performance setting.

The driver cache lives privately at `files/game_cache/vulkan-driver-cache.bin`. An isolated diagnostic was about 2.47 times faster warm than cold. This is **not a measured gameplay speedup**; see [validation](android-validation.md).

## Observed device and local artifact

| Item | Value |
| --- | --- |
| Device | OPPO A58 / CPH2577 |
| OS / ABI | Android 15 / API 35 / ARM64 |
| GPU / RAM | Mali-G52 MC2 / approximately 6 GB |
| APK | 0.4.0 / version code 8 / debug signed |
| APK bytes | 69,232,229 |
| APK SHA-256 | `f76cf59c705ffb3d7e26f5550a4830b9a0ada68162a35dfbbac08e2d97aaef8b` |
| Local asset staging | 216 files / 3,411,863,317 bytes / SHA-256 verified |
| Current gameplay rate | Approximately 1 FPS, reported by the user |

The APK is a local diagnostic artifact, not a release in this source checkpoint. Native `-O1`, disabled precompiled headers, and one compile job fit the development laptop's memory limits. This is not an optimized mobile release build.

## Remaining work

- **Performance:** measure CPU execution, submission, descriptor work, geometry stages, GPU work, and present waits in the slow fight. The dominant cause of 1 FPS has not been established.
- **Startup:** compare cold/warm full-game launches; separate compilation latency from steady-state frame rate. Long black transitions remain possible.
- **Optimization:** use measured bottlenecks to evaluate avoiding unnecessary geometry work when clipping is disabled, reducing descriptor churn, and stronger native optimization while preserving floating-point behavior. These are hypotheses, not completed improvements.
- **Controls:** virtual analog sticks, triggers, X/Y, and shoulder-button overlay remain missing.
- **Coverage:** campaign completion, save/load, DLC, networking, long sessions, and other device families are unverified on Android.
- **Build setup:** parameterize parent/sibling tool paths, automate mobile shader generation, and validate a fresh checkout end to end.
- **Mobile settings:** a tested lower-resolution preset is not implemented; inherited internal scale is currently 1x–4x.

Desktop feature claims and frame rates do not transfer to Android. No smooth frame-rate target or completion date is established.

## Evidence and resuming

- [Original training fight](screenshots/android-training-fight.png)
- [Original player selection](screenshots/android-player-selection.png)
- [Screenshot provenance](screenshots/README.md)
- [Validation](android-validation.md) and [build notes](android-build.md)

Resume from this checkpoint by profiling the actual slow fight before introducing more performance changes. Preserve the working original menu/input path and use the existing pixel/input/shutdown regressions when changing it.
