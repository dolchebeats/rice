#include "GrainEngine.h"
#include "GrainTiming.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace
{
void check (bool success, const std::string& message)
{
    if (! success)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit (1);
    }
}

void checkNear (double actual, double expected, const std::string& message)
{
    check (std::abs (actual - expected) < 1.0e-5, message);
}

void playbackOrder()
{
    for (const auto mode : { rice::GrainEngine::Repeat::straight, rice::GrainEngine::Repeat::reverse, rice::GrainEngine::Repeat::pingPong })
    {
        rice::GrainEngine engine;
        engine.prepare (1000.0);
        rice::GrainEngine::Settings settings;
        settings.grainSamples = 4;
        settings.intervalSamples = 1000000.0;
        settings.repeat = mode;
        engine.beginBlock (settings, 0.0, 0.002, false);
        for (int i = 0; i < 4; ++i)
        {
            const auto out = engine.process ({ static_cast<float> (i + 1), static_cast<float> (-2 * (i + 1)) });
            checkNear (out[0], i + 1, "Live input passes through while first grain records");
        }
        const int pingPong[] { 1, 2, 3, 4, 3, 2 };
        // Run longer than the entire rolling history: held grains must stay immutable.
        for (int i = 0; i < 240000; ++i)
        {
            const auto expected = mode == rice::GrainEngine::Repeat::straight ? i % 4 + 1
                : mode == rice::GrainEngine::Repeat::reverse ? 4 - i % 4 : pingPong[(i + 4) % 6];
            const auto out = engine.process ({ 0.0f, 0.0f });
            checkNear (out[0], expected, "Playback direction and held snapshot");
            checkNear (out[1], -2 * expected, "Stereo channels share timing, preserve audio");
        }
        engine.reset();
        check (engine.getCaptureCount() == 0 && engine.getActiveLength() == 0, "Reset clears grains");
    }
}

void overlappingCaptures()
{
    rice::GrainEngine engine;
    engine.prepare (48000.0);
    rice::GrainEngine::Settings settings;
    settings.grainSamples = 48000;
    settings.intervalSamples = 48.0;
    engine.beginBlock (settings, 0.0, 0.0, false);
    for (int i = 0; i < 96000; ++i)
    {
        const auto out = engine.process ({ std::sin (static_cast<float> (i) * 0.1f), 0.0f });
        check (std::isfinite (out[0]) && std::abs (out[0]) <= 1.00001f, "Overlapping captures stay bounded");
    }
    check (engine.getCaptureCount() == 1000, "1 ms refresh remains independent of a 1000 ms grain");
    check (engine.getActiveLength() == 48000, "Captured grain retains requested length");
}

void hostScheduling()
{
    rice::GrainEngine engine;
    engine.prepare (1000.0);
    rice::GrainEngine::Settings settings;
    settings.grainSamples = 4;
    settings.intervalSamples = 125.0;
    settings.intervalBeats = 0.25;
    settings.capture = rice::GrainEngine::Capture::synced;
    double ppq = 0.1;
    for (int block = 0; block < 10; ++block)
    {
        engine.beginBlock (settings, ppq, 0.002, true);
        for (int sample = 0; sample < 100; ++sample)
            engine.process ({ 0.1f, 0.1f });
        ppq += 0.2;
    }
    check (engine.getCaptureCount() == 9, "Host PPQ grid remains stable across blocks starting off-grid");
    const auto beforeSeek = engine.getCaptureCount();
    engine.beginBlock (settings, -2.0, 0.004, true);
    for (int sample = 0; sample < 130; ++sample)
        engine.process ({ 0.1f, 0.1f });
    check (engine.getCaptureCount() == beforeSeek + 3, "Seek, negative PPQ and tempo change re-anchor the grid");

    engine.reset();
    ppq = 0.0;
    for (int block = 0; block < 20; ++block)
    {
        const auto step = 0.002 + block * 0.00001;
        settings.intervalSamples = settings.intervalBeats / step;
        engine.beginBlock (settings, ppq, step, true);
        for (int sample = 0; sample < 10; ++sample)
            engine.process ({ 0.1f, 0.1f });
        ppq += step * 10.0;
    }
    check (engine.getCaptureCount() == 2, "Tempo ramps do not introduce extra block-rate captures");
}

