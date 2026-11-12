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
        for (int i = 0; i < 150000; ++i)
        {
            const auto expected = mode == rice::GrainEngine::Repeat::straight ? i % 4 + 1
                : mode == rice::GrainEngine::Repeat::reverse ? 4 - i % 4 : pingPong[i % 6];
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
}

int main()
{
    playbackOrder();
    overlappingCaptures();
    hostScheduling();
    timingAndEdges();
    std::cout << "All grain-engine checks passed.\n";
}
