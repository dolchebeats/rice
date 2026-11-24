# rice

<<<<<<< Updated upstream
A JUCE VST3 / standalone granular capture-and-repeat effect with its own live-input capture engine.
=======
JUCE VST3 / standalone granular effect with rhythmic Beat playback, overlapping Texture playback, and a continuous blend between them. Defaults preserve the original single-voice Beat sound. All parameters, envelope curves, step values and modulation routes support host automation and session save/restore. Audio buffers are temporary and are not embedded in presets.
>>>>>>> Stashed changes

## Getting started

Use **Grains & Buffer** for the sound source and timing, **Pitch & Feedback** for harmonies and regeneration, **Envelope** to shape grains, **Motion** for modulation sources, and **Mod Matrix** to connect sources to controls. The waveform stays visible above every tab. Vertical marks indicate capture positions; colored dots indicate grain positions and their envelope levels, grouped by voice.

- Beat / Texture at **0%** retains the live first-pass slice repeater.
- At **100%**, Texture reads recorded source windows and launches independent, overlapping, enveloped grains.
- Intermediate settings linearly crossfade both engines. The mode blend and Dry/Wet controls have 20 ms ramps.
- Density is **grain launches per second per voice**, independent of the capture interval. More voices add independent layers; Density adds overlaps within each layer.

## Grains, capture and buffer

**Grain Size** is 1-1,000 ms or a DAW division. Both size and capture interval support `1/32`, `1/16`, `1/8`, `1/6`, `1/4`, `1/2`, `1`, `2`, `3`, `4` and every triplet (`T`) and dotted (`D`) variation. `1` is a whole note / four quarter-note beats regardless of time signature. Triplets are two-thirds the straight value; dotted values are one-and-a-half times it. Synced durations use host tempo, with a visible 120 BPM fallback. Supported tempo is 20-999 BPM; duration is bounded at 72 seconds.

**Capture / Sensitivity** selects when new source material is chosen: milliseconds, host beat grid, or input transients. In Transient mode the knob becomes sensitivity; higher values detect quieter attacks. Detection is stereo-linked with a fast/slow envelope comparison, a noise floor and 15 ms holdoff. Texture can keep launching grains between captures without passing the dry signal through.

**Beat first pass:** every capture records fresh input and immediately plays the first pass live. It then repeats Straight, Reverse, or Back-and-forth. Reverse therefore has a forward live first pass before looping backward. Back-and-forth reflects after its initial forward pass. When the capture interval is shorter than the grain, live passes overlap and Beat can sound mostly dry; use Texture for a continuous cloud instead. Pitch shifting applies to Beat's recorded repeats, not to the initial live pass.

**Texture capture:** chooses a source window in already-recorded audio. It waits for the first source window to fill, then launches grains using that window independently of subsequent capture starts. Source windows are bounded by the selected buffer and available recorded history. Grain playback wraps or reflects within its source window when the pitched playback rate traverses it; the grain's envelope lifetime is unchanged by pitch.

**Buffer Position** selects how far back to read: 0% is recent audio; 100% is the older end of the chosen history. **Buffer Length** is 1-72 seconds. **Scan Speed** moves the source selection in cycles per second, with signed direction; it also works while frozen. In Beat mode, Position and Scan offset subsequent captures into history. In Texture, Scan affects the source of subsequent grain launches.

**Freeze Buffer** stops updating the visible recording buffer. Texture continues launching grains from this fixed material; Beat holds its captured slice. Freeze also stops feedback from being recorded. Stopping the DAW silences all grain playback. Frozen material is retained in the running instance for a fresh grain restart; unfrozen stop/restart clears the source history. Reloading a session does not restore recorded audio: unfreeze and feed input before freezing again. Without host transport information, including standalone use, the engine free-runs.

**Voices** is 1-16 (default 1). **Spread** distributes voices evenly left to right using equal-power stereo balance, with a single voice centered. The mix is normalised to avoid multiplying peak level as voices or overlaps are added. Mono hosts receive both sides folded down. Voice-count changes fade retiring Texture grains over 5 ms.