void transportAndLivePass()
{
    for (const auto mode : { rice::GrainEngine::Repeat::straight, rice::GrainEngine::Repeat::reverse, rice::GrainEngine::Repeat::pingPong })
    {
        rice::GrainEngine engine;
        engine.prepare (1000.0); // One-sample de-click fades make sample order exact.
        rice::GrainEngine::Settings settings;
        settings.grainSamples = 4;
        settings.intervalSamples = 20.0;
        settings.repeat = mode;
        engine.beginBlock (settings, 0.0, 0.0, false);
        for (int i = 0; i < 20; ++i)
            engine.process ({ 0.25f, -0.25f });
        // The second capture must be audible immediately, even with an old loop.
        for (int i = 0; i < 4; ++i)
        {
            const auto out = engine.process ({ static_cast<float> (i + 1), static_cast<float> (-i - 1) });
            checkNear (out[0], i + 1, "Every capture has an immediate live first pass");
            checkNear (out[1], -i - 1, "Live first pass preserves stereo without summing dry and wet");
        }
        const auto next = engine.process ({ 99.0f, 99.0f });
        checkNear (next[0], mode == rice::GrainEngine::Repeat::straight ? 1.0 : mode == rice::GrainEngine::Repeat::reverse ? 4.0 : 3.0,
                   "After the live pass, playback follows the selected repeat mode");
        settings.capture = rice::GrainEngine::Capture::transient;
        engine.beginBlock (settings, 0.0, 0.0, false, false);
        for (int block = 0; block < 10; ++block)
        {
            engine.beginBlock (settings, 0.0, 0.0, false, false);
            for (int i = 0; i < 100; ++i)
            {
                const auto out = engine.process ({ 0.75f, -0.75f });
                checkNear (out[0], 0.0, "Stopped transport silences the grain and live path");
                checkNear (out[1], 0.0, "Stopped transport silences both channels");
            }
        }
        check (engine.getCaptureCount() == 0 && engine.getActiveLength() == 0, "Stop clears held and pending captures");
        engine.beginBlock (settings, 0.0, 0.0, false, true);
        for (int i = 0; i < 4; ++i)
            checkNear (engine.process ({ 0.5f, -0.5f })[0], 0.5, "Restart uses fresh live input");
        for (int i = 0; i < 12; ++i)
            check (std::abs (engine.process ({ 0.0f, 0.0f })[0]) <= 0.50001f, "Old grains never return after restart");
    }

    // Stop before the first recording completes: nothing may activate on restart.
    rice::GrainEngine engine;
    engine.prepare (1000.0);
    rice::GrainEngine::Settings settings;
    settings.grainSamples = 100;
    settings.capture = rice::GrainEngine::Capture::transient;
    engine.beginBlock (settings, 0.0, 0.0, false);
    for (int i = 0; i < 50; ++i)
        engine.process ({ 0.7f, 0.7f });
    engine.beginBlock (settings, 0.0, 0.0, false, false);
    engine.beginBlock (settings, 0.0, 0.0, false, true);
    for (int i = 0; i < 250; ++i)
        checkNear (engine.process ({ 0.0f, 0.0f })[0], 0.0, "Stop discards unfinished recordings");
}

std::uint64_t transientCount (float sensitivity, bool pulses)
{
    rice::GrainEngine engine;
    engine.prepare (1000.0);
    rice::GrainEngine::Settings settings;
    settings.grainSamples = 10;
    settings.capture = rice::GrainEngine::Capture::transient;
    settings.sensitivity = sensitivity;
    engine.beginBlock (settings, 0.0, 0.0, false);
    for (int i = 0; i < 1200; ++i)
    {
        const auto input = pulses && i % 300 == 100 ? 0.005f : 0.0f;
        engine.process ({ input, input });
    }
    return engine.getCaptureCount();
}

