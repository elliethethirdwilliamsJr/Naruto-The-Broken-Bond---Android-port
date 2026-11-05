# Android checkpoint validation

Recorded for **2026-11-05**, local APK **0.4.0**. Device: OPPO A58 / Android 15 / Mali-G52 MC2 unless noted. This summarizes local runs; raw identifying device logs, game data, and generated shader corpora are excluded.

## Completed checks

| Area | Observed result | Scope |
| --- | --- | --- |
| Original CPU probe | Five routines, eight cases, three guest threads; 32,765 registrations | Selected translated-code execution, not whole-game correctness |
| Fibers | 5,000 round trips | ARM64 context switching |
| Texture fallback | Four formats, 108 subresources, 17,664 sampled values, 16 negative checks | Selected BC decode/upload contracts |
| Clipping | 2,560 comparisons / 7,864,320 channels; 18 varyings; 1x/4x MSAA; up to nine polygon vertices | Tested clipping/interpolation cases |
| Descriptors | 105 snapshots, 2,520 sampled values, 41 register checks, three negative cases | Selected bounded descriptor behavior |
| Renderer | 121 draws / 8,192 pixels; 60 surface presents and two checked captures; cancel/reopen | Isolated renderer/surface path |
| Original-shader regression | 16 exact pixel cases / eight pipelines, clipping active and disabled | Mali topology regression |
| Thread cleanup | 112 repeated/concurrent waits; immediate/deferred dispatcher shutdown and destruction | Repeat-wait/cleanup regression |
| Bounded game boot | Twenty-second diagnostic released engine/joined renderer workers; deliberate exit 87, no SIGABRT | Controlled cleanup, not a complete playthrough |
| Touch | 22 real SDK input checks | User zero, brief taps, stable packets, chords, reset, capabilities, keystrokes, disconnected user one |
| SDK patches | 28 patches replayed / 49 code files / zero normalized mismatches | Captures local SDK code changes at pin |
| APK | Debug signature and 16 KiB ZIP alignment checked | Packaging, not all-device/page-size certification |
| Desktop compile | Windows Vulkan backend / shader-contract objects compile | Selected objects, no new desktop playthrough |

## Persistent driver cache

These are isolated probe wall times, **not game frame rates**. Each run passed the same 16 pixel comparisons.

| Case | Wall seconds | Cache bytes loaded | Result |
| --- | ---: | ---: | --- |
| Cold | 8.2438 | 0 | Pass |
| Warm | 3.3417 | 183,256 | Pass |
| Corrupt input | 5.4911 | 0 | Pass; empty-cache fallback |

Cold/warm ratio: approximately **2.47x**. The 48 total comparisons establish tested persistence/fallback behavior, not a 2.47x gameplay speedup. Full-game cold/warm timing remains outstanding. [Sanitized machine-readable results](android-validation.json) accompany this record.

## Full-game observations and limits

The original intro with sound, menu/player selection, and training fight displayed. Basic touch input navigated into the fight. [Actual captures](screenshots/README.md) are included. Latest gameplay rate: **approximately 1 FPS, reported by the user**, not calibrated frame-time capture.

No smooth performance, completed campaign, or resolution of every rendering/crash path is claimed. Pausing a debugger during active Vulkan work can itself trigger completion deadlines or Android input ANRs; disturbed captures are not performance evidence.

Diagnostic source is in `tools/android-runtime`. Full-game reproduction requires supported private game inputs and locally generated code/shaders. A fresh clone has not passed the whole Android build. See [build notes](android-build.md) for remaining setup gaps.
