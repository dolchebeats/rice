#include "CloudEngine.h"
#include "Modulation.h"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <atomic>
#include <chrono>
namespace { std::atomic<bool> monitor { false }; std::atomic<size_t> allocations { 0 }; }
void* operator new (std::size_t count)
{ if (monitor.load()) ++allocations; if (auto* p = std::malloc (count == 0 ? 1 : count)) return p; throw std::bad_alloc(); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void* operator new[] (std::size_t n) { return ::operator new (n); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
namespace
{
void check (bool yes, const char* why) { if (! yes) { std::cerr << why << '\n'; std::exit (1); } }
void near (double a, double b, const char* why) { check (std::abs (a - b) < 1.0e-5, why); }
void envelopes()
{
    rice::Envelope e { 10, 10, 20, 20, 0.4f, 0, 0, 0 };
    near (e.value (0, 100), 0, "Envelope begins at zero"); near (e.value (0.05, 100), 0.5, "Linear attack");
    near (e.value (0.15, 100), 1, "Hold at peak"); near (e.value (0.3, 100), 0.7, "Decay interpolates to sustain");
    near (e.value (0.6, 100), 0.4, "Sustain level"); near (e.value (0.9, 100), 0.2, "Release"); near (e.value (1, 100), 0, "Envelope ends at zero");
    e.attackCurve = 0.7f; check (e.value (0.05, 100) > 0.5f, "Positive attack curvature bends above linear");
    e.attackCurve = -0.7f; check (e.value (0.05, 100) < 0.5f, "Negative curvature bends below linear");
    rice::CompiledEnvelope compiled; compiled.set (e, 100);
    for (int i = 0; i < 1000; ++i) near (compiled.value (i * 0.001), e.value (i * 0.001, 100), "Compiled audio envelope agrees with the editor curve");
    for (const auto duration : { 1.0, 10.0, 100.0, 10000.0 })
        for (int i = 0; i <= 1000; ++i) { const auto v = e.value (i * 0.001, duration); check (std::isfinite (v) && v >= 0 && v <= 1, "Scaled envelopes remain finite and bounded"); }
    e = { 0, 0, 0, 0, 1, 0, 0, 0 }; near (e.value (0.5, 1), 1, "Zero-length stages are safe");
    near (rice::pitchRatio (12, 0, 0, 0, 0, 0), 2, "Octave up is double speed");
    near (rice::pitchRatio (-12, 0, 0, 0, 0, 0), 0.5, "Octave down is half speed");
    near (rice::quantizePitch (1.9f, 0, 2), 2, "Major scale snaps to an allowed note");
    near (rice::quantizePitch (1, 2, 2), 1, "Scale root transposes allowed pitch classes");
    near (rice::quantizePitch (8.2f, 0, 4), 7, "Fifths scale"); near (rice::quantizePitch (11.8f, 0, 5), 12, "Octave scale");
}
rice::CloudEngine::Settings defaults()
{
    rice::CloudEngine::Settings s; s.grain.grainSamples = 100; s.grain.intervalSamples = 400; s.grain.intervalBeats = 0.8;
    s.grain.envelope = { 5, 0, 10, 20, 0.7f, 0, 0, 0 }; s.density = 40; s.bufferSeconds = 2; return s;
}
void clouds()
{
    rice::CloudEngine engine; engine.prepare (1000); auto s = defaults();
    engine.configure (s, 0, 0.002, false, true);
    for (int i = 0; i < 1000; ++i)
    {
        const auto out = engine.process ({ 0.5f, -0.5f });
        check (std::isfinite (out[0]) && std::abs (out[0]) <= 0.50001f && std::abs (out[1]) <= 0.50001f, "Cloud overlap is normalised and stereo-safe");
    }
    check (engine.launches() > engine.captures() * 5, "Launch density is independent of capture interval");
    check (engine.activeGrains() >= 3, "True simultaneous overlap");
    const auto recorded = engine.recordedFrames(); s.grain.freeze = true; s.scanHz = 0.25;
    engine.configure (s, 2, 0.002, false, true);
    double energy = 0;
    monitor.store (true);
    for (int i = 0; i < 1000; ++i) energy += std::abs (engine.process ({ 0, 0 })[0]);
    monitor.store (false);
    check (allocations.load() == 0, "Grain scheduling and rendering allocate no memory in the audio loop");
    check (engine.recordedFrames() == recorded, "Freeze stops buffer writes"); check (energy > 1, "Frozen audio keeps producing grains"); near (engine.scanPosition(), 0.25, "Scan moves while frozen");
    engine.configure (s, 4, 0.002, false, false); near (engine.process ({ 1, 1 })[0], 0, "Stop silences frozen cloud");
    engine.configure (s, 0, 0.002, false, true); energy = 0;
    for (int i = 0; i < 500; ++i) energy += std::abs (engine.process ({ 0, 0 })[0]);
    check (energy > 1, "Frozen buffer survives a stop/restart, with fresh grains");
    s.grain.freeze = false; engine.configure (s, 1, 0.002, false, false); engine.configure (s, 0, 0.002, false, true);
    for (int i = 0; i < 500; ++i) near (engine.process ({ 0, 0 })[0], 0, "Nonfrozen restart cannot access stale history");
    engine.reset(); s = defaults(); s.grain.voiceCount = 16; s.grain.grainSamples = 1000; s.density = 200; s.grain.spread = 1;
    s.grain.randomPitch = 24; s.grain.detune = 100; s.grain.scale = 2;
    engine.configure (s, -2, 0.002, true, true);
    for (int i = 0; i < 3000; ++i)
    {
        const auto out = engine.process ({ std::sin (i * 0.1f), std::cos (i * 0.1f) });
        check (std::isfinite (out[0]) && std::abs (out[0]) <= 1.0001f && std::abs (out[1]) <= 1.0001f, "Maximum density, spread and pitched voices stay bounded");
    }
    rice::VisualState visual; engine.publish (visual);
    check (engine.activeGrains() <= rice::CloudEngine::maxGrains && visual.dropped.load() > 0, "Bounded grain pool skips excess launches safely");
    check (visual.available.load() > 0 && visual.active.load() > 0, "Waveform and grain telemetry is populated");
    engine.reset(); s = defaults(); engine.configure (s, 0, 0.002, false, true);
    for (int i = 0; i < 400; ++i) engine.process ({ 0, 0.4f });
    engine.publish (visual); float peak = 0; for (const auto& bin : visual.waveform) peak = std::max (peak, bin.load());
    check (peak > 0.3f, "Waveform includes right-only stereo input");
}
int pitchCrossings (float pitch)
{
    rice::CloudEngine e; e.prepare (1000); auto s = defaults(); s.grain.grainSamples = 250; s.density = 4;
    s.grain.envelope = { 0, 0, 0, 0, 1, 0, 0, 0 }; s.grain.intervalSamples = 250; e.configure (s, 0, 0.002, false, true);
    for (int i = 0; i < 1000; ++i) e.process ({ std::sin (i * 6.28318530718f * 0.016f), 0 });
    s.grain.freeze = true; s.grain.pitch = pitch; e.configure (s, 2, 0.002, false, true);
    for (int i = 0; i < 500; ++i) e.process ({ 0, 0 });
    int count = 0; float previous = 0;
    for (int i = 0; i < 2000; ++i) { const auto v = e.process ({ 0, 0 })[0]; if (previous <= 0 && v > 0) ++count; previous = v; }
    return count;
}
void motion()
{
    rice::Modulation m; m.prepare (1000); rice::Modulation::Settings s;
    s.sequence[0] = -1; s.sequence[1] = 0.25f; s.sequence[2] = 1; s.steps = 3; s.sequenceBeats = 0.25;
    s.lfos[1].sync = true; s.lfos[1].beats = 1; s.followerAttack = 5; s.followerRelease = 100;
    m.configure (s); auto frame = m.process (0, 0.25); near (frame[1], 1, "LFO follows host PPQ"); near (frame[3], 0.25, "Sequencer follows beat division");
    near (m.process (0, -0.25)[3], 1, "Sequencer wraps negative host positions");
    for (int i = 0; i < 10; ++i) frame = m.process (1, i * 0.002);
    check (frame[2] > 0.8f && frame[2] < 1, "Follower attack responds to input");
    const auto high = frame[2]; frame = m.process (0, 0); check (frame[2] < high && frame[2] > 0.7f, "Follower release decays smoothly");
    auto previous = frame[4];
    for (int i = 0; i < 4000; ++i)
    { frame = m.process (0, i * 0.002); check (std::abs (frame[4] - previous) < 0.01f, "Smooth random has no stepped discontinuities"); previous = frame[4]; }
    check (std::abs (pitchCrossings (0) - 32) <= 3, "Unpitched frozen grain frequency");
    check (std::abs (pitchCrossings (12) - 64) <= 3, "Pitch changes frequency while grain envelope duration stays fixed");
    check (std::abs (pitchCrossings (-12) - 16) <= 3, "Octave-down resampling");
}
}
int main (int argc, char**)
{
    envelopes(); clouds(); motion(); std::cout << "Advanced DSP checks passed.\n";
    if (argc > 1)
    {
        rice::CloudEngine e; e.prepare (48000); auto s = defaults(); s.grain.voiceCount = 16; s.grain.grainSamples = 24000; s.grain.intervalSamples = 24000;
        s.density = 200; s.grain.randomPitch = 24; s.grain.envelope.attackCurve = 0.4f; e.configure (s, 0, 0.00004, false, true);
        for (int i = 0; i < 48000; ++i) e.process ({ std::sin (i * 0.02f), std::cos (i * 0.02f) });
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 48000; ++i) e.process ({ std::sin (i * 0.02f), std::cos (i * 0.02f) });
        std::cout << "Dense-cloud benchmark: " << std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count()
                  << " processing seconds / one audio second; " << e.activeGrains() << " active grains.\n";
    }
}
