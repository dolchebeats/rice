#pragma once
#include "GrainEngine.h"
#include <atomic>
namespace rice
{
struct VisualState
{
    static constexpr size_t bins = 256, dots = 528;
    std::array<std::atomic<float>, bins> waveform {};
    std::array<std::atomic<float>, dots> positions {}, gains {}, layers {};
    std::array<std::atomic<float>, 16> captures {};
    std::atomic<int> active { 0 }, dropped { 0 };
    std::atomic<float> available { 0.0f }, scan { 0.0f };
};
class CloudEngine
{
public:
    static constexpr int grainsPerLayer = 16, maxGrains = 16 * grainsPerLayer;
    struct Settings
    {
        GrainEngine::Settings grain;
        double bufferSeconds = 8.0, position = 0.0, scanHz = 0.0, density = 10.0;
    };
    void prepare (double sampleRate);
    void reset (bool keepBuffer = false);
    void configure (const Settings&, double ppq, double step, bool sync, bool playing);
    std::array<float, 2> process (std::array<float, 2>);
    void publish (VisualState&);
    int activeGrains() const;
    std::uint64_t launches() const { return launchCount; }
    std::uint64_t captures() const { return captureCount; }
    std::uint64_t recordedFrames() const { return written; }
    double scanPosition() const { return scanPhase; }
private:
    struct Layer
    {
        double captureTimer = 0.0, launchTimer = 0.0, nextBeat = 0.0, anchor = 0.0;
        int length = 1, duration = 1;
        bool valid = false;
        std::uint32_t rng = 1;
    };
    struct Grain
    {
        double start = 0.0, phase = 0.0, speed = 1.0;
        double inverseDuration = 1.0, edgeInverse = 1.0, period = 1.0;
        int length = 0, age = 0, duration = 1, layer = 0;
        GrainEngine::Repeat repeat = GrainEngine::Repeat::straight;
        Envelope envelope;
        CompiledEnvelope contour;
        float panL = 1.0f, panR = 1.0f;
        bool active = false;
        bool retiring = false;
        int retireAge = 0;
        std::array<float, 2> lastSample {};
    };
    float read (int channel, double timestamp) const;
    std::array<float, 2> readStereo (double timestamp) const;
    void capture (int layer, double jitter = 0.0);
    void launch (int layer);
    Settings settings;
    std::array<std::vector<float>, 2> buffer;
    std::array<Layer, 16> layers;
    std::array<Grain, maxGrains> grains;
    double rate = 44100.0, ppq = 0.0, step = 0.0, expectedPpq = 0.0, lastGrid = 0.0, scanPhase = 0.0;
    float fast = 0.0f, slow = 0.0f, fastA = 0.0f, fastR = 0.0f, slowA = 0.0f, slowR = 0.0f;
    float transientRatio = 3.1f, transientFloor = 0.003f;
    int capacity = 0, window = 0, holdoff = 0, dropped = 0, layerLimit = 16;
    bool sync = false, playing = true, armed = true, first = true;
    std::uint64_t written = 0, oldest = 0, launchCount = 0, captureCount = 0;
};
}