void timingAndEdges()
{
    check (rice::divisions().size() == 30, "All ten divisions have straight, triplet and dotted values");
    for (const auto* name : { "1/6", "1/32", "1/16", "1/8", "1/4", "1/2", "1", "2", "3", "4" })
    {
        check (rice::divisions()[static_cast<size_t> (rice::divisionIndex (name))].name == name, "Requested division exists");
        const auto base = rice::millisecondsForDivision (rice::divisionIndex (name), 120.0);
        checkNear (rice::millisecondsForDivision (rice::divisionIndex (std::string (name) + " T"), 120.0), base * 2.0 / 3.0, "Triplet duration");
        checkNear (rice::millisecondsForDivision (rice::divisionIndex (std::string (name) + " D"), 120.0), base * 1.5, "Dotted duration");
    }
    checkNear (rice::millisecondsForDivision (rice::divisionIndex ("4 D"), 20.0), 72000.0, "Longest synced grain");
    checkNear (rice::millisecondsForDivision (rice::divisionIndex ("1/4"), 120.0), 500.0, "Quarter note at fallback tempo");
    check (transientCount (1.0f, false) == 1, "Silence does not retrigger transients");
    check (transientCount (0.0f, true) == 1, "Low sensitivity ignores quiet attacks");
    check (transientCount (1.0f, true) == 5, "High sensitivity captures each quiet attack and re-arms");
    for (const auto sampleRate : { 44100.0, 96000.0 })
    {
        rice::GrainEngine engine;
        engine.prepare (sampleRate);
        rice::GrainEngine::Settings settings;
        settings.grainSamples = static_cast<int> (std::round (sampleRate * 0.001));
        settings.intervalSamples = sampleRate * 0.001;
        for (int mode = 0; mode < 3; ++mode)
        {
            settings.repeat = static_cast<rice::GrainEngine::Repeat> (mode);
            engine.beginBlock (settings, 0.0, 0.0, false);
            for (int i = 0; i < 1000; ++i)
            {
                const auto out = engine.process ({ std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() });
                check (std::isfinite (out[0]) && std::isfinite (out[1]), "Minimum grain, mode automation and nonfinite input stay safe");
            }
        }
    }
}

void unisonAndRandomisation()
{
    rice::GrainEngine engine;
    engine.prepare (1000.0);
    rice::GrainEngine::Settings settings;
    settings.grainSamples = 20;
    settings.intervalSamples = 70.0;
    settings.voiceCount = 16;
    engine.beginBlock (settings, 0.0, 0.0, false);
    for (int i = 0; i < 500; ++i)
    {
        const auto out = engine.process ({ 0.5f, -0.25f });
        check (std::abs (out[0]) <= 0.50001f && std::abs (out[1]) <= 0.25001f, "Sixteen identical voices do not multiply the output level");
    }
    for (int i = 1; i < 16; ++i)
        check (engine.getVoiceInfo (i).requests == engine.getVoiceInfo (0).requests, "Zero randomness keeps every voice on the same capture schedule");
    for (const int voices : { 1, 16, 2, 9, 1 })
    {
        settings.voiceCount = voices;
        settings.spread = 1.0f;
        engine.beginBlock (settings, 0.0, 0.0, false);
        for (int i = 0; i < 200; ++i)
        {
            const auto out = engine.process ({ 0.5f, 0.5f });
            check (std::abs (out[0]) <= 0.50001f && std::abs (out[1]) <= 0.50001f, "Automating voice count and spread keeps the mix bounded");
        }
    }
    engine.reset();
    settings.voiceCount = 16;
    settings.spread = 0.0f;
    settings.randomSizeSamples = 9.0;
    settings.randomIntervalSamples = 25.0;
    settings.randomPositionSamples = 15.0;
    engine.beginBlock (settings, 0.0, 0.0, false);
    bool differentSizes = false, differentTimes = false, positive = false, negative = false;
    double stereoDifferenceCentered = 0.0;
    for (int i = 0; i < 3000; ++i)
    {
        const auto input = std::sin (static_cast<float> (i) * 0.17f);
        const auto out = engine.process ({ input, input });
        check (std::isfinite (out[0]) && std::abs (out[0]) <= 1.00001f && std::abs (out[1]) <= 1.00001f, "Randomised polyphonic audio is finite and bounded");
        stereoDifferenceCentered += std::abs (out[0] - out[1]);
        for (int voice = 0; voice < 16; ++voice)
        {
            const auto info = engine.getVoiceInfo (voice);
            check (info.length >= 11 && info.length <= 29, "Random Size stays inside the signed amount");
            check (info.interval >= 45.0 && info.interval <= 95.0, "Random Interval stays inside the signed amount");
            check (std::abs (info.positionOffset) <= 15, "Random Position stays inside the signed amount");
            differentSizes = differentSizes || info.length != engine.getVoiceInfo (0).length;
            differentTimes = differentTimes || info.requests != engine.getVoiceInfo (0).requests;
            positive = positive || info.positionOffset > 0;
            negative = negative || info.positionOffset < 0;
        }
    }
    check (differentSizes && differentTimes && positive && negative, "Voices draw independent lengths, intervals, and both position signs");
    check (stereoDifferenceCentered < 0.001, "Zero spread centers mono material");
    settings.spread = 1.0f;
    engine.beginBlock (settings, 0.0, 0.0, false);
    double stereoDifferenceSpread = 0.0;
    for (int i = 0; i < 1000; ++i)
    {
        const auto input = std::sin (static_cast<float> (i) * 0.17f);
        const auto out = engine.process ({ input, input });
        stereoDifferenceSpread += std::abs (out[0] - out[1]);
    }
    check (stereoDifferenceSpread > 1.0, "Spread creates different left/right signals from independent mono voices");

    engine.reset();
    settings.capture = rice::GrainEngine::Capture::synced;
    settings.intervalBeats = 0.14;
    double ppq = -1.0;
    for (int block = 0; block < 40; ++block)
    {
        const double step = 0.002 + block * 0.000005;
        settings.intervalSamples = settings.intervalBeats / step;
        settings.randomIntervalSamples = 0.05 / step;
        engine.beginBlock (settings, ppq, step, true);
        for (int i = 0; i < 50; ++i)
        {
            const auto out = engine.process ({ 0.1f, 0.1f });
            check (std::isfinite (out[0]) && std::abs (out[0]) <= 0.10001f, "Random synced scheduling survives negative PPQ and tempo ramps");
        }
        ppq += step * 50.0;
    }
    check (engine.getVoiceInfo (0).requests < 40, "Synced random amounts do not force a capture on each tempo-ramp block");
    settings.capture = rice::GrainEngine::Capture::transient;
    engine.beginBlock (settings, 0.0, 0.0, false, false);
    for (int i = 0; i < 200; ++i)
        checkNear (engine.process ({ 1.0f, 1.0f })[0], 0.0, "Stopping transport silences every randomised voice");
    engine.beginBlock (settings, 0.0, 0.0, false, true);
    for (int i = 0; i < 200; ++i)
        checkNear (engine.process ({ 0.0f, 0.0f })[0], 0.0, "Restart cannot read pre-stop history at negative random positions");
}

