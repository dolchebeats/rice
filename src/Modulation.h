#pragma once
#include "GrainMath.h"
#include "GrainTiming.h"
namespace rice
{
class Modulation
{
public:
    struct Lfo { double hz = 1.0, beats = 1.0; int shape = 0; bool sync = false; };
    struct Settings
    {
        std::array<Lfo, 2> lfos;
        double followerAttack = 10.0, followerRelease = 100.0, randomHz = 1.0, sequenceBeats = 0.25;
        float followerGain = 1.0f;
        int steps = 16;
        std::array<float, 16> sequence {};
    };
    void prepare (double sampleRate) { rate = sampleRate; reset(); }
    void reset() { phase = {}; follower = 0.0f; randomPhase = 0.0; previousRandom = 0.0f; nextRandom = 0.5f; seed = 0x16ea9b41u; }
    void configure (const Settings& value)
    {
        settings = value;
        attack = std::exp (-1.0 / (rate * std::max (0.1, settings.followerAttack) * 0.001));
        release = std::exp (-1.0 / (rate * std::max (1.0, settings.followerRelease) * 0.001));
    }
    std::array<float, 5> process (float input, double ppq)
    {
        std::array<float, 5> result {};
        for (size_t i = 0; i < 2; ++i)
        {
            const auto& lfo = settings.lfos[i];
            const auto p = lfo.sync ? wrap (ppq / std::max (0.001, lfo.beats), 1.0) : phase[i];
            result[i] = lfo.shape == 0 ? static_cast<float> (std::sin (p * 6.28318530718))
                : lfo.shape == 1 ? static_cast<float> (1.0 - 4.0 * std::abs (p - 0.5))
                : lfo.shape == 2 ? static_cast<float> (p * 2.0 - 1.0) : p < 0.5 ? 1.0f : -1.0f;
            phase[i] = wrap (phase[i] + std::clamp (lfo.hz, 0.01, 40.0) / rate, 1.0);
        }
        const auto level = std::abs (input) * settings.followerGain;
        follower = static_cast<float> (level + (level > follower ? attack : release) * (follower - level));
        result[2] = std::clamp (follower, 0.0f, 1.0f);
        const auto count = std::clamp (settings.steps, 1, 16);
        const int index = static_cast<int> (wrap (std::floor (ppq / std::max (0.001, settings.sequenceBeats) + 1.0e-9), count));
        result[3] = settings.sequence[static_cast<size_t> (index)];
        randomPhase += std::clamp (settings.randomHz, 0.01, 40.0) / rate;
        if (randomPhase >= 1.0) { randomPhase -= 1.0; previousRandom = nextRandom; nextRandom = randomBipolar (seed); }
        const auto t = static_cast<float> (randomPhase * randomPhase * (3.0 - 2.0 * randomPhase));
        result[4] = previousRandom + t * (nextRandom - previousRandom);
        return result;
    }
private:
    Settings settings;
    double rate = 44100.0, attack = 0.0, release = 0.0, randomPhase = 0.0;
    std::array<double, 2> phase {};
    float follower = 0.0f, previousRandom = 0.0f, nextRandom = 0.5f;
    std::uint32_t seed = 1;
};
}