**Random Size**, **Random Interval**, and **Random Position** each have independent milliseconds / sync units. Amounts default to zero / Off; ms amounts range from 0 to 1,000 ms; sync includes Off plus every supported division. They draw fresh uniform signed offsets independently per voice. Size and interval draws happen on capture; pitch draws happen on grain launch in Texture and on completed captures in Beat. Streams are deterministic on transport restart. Unit changes remember both amounts.

Random intervals clamp to at least 1 ms. In Transient mode Random Interval jitters the selected audio around each attack. Beat allows positive source offsets to wait for future audio and negative ones to read earlier history. Texture selects existing material and clamps out-of-history offsets to the available window. Randomised duration and total displacement are bounded to the allocated 72-second range. Size edits discard unfinished Beat captures and start a new live pass.

**Dry/Wet** is a linear blend: 0% passes input exactly, 100% is the grain output (default). Grain engines continue running at 0%. Stopping transport clears grain playback, while the dry path can still pass monitored input below 100% wet.

## AHDSR envelope editor

The five controls are **Attack**, **Hold**, **Decay**, **Sustain**, and **Release**. Times are milliseconds; Sustain is a percentage. The envelope fits each grain's lifetime: if A + H + D + R exceeds that lifetime, stage times are scaled proportionally; otherwise the remaining time is sustain.

- Drag **A** horizontally for attack time.
- Drag **H** horizontally for hold time. A zero-length hold uses an offset handle so it remains selectable.
- Drag **D** horizontally for decay time and vertically for sustain level.
- Drag **S** horizontally to move the release start and vertically for sustain level.
- Drag **R** horizontally to change release duration. The graph's right edge remains the grain endpoint.
- Drag a segment's diamond vertically for continuously variable attack, decay or release curvature. Zero is linear; either sign bends in opposite directions. Double-click a diamond to restore linear.

Numeric stage and curve controls provide the same edits, and the graph follows host automation. **Shape Beat Grains** is off by default for compatibility; enable it to envelope Beat's live passes and repeated grains. Texture always uses AHDSR, with a short fixed edge fade as additional de-click protection. Envelope settings are captured for each new Texture grain and each Beat envelope cycle, including while frozen, so edits and modulation apply at grain boundaries rather than rewriting the middle of an envelope.

## Pitch and feedback

**Pitch:** -36 to +36 semitones. **Voice Detune:** up to +/-100 cents across the voice layers. **Random Pitch:** up to +/-24 semitones per grain. Pitch changes the interpolated source playback rate independently of the grain lifetime. Beat pitch and detune edits also affect already-held or frozen slices; Texture pitch settings are captured by each new grain. Scales are Off, Chromatic, Major, Minor, Fifths, and Octaves, with a selectable root. Snapping applies to semitone offsets before fine detune; it does not detect or correct the input's fundamental or preserve vocal formants.

**Feedback** returns up to 95% of the wet output through a high-pass, low-pass and tanh saturation stage to the recording input. **Feedback Saturation** supplies 0-24 dB drive. Filters are clamped below the actual sample-rate Nyquist limit. Dry/Wet does not affect the feedback tap. Freeze stops new buffer recording; transport stop clears the feedback state. Saturation bounds regeneration, but high feedback is deliberately capable of strong, dense effects.

## Motion and routing

Two LFOs have independent free-Hz / tempo-synced rates and Sine, Triangle, Saw, or Square waveforms. Synced phases follow host PPQ, including loops and negative timeline positions; standalone timing uses the fallback clock. The **Envelope Follower** has attack, release and gain controls and follows the dry input peak. The **Step Sequencer** has 1-16 steps, a synced step division, and sixteen bipolar value sliders. **Smooth Random** glides between deterministic random targets at an adjustable rate.

