# rice

A JUCE VST3 / standalone granular capture-and-repeat effect with its own live-input capture engine.

## Controls

- **Grain size:** 1-1,000 ms, or a tempo-synced note length.
- **Capture interval:** time between the starts of new recordings, independently set to 1-1,000 ms or a synced note length. Shorter intervals refresh more often. Recording windows can overlap when the interval is shorter than the grain.
- **Transient capture:** select Transient in the capture dropdown; the second knob becomes sensitivity. Higher percentages detect quieter attacks. A stereo-linked fast/slow envelope detector includes a noise floor, re-arming, and a 15 ms holdoff to avoid triggering repeatedly on one attack.
- **Repeat:** Straight loops forward; Reverse loops backward; Back-and-forth alternates direction across the entire grain without jumping between its ends.
- **Voices:** 1-16 independent capture/repeat voices, default 1. The mix is normalised, including voices fading out during automation, so unison does not multiply the peak level.
- **Spread:** 0-100% stereo distribution, with voices spaced evenly left to right using equal-power stereo balance. A single voice stays centered. Stereo source channels are preserved; mono hosts receive a fold-down of both sides.
- **Random Size:** draw a fresh uniform signed size offset per voice and capture, within +/- the selected amount.
- **Random Interval:** draw a fresh uniform signed interval offset per voice for each timed capture. In Transient mode, it instead jitters the capture source around each detected attack independently per voice (negative jitter uses earlier buffered audio; positive jitter waits).
- **Random Position:** draw a fresh uniform signed source-position offset per voice and capture. Negative offsets read earlier input history; positive offsets wait for later input to arrive. At startup, earlier positions clamp to the beginning of the current transport run.

Each random knob has its own Milliseconds / Tempo sync dropdown. Millisecond amounts range from 0 to 1,000 ms; synced amounts include Off plus the same divisions, triplets and dotted values as grain size. They specify the maximum signed variation, not a new base value. All randomness defaults to 0 / Off, and switching units remembers both amounts. Amount edits apply on subsequent captures; a new random draw is made for each voice each time, using separate deterministic pseudo-random streams (transport restart repeats the streams).

Randomised grain lengths clamp to 1 ms-72 seconds, and timed intervals clamp to at least 1 ms. Total position displacement, including transient timing jitter, clamps to +/-72 seconds. These limits protect the preallocated buffers; large amounts can hit them. A +/-20 ms size amount on a 100 ms grain produces lengths from 80 to 120 ms. Identical voices with all randomness off remain identical, so spreading them alone does not decorrelate mono audio.

Both sync knobs contain `1/32`, `1/16`, `1/8`, `1/6`, `1/4`, `1/2`, `1`, `2`, `3`, and `4`, each with triplet (`T`, two-thirds length) and dotted (`D`, one-and-a-half length) variants. Values are sorted by duration; some labels intentionally have the same duration. `1` means one whole note (four quarter-note beats), regardless of time signature.

## Capture behavior

A trigger begins recording fresh incoming audio and plays that first pass live, independently per voice. It replaces that voice's old loop during this pass, rather than adding dry audio to it. Random Position shifts the source of this pass as well as its recorded repeats: past positions are available immediately; future positions wait while the existing grain continues (or silence before the first grain). After the selected grain duration has elapsed, the recording repeats in the selected direction. One initial capture per voice starts automatically, including in Transient mode.

For example, with a 100 ms size and 250 ms interval, recordings start and are heard live at 0, 250, 500 ms. Repeats begin at 100, 350, 600 ms. Straight wraps to the beginning; Back-and-forth turns back from the end immediately after its live forward pass. Reverse also has a live forward first pass, then loops the complete recording backward. Reversing audio that has not yet arrived is impossible without lookahead, so this live bridge is the intentional low-latency compromise. There is no grain-length delay on the first pass and no reported plugin latency. Fixed de-click crossfades take up to 0.5 ms.

If captures occur more often than the grain length, their live passes overlap and input stays audible until one complete grain can finish without another trigger. This can make very short capture intervals sound largely dry.

Tempo follows the DAW, with a visible 120 BPM fallback when the host supplies no tempo (including standalone use). Synced capture starts follow the host PPQ grid while playing; the first capture starts immediately, then subsequent starts follow the grid. Stopping the DAW silences all capture modes and discards held and unfinished grains. Playback resumes from fresh audio when the DAW starts again. Without host transport information (including standalone use), capture free-runs. Supported tempo is 20-999 BPM; the longest setting is 4 dotted whole notes at 20 BPM (72 seconds).

Size edits discard unfinished captures and begin a recording at the new size. Capture-mode changes and host timeline jumps in synced mode also discard unfinished recordings and start a new live pass. Direction changes affect the existing grain immediately. Short fixed fades smooth loop boundaries, replacements, and direction changes; Back-and-forth reflects at the endpoints. These are de-click fades, not user-adjustable grain envelopes.

Mono and stereo layouts are supported. Stereo audio retains its channels, with linked timing and detection. All controls support host automation and are saved with the session; recorded audio is temporary. In Transient mode the last grain can repeat indefinitely while the transport plays without further triggers, and the VST3 reports an infinite tail accordingly. The host must deliver a transport-stopped processing block for the plugin to clear playback.

Memory is allocated in `prepareToPlay`, with one shared stereo input history for all voices (about 79 MiB at 48 kHz). Per-voice snapshot arrays are allocated without initialising unused samples; only active grain data is written. Supporting 16 voices at the maximum 72-second grain length reserves approximately 959 MiB of buffers and event storage at 48 kHz, doubling at 96 kHz for audio storage. Short grains touch much less snapshot memory. The audio callback allocates no grain buffers and copies snapshots incrementally to keep capture work bounded.

## Build on Windows

Requires a local JUCE checkout in `JUCE/` (ignored by Git), CMake 3.22+, and Visual Studio 2022 with the C++ desktop workload and a Windows SDK.

```powershell
cmake -S . -B build -DRICE_BUILD_TESTS=ON
cmake --build build --config Debug --target rice_VST3 rice_Standalone rice_engine_tests rice_plugin_tests --parallel 4
ctest --test-dir build -C Debug --output-on-failure
```

Outputs:

- VST3 bundle: `build/rice_artefacts/Debug/VST3/rice.vst3`
- Standalone app: `build/rice_artefacts/Debug/Standalone/rice.exe`

For a DAW, copy the entire `rice.vst3` bundle into a VST3 folder scanned by your host, then rescan. Nothing is installed automatically. For optimized builds, replace `Debug` with `Release` in the build command and output paths.

Try a drum loop with a 50-100 ms grain, a synced `1/8` capture interval, and Back-and-forth playback. Then switch capture to Transient and raise sensitivity to refresh on attacks. Future versions can add envelopes, additional voices, panning, detune, and texture processing.

## Validation

The offline engine checks cover the original playback/transport/timing behavior plus 16-voice normalisation, voice-count automation, independent signed random draws, position-shifted source audio, stereo spread, random sync timing through tempo ramps, and clearing history on stop/restart. Plugin integration checks exercise parameter/state restoration (including older sessions), transport forwarding, seven visible knobs, and random-knob unit rebinding. They also render `build/rice-ui.png` for visual inspection. Listening and DAW-specific automation testing remain necessary when evaluating the sound.
