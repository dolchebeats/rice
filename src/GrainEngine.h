#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <vector>
#include "GrainMath.h"

namespace rice
{
struct VisualState;
class GrainEngine
{
public:
    static constexpr int maxVoices = 16;
    enum class Repeat { straight, reverse, pingPong };
    enum class Capture { milliseconds, synced, transient };
    struct Settings
    {
        int grainSamples = 1;
        double intervalSamples = 1.0, intervalBeats = 1.0;
        float sensitivity = 0.5f;
        Repeat repeat = Repeat::straight;
        Capture capture = Capture::milliseconds;
        int voiceCount = 1;
        float spread = 0.0f;
        double randomSizeSamples = 0.0, randomIntervalSamples = 0.0, randomPositionSamples = 0.0;
        Envelope envelope;
        bool envelopeEnabled = false, freeze = false, modulating = false;
        double positionSamples = 0.0;
        float pitch = 0.0f, detune = 0.0f, randomPitch = 0.0f;
        int root = 0, scale = 0;
    };
    struct VoiceInfo
    {
        int length = 0;
        std::int64_t positionOffset = 0;
        double interval = 0.0;
        std::uint64_t requests = 0;
    };
    void prepare (double sampleRate);
    void reset();
    void cancelPendingCaptures();
    void beginBlock (const Settings&, double ppq, double beatsPerSample, bool followHost, bool transportPlaying = true);
    std::array<float, 2> process (std::array<float, 2> input);
    std::uint64_t getCaptureCount() const noexcept { return captureCount; }
    int getActiveLength() const noexcept { return lanes[0].heads[lanes[0].current].length; }
    double getPlaybackRate (int index = 0) const { const auto& lane = lanes[static_cast<size_t> (index)]; return lane.heads[static_cast<size_t> (lane.current)].speed; }
    VoiceInfo getVoiceInfo (int index) const noexcept { return lanes[static_cast<size_t> (index)].info; }
    void publishMarkers (VisualState&, std::uint64_t recorded, double availableSamples, bool append) const;
    void setVisualClock (std::uint64_t timestamp) { visualOffset = static_cast<double> (timestamp) - static_cast<double> (clock); }

private:
    struct Head
    {
        // Uninitialised storage: only recorded samples are touched/read. Long sync
        // settings reserve address space without eagerly filling every voice cache.
        std::array<std::unique_ptr<float[]>, 2> cache;
        int start = 0, length = 0, copied = 0;
        double phase = 0.0, speed = 1.0, oldPhase = 0.0;
        int envelopeAge = 0;
        Envelope envelope;
        bool envelopeEnabled = false;
        CompiledEnvelope contour;
        std::uint64_t sourceTime = 0;
        int frozenOwner = -1, aliasFade = 0;
        float randomPitchDraw = 0.0f;
        Repeat repeat = Repeat::straight;
        int modeFadeRemaining = 0;
        Repeat oldRepeat = Repeat::straight;
    };
    struct Pending
    {
        std::uint64_t due;
        int start, length;
        std::uint64_t sourceTime = 0;
        bool operator< (const Pending& other) const noexcept { return due > other.due; }
    };
    struct Lane
    {
        std::array<Head, 2> heads;
        std::vector<Pending> pending;
        int current = 0, fadeRemaining = 0;
        double countdown = 0.0, nextBeat = 0.0;
        std::uint64_t liveBegin = 0, liveUntil = 0, sourceStart = 0;
        std::uint64_t displayStart = 0;
        std::uint32_t rng = 1;
        float liveMix = 0.0f, gainL = 0.0f, gainR = 0.0f, targetL = 0.0f, targetR = 0.0f;
        bool force = true, enabled = false;
        VoiceInfo info;
    };
    void clearLane (Lane&, int index);
    double randomSigned (Lane&, double amount);
    double nextInterval (Lane&);
    void requestCapture (Lane&, double transientJitter = 0.0);
    void activate (Lane&, const Pending&);
    std::array<float, 2> render (Head&);
    std::array<float, 2> renderLane (Lane&);
    float read (const Head&, int channel, int index) const;
    bool detectTransient (float level);
    std::array<std::vector<float>, 2> history;
    std::array<Lane, maxVoices> lanes;
    Settings settings;
    double visualOffset = 0.0;
    double rate = 44100.0, ppqPosition = 0.0, ppqStep = 0.0, expectedPpq = 0.0, lastGrid = 0.0;
    float fastEnvelope = 0.0f, slowEnvelope = 0.0f;
    float fastAttack = 0.0f, fastRelease = 0.0f, slowAttack = 0.0f, slowRelease = 0.0f;
    float transientRatio = 3.1f, transientFloor = 0.003f, gainSmoothing = 0.0f;
    float normalisation = 1.0f;
    int capacity = 0, maximumGrain = 0, writePosition = 0, fadeLength = 1, transientHoldoff = 0;
    bool hostSync = false, wasHostSync = false, firstBlock = true, forceCapture = true, transientArmed = true;
    bool suspended = false;
    std::uint64_t clock = 0, captureCount = 0;
};
}