The **Mod Matrix** has eight source / target / signed-depth routes. Sources are LFO 1, LFO 2, Follower, Sequencer and Smooth Random. Targets include all sound knobs, AHDSR stages and curves, LFO rates, follower settings and sequence steps. The unit-specific versions of dual-mode knobs appear separately: choose the active ms or sync destination. Routing selectors and route depths are not recursive destinations. Follower is unipolar; other sources are bipolar. Multiple routes add in normalized parameter space and clamp to the target range. The saved base value stays unchanged. A 32-sample control clock is independent of audio block size; LFO/follower phases are advanced per sample.

## Practical limits

Texture allows 16 simultaneous grains per voice / 256 total at 44.1-48 kHz, 8 per voice at 88.2-96 kHz, and 4 per voice at 192 kHz. These sample-rate-aware limits bound the render cost. Excess launches are skipped rather than allocating or stealing a sounding grain; the waveform displays an overlap-limit message. Reduce Size, Density or Voices if this occurs. Long grains whose source window is overwritten retire with a 5 ms fade; Freeze retains source material indefinitely. Keep Buffer Length comfortably longer than the grain for sustained live clouds.

Grain buffers are preallocated outside the audio callback. The original 16-voice Beat caches and event storage reserve about 976 MiB at 48 kHz; only active cache data is touched. The shared Texture history adds about 27 MiB. Audio storage scales with sample rate; short normal grains use much less physical cache memory than the reserved maximum. Interpolated resampling is intended for granular pitch effects, not formant-preserving or alias-free spectral pitch correction. Dense 16-voice clouds require more CPU than sparse Beat repeats. The Release cloud-only stress benchmark on the development machine processed one second of 48 kHz stereo with 256 active pitched grains in approximately 0.44 seconds; this is not a host CPU or audio-driver latency measurement.

## Build and check on Windows

Requires a local JUCE checkout in `JUCE/` (ignored by Git), CMake 3.22+, Visual Studio 2022 C++ tools, and a Windows SDK.

```powershell
cmake -S . -B build -DRICE_BUILD_TESTS=ON
cmake --build build --config Release --target rice_VST3 rice_Standalone rice_engine_tests rice_advanced_tests rice_plugin_tests --parallel 4
ctest --test-dir build -C Release --output-on-failure
```

- VST3 bundle: `build/rice_artefacts/Release/VST3/rice.vst3`
- Standalone: `build/rice_artefacts/Release/Standalone/rice.exe`

Copy the entire VST3 bundle into a folder scanned by your DAW, then rescan. Nothing is installed automatically. Replace Release with Debug for development builds.

Checks cover original Beat sample order and transport behavior, live first passes, signed position audio, AHDSR boundaries and compiled-curve agreement, independent overlap/capture clocks, freeze and restart, frequency changes at +/-12 semitones, scale snapping, maximum grain-pool bounds, normalisation, modulation sources/routing, callback allocations, variable block sizes, feedback stability, exact dry endpoints, all-parameter state round trips, unit switching, envelope mouse gestures and tab rendering. UI renders are `build/rice-ui.png` and `build/rice-tab-1.png` through `rice-tab-4.png`. A plugin validator, DAW-specific automation and listening tests are still useful before distributing a release.

## Starting sounds

- **Beat chop:** Beat 0%, 50 ms size, synced 1/8 captures, Straight, one voice.
- **Texture cloud:** Texture 100%, 100 ms size, 20 grains/s, four voices, Spread 80%, Attack 10 / Hold 0 / Decay 20 / Sustain 70% / Release 40 ms.
- **Frozen harmony:** record a phrase, Freeze, Texture 100%, scale Fifths, Random Pitch 12 st, Detune 10 cents, slow Scan.
- **Feedback cascade:** Texture 70%, Pitch +7 st, Feedback 40%, high-pass 100 Hz, low-pass 5 kHz, gentle saturation; modulate Position with a slow synced LFO.

The capture / release / buffer distinction and modulation workflow were informed by [Arturia's Efx FRAGMENTS overview](https://www.arturia.com/products/software-effects/efx-fragments/overview). Rhythmic source handling and effect-control conventions were checked against [Ableton's audio effect reference](https://www.ableton.com/en/manual/live-audio-effect-reference/). Rice uses its own DSP and envelope implementation rather than reproducing either product.
