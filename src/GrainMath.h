#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace rice
{
inline double wrap (double x, double length) { return length > 0.0 ? x - std::floor (x / length) * length : 0.0; }
inline float randomBipolar (std::uint32_t& seed)
{
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return static_cast<float> (static_cast<double> (seed) / 4294967295.0 * 2.0 - 1.0);
}
struct Envelope
{
    double attack = 5.0, hold = 0.0, decay = 30.0, release = 30.0;
    float sustain = 0.7f, attackCurve = 0.0f, decayCurve = 0.0f, releaseCurve = 0.0f;
    std::array<double, 6> points (double durationMs) const
    {
        const auto a = std::max (0.0, attack), h = std::max (0.0, hold), d = std::max (0.0, decay), r = std::max (0.0, release);
        const auto total = std::max (0.001, durationMs);
        const auto scale = std::min (1.0, total / std::max (0.001, a + h + d + r));
        return { 0.0, a * scale / total, (a + h) * scale / total, (a + h + d) * scale / total, 1.0 - r * scale / total, 1.0 };
    }
    static float curve (double phase, float bend)
    {
        return std::pow (static_cast<float> (std::clamp (phase, 0.0, 1.0)), std::exp2 (-std::clamp (bend, -1.0f, 1.0f) * 4.0f));
    }
    float value (double phase, double durationMs) const
    {
        if (phase < 0.0 || phase >= 1.0) return 0.0f;
        const auto p = points (durationMs);
        const auto s = std::clamp (sustain, 0.0f, 1.0f);
        if (phase < p[1]) return curve (phase / p[1], attackCurve);
        if (phase < p[2]) return 1.0f;
        if (phase < p[3]) return 1.0f + (s - 1.0f) * curve ((phase - p[2]) / (p[3] - p[2]), decayCurve);
        if (phase < p[4]) return s;
        return s * (1.0f - curve ((phase - p[4]) / std::max (1.0e-12, 1.0 - p[4]), releaseCurve));
    }
};
inline float quantizePitch (float pitch, int root, int mode)
{
    if (mode == 0) return pitch;
    static constexpr std::array<int, 7> major { 0, 2, 4, 5, 7, 9, 11 }, minor { 0, 2, 3, 5, 7, 8, 10 };
    float best = std::round (pitch), distance = 1000.0f;
    for (int note = -72; note <= 72; ++note)
    {
        const int pc = ((note - root) % 12 + 12) % 12;
        bool allowed = mode == 1;
        if (mode == 2 || mode == 3)
        {
            const auto& scale = mode == 2 ? major : minor;
            allowed = std::find (scale.begin(), scale.end(), pc) != scale.end();
        }
        if (mode == 4) allowed = pc == 0 || pc == 7;
        if (mode == 5) allowed = pc == 0;
        const auto delta = std::abs (pitch - static_cast<float> (note));
        if (allowed && delta < distance) { best = static_cast<float> (note); distance = delta; }
    }
    return best;
}
struct CompiledEnvelope
{
    std::array<double, 6> points {};
    std::array<float, 3> exponents { 1, 1, 1 };
    float sustain = 1.0f;
    void set (const Envelope& e, double duration)
    {
        points = e.points (duration); sustain = std::clamp (e.sustain, 0.0f, 1.0f);
        exponents = { std::exp2 (-std::clamp (e.attackCurve, -1.0f, 1.0f) * 4.0f), std::exp2 (-std::clamp (e.decayCurve, -1.0f, 1.0f) * 4.0f), std::exp2 (-std::clamp (e.releaseCurve, -1.0f, 1.0f) * 4.0f) };
    }
    float value (double phase) const
    {
        if (phase < 0.0 || phase >= 1.0) return 0.0f;
        const auto shape = [this] (double x, size_t index)
        { const auto t = static_cast<float> (std::clamp (x, 0.0, 1.0)); return exponents[index] == 1.0f ? t : std::pow (t, exponents[index]); };
        if (phase < points[1]) return shape (phase / points[1], 0);
        if (phase < points[2]) return 1.0f;
        if (phase < points[3]) return 1.0f + (sustain - 1.0f) * shape ((phase - points[2]) / (points[3] - points[2]), 1);
        if (phase < points[4]) return sustain;
        return sustain * (1.0f - shape ((phase - points[4]) / std::max (1.0e-12, 1.0 - points[4]), 2));
    }
};
inline double pitchRatio (float semitones, float cents, float randomPitch, float random, int root, int scale)
{
    return std::exp2 ((quantizePitch (std::clamp (semitones + randomPitch * random, -48.0f, 48.0f), root, scale) + cents * 0.01f) / 12.0);
}
}
