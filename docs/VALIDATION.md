# rice verification record

Date: 2026-11-16. Platform: Windows x64 / Visual Studio 2022 / JUCE / CMake.

## Final build results

Both Debug and Release produced VST3 and standalone targets. Each configuration passed all three CTest suites, with no failed tests. JUCE's VST3 manifest helper also loaded the compiled module and generated its class manifest.

| Area | Verification |
| --- | --- |
| Existing Beat engine | Forward/reverse/ping-pong order, immediate first passes, overlapping captures, stable held caches, independent stereo timing, minimum grain sizes, reset and transport stop |
| AHDSR | Stage boundaries, sustain and proportional time fitting, positive/negative curvature, compiled audio curve compared to the editor model, mouse point and midpoint drags, double-click-to-linear |
| Frozen Beat | Retained slice across stop/restart, adding voices from immutable caches, pitch edits on a held slice, envelope edits at the next cycle, safe unfreeze |
| Texture and density | Independent capture/launch counts, multiple simultaneous grains, bounded voice pool, normalised mix, safe capacity overflow |
| Buffer / position / scan / freeze | Actual signed source selection in Beat, read-bound protection, retained frozen source, scan phase, restart without stale unfrozen data, frozen waveform consistency |
| Pitch / scale | Octave-up and octave-down frequency measurements, pitch-ratio math, root and scale quantisation, independent envelope lifetime |
| Modulation | LFO host phase, negative timeline positions, sequencer division, follower attack/release, smooth-random continuity, actual matrix-to-parameter routing without changing saved base values |
| Feedback | Integrated pitched cloud with 95% feedback and 24 dB drive, filter bounds, finite and bounded output |
| Host integration | Mono/stereo dry endpoints, 50% wet blend, 20 ms smoothing, host playhead stop forwarding, fixed vs varying audio block sizes |
| State | Every parameter survives XML roundtrip; missing parameters in older sessions reset to compatible defaults even in an already-edited instance |
| Realtime allocation | new/new[] instrumentation around cloud processing and full processor callbacks found no callback allocations in the exercised paths |
| UI | All five tabs rendered to PNG and inspected for clipping, control overlap, readable labels, active waveform/grain markers, unit switching and route controls |

## Performance and resource bounds

The Release cloud-only stress benchmark at 48 kHz, with 256 active stereo pitched grains, took approximately 0.44 seconds to process one second of audio on the development machine. This is a wall-time processing measurement, not a DAW CPU or driver-latency measurement.

Texture overlap is bounded to 16 grains per layer at 44.1-48 kHz, 8 at 88.2-96 kHz and 4 at 192 kHz, with up to 16 layers. Excess launches are skipped and visibly reported. Audio buffers are preallocated; maximum reserved storage is much larger than the memory touched by ordinary short grains. See README for the buffer and memory limits.

## Reproduce

```powershell
cmake -S . -B build -DRICE_BUILD_TESTS=ON
cmake --build build --config Release --target rice_VST3 rice_Standalone rice_engine_tests rice_advanced_tests rice_plugin_tests --parallel 4
ctest --test-dir build -C Release --output-on-failure
.\build\Release\rice_advanced_tests.exe --benchmark
```

Replace Release with Debug to reproduce the other configuration. UI renders are build/rice-ui.png and build/rice-tab-1.png through rice-tab-4.png.

## Remaining external checks

The plugin has not been auditioned in a real DAW or independently run through Steinberg's validator. Driver behavior, individual host automation/preset compatibility, perceptual sound quality and long-session operation remain external validation work. No claim of zero possible defects is made. Freeze audio is temporary and is not embedded in saved sessions; pitch snapping quantises offsets rather than detecting an input key.

## Packaged binary

Release VST3 binary SHA-256: C81A1A9F5E8F206BC946241FD959D1D3B91F4F38A0A40647C62C3A1A53E4A3D3
