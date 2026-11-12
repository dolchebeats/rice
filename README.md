# rice

A JUCE VST3 / standalone granular capture-and-repeat effect. This first version is inspired by the rhythmic looping idea of [Ableton's Beats warp mode](https://www.ableton.com/en/live-manual/12/audio-clips-tempo-and-warping/), with its own live-input capture engine.

## Controls

- **Grain size:** 1-1,000 ms, or a tempo-synced note length.
- **Capture interval:** time between the starts of new recordings, independently set to 1-1,000 ms or a synced note length. Shorter intervals refresh more often. Recording windows can overlap when the interval is shorter than the grain.
- **Transient capture:** select Transient in the capture dropdown; the second knob becomes sensitivity. Higher percentages detect quieter attacks. A stereo-linked fast/slow envelope detector includes a noise floor, re-arming, and a 15 ms holdoff to avoid triggering repeatedly on one attack.
- **Repeat:** Straight loops forward; Reverse loops backward; Back-and-forth alternates direction across the entire grain without jumping between its ends.

Both sync knobs contain `1/32`, `1/16`, `1/8`, `1/6`, `1/4`, `1/2`, `1`, `2`, `3`, and `4`, each with triplet (`T`, two-thirds length) and dotted (`D`, one-and-a-half length) variants. Values are sorted by duration; some labels intentionally have the same duration. `1` means one whole note (four quarter-note beats), regardless of time signature.

## Capture behavior

A trigger begins recording fresh incoming audio. After the selected grain duration has elapsed, that recording becomes the new repeating grain. The previous grain keeps playing while the next one records. Before the first grain is ready, live input passes through. One initial capture starts automatically, including in Transient mode.

For example, with a 100 ms size and 250 ms interval, recordings start at 0, 250, 500 ms, and become playable at 100, 350, 600 ms. This recording wait also applies to Reverse and Back-and-forth. Playback is fully wet after the first capture. The wait is part of the effect, rather than fixed host-compensated latency.

Tempo follows the DAW, with a visible 120 BPM fallback when the host supplies no tempo (including standalone use). Synced capture starts follow the host PPQ grid while playing; the first capture starts immediately, then subsequent starts follow the grid. Each completed grain becomes audible one grain duration after its trigger. When stopped, or without host PPQ, synced capture free-runs at the supplied tempo. Supported tempo is 20-999 BPM; the longest setting is 4 dotted whole notes at 20 BPM (72 seconds).

Size edits discard unfinished captures and begin a recording at the new size. Capture-mode changes and host timeline jumps in synced mode also discard unfinished recordings. The existing audible grain continues until a replacement is ready. Direction changes affect the existing grain immediately. Short fixed fades smooth loop boundaries, replacements, and direction changes; Back-and-forth reflects at the endpoints. These are de-click fades, not user-adjustable grain envelopes.

Mono and stereo layouts are supported. Stereo audio retains its channels, with linked timing and detection. All controls support host automation and are saved with the session; recorded audio is temporary. In Transient mode the last grain can repeat indefinitely without further triggers, and the VST3 reports an infinite tail accordingly.

Memory is preallocated in `prepareToPlay`, including space for long synced grains (approximately 106 MiB of audio buffers at 48 kHz). The audio callback allocates no grain buffers and copies snapshots incrementally to keep capture work bounded.

## Build on Windows

Requires the included `JUCE` directory, CMake 3.22+, and Visual Studio 2022 with the C++ desktop workload and a Windows SDK.

```powershell
cmake -S . -B build -DRICE_BUILD_TESTS=ON
cmake --build build --config Debug --target rice_VST3 rice_Standalone rice_engine_tests --parallel 4
ctest --test-dir build -C Debug --output-on-failure
```

Outputs:

- VST3 bundle: `build/rice_artefacts/Debug/VST3/rice.vst3`
- Standalone app: `build/rice_artefacts/Debug/Standalone/rice.exe`

For a DAW, copy the entire `rice.vst3` bundle into a VST3 folder scanned by your host, then rescan. Nothing is installed automatically. For optimized builds, replace `Debug` with `Release` in the build command and output paths.

Try a drum loop with a 50-100 ms grain, a synced `1/8` capture interval, and Back-and-forth playback. Then switch capture to Transient and raise sensitivity to refresh on attacks. Future versions can add envelopes, additional voices, panning, detune, and texture processing.

## Validation

The offline engine checks exercise forward/reverse/ping-pong sample order, persistent snapshots after history wraparound, independent stereo audio, overlapping 1 ms captures with a 1,000 ms grain, host PPQ boundaries across blocks, seeks and tempo ramps, triplet/dotted conversions, transient sensitivity and re-arming, reset, 1 ms grains at 44.1/96 kHz, and nonfinite input handling. Listening and DAW-specific automation testing remain necessary when evaluating the sound.