void positionSourceAudio()
{
    rice::GrainEngine engine;
    engine.prepare (1000.0);
    rice::GrainEngine::Settings settings;
    settings.grainSamples = 4;
    settings.intervalSamples = 1000000.0;
    settings.randomPositionSamples = 12.0;
    engine.beginBlock (settings, 0.0, 0.0, false);
    int time = 0;
    const auto tick = [&engine, &time]
    {
        const auto value = static_cast<float> (time++) * 0.001f;
        return engine.process ({ value, -value });
    };
    for (; time < 100;) tick();
    bool earlier = false, later = false;
    for (int capture = 0; capture < 30; ++capture)
    {
        const auto start = time;
        engine.cancelPendingCaptures();
        const auto first = tick();
        const auto offset = static_cast<int> (engine.getVoiceInfo (0).positionOffset);
        earlier = earlier || offset < 0;
        later = later || offset > 0;
        const auto liveBegin = std::max (0, offset);
        if (liveBegin == 0)
            checkNear (first[0], static_cast<double> (start + offset) * 0.001, "Negative position reads actual earlier audio");
        for (int sample = 1; sample <= liveBegin + 4; ++sample)
        {
            const auto out = tick();
            if (sample >= liveBegin && sample < liveBegin + 4)
                checkNear (out[0], static_cast<double> (start + offset + sample - liveBegin) * 0.001, "Position shifts the source audio, with causal waiting for positive offsets");
            if (sample == liveBegin + 4)
                checkNear (out[0], static_cast<double> (start + offset) * 0.001, "The completed grain loops the same shifted source");
        }
    }
    check (earlier && later, "Position source test exercises both signed directions");
}
}

int main()
{
    playbackOrder();
    overlappingCaptures();
    hostScheduling();
    transportAndLivePass();
    timingAndEdges();
    unisonAndRandomisation();
    positionSourceAudio();
    std::cout << "All grain-engine checks passed.\n";
}
