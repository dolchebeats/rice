#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace rice
{
class GrainEngine
{
public:
    enum class Repeat { straight, reverse, pingPong };
    enum class Capture { milliseconds, synced, transient };
    struct Settings
    {
        int grainSamples = 1;
        double intervalSamples = 1.0, intervalBeats = 1.0;
        float sensitivity = 0.5f;
        Repeat repeat = Repeat::straight;
        Capture capture = Capture::milliseconds;
    };
    void prepare (double sampleRate);
    void reset();
    void cancelPendingCaptures();
    void beginBlock (const Settings&, double ppq, double beatsPerSample, bool followHost);
    std::array<float, 2> process (std::array<float, 2> input);
    std::uint64_t getCaptureCount() const noexcept { return captureCount; }
    int getActiveLength() const noexcept { return voices[currentVoice].length; }

private:
    struct Voice
    {
        std::array<std::vector<float>, 2> cache;
        int start = 0, length = 0, copied = 0, phase = 0;
        Repeat repeat = Repeat::straight;
        int oldPhase = 0, modeFadeRemaining = 0;
        Repeat oldRepeat = Repeat::straight;
    };
    struct Pending
    {
        std::uint64_t due;
        int start, length;
        bool operator< (const Pending& other) const noexcept { return due > other.due; }
    };
    void requestCapture();
    void activate (const Pending&);
    std::array<float, 2> render (Voice&);
    float read (const Voice&, int channel, int index) const;
    bool detectTransient (float level);
    std::array<std::vector<float>, 2> history;
    std::array<Voice, 2> voices;
    std::vector<Pending> pending;
    Settings settings;
    double rate = 44100.0, countdown = 0.0, ppqPosition = 0.0, ppqStep = 0.0;
    double expectedPpq = 0.0, lastGrid = 0.0;
    float fastEnvelope = 0.0f, slowEnvelope = 0.0f;
    float fastAttack = 0.0f, fastRelease = 0.0f, slowAttack = 0.0f, slowRelease = 0.0f;
    float transientRatio = 3.1f, transientFloor = 0.003f;
    int capacity = 0, maximumGrain = 0, writePosition = 0, currentVoice = 0;
    int fadeLength = 1, fadeRemaining = 0, transientHoldoff = 0;
    bool hostSync = false, wasHostSync = false, firstBlock = true, forceCapture = true, transientArmed = true;
    std::uint64_t clock = 0, captureCount = 0;
};
}
