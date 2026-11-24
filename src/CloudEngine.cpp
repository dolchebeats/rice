#include "CloudEngine.h"
namespace rice
{
void CloudEngine::prepare (double sampleRate)
{
    rate = sampleRate; capacity = static_cast<int> (std::ceil (rate * 72.0)) + 8;
    layerLimit = std::clamp (static_cast<int> (grainsPerLayer * 48000.0 / rate), 4, grainsPerLayer);
    for (auto& channel : buffer) channel.assign (static_cast<size_t> (capacity), 0.0f);
    fastA = static_cast<float> (std::exp (-1.0 / (rate * 0.0003))); fastR = static_cast<float> (std::exp (-1.0 / (rate * 0.005)));
    slowA = static_cast<float> (std::exp (-1.0 / (rate * 0.020))); slowR = static_cast<float> (std::exp (-1.0 / (rate * 0.100)));
    reset();
}
void CloudEngine::reset (bool keepBuffer)
{
    for (auto& grain : grains) grain.active = false;
    for (size_t i = 0; i < layers.size(); ++i)
    { layers[i] = {}; layers[i].rng = 0x28aef793u ^ (static_cast<std::uint32_t> (i + 1) * 0x9e3779b9u); }
    if (! keepBuffer) written = oldest = 0;
    launchCount = captureCount = 0; dropped = holdoff = 0;
    fast = slow = 0.0f; armed = first = true; scanPhase = 0.0;
}
void CloudEngine::configure (const Settings& next, double positionPpq, double ppqStep, bool hostSync, bool transportPlaying)
{
    if (! transportPlaying && playing) reset (next.grain.freeze);
    if (transportPlaying && ! playing) first = true;
    if (hostSync && ! first && std::abs (positionPpq - expectedPpq) > ppqStep * 2.0)
    { for (auto& layer : layers) { layer.captureTimer = layer.launchTimer = 0.0; layer.valid = false; } }
    if (next.grain.capture != settings.grain.capture)
        for (auto& layer : layers) layer.captureTimer = 0.0;
    settings = next; settings.grain.voiceCount = std::clamp (settings.grain.voiceCount, 1, 16);
    settings.density = std::clamp (settings.density, 0.25, 200.0);
    transientRatio = 5.0f - 3.8f * settings.grain.sensitivity;
    transientFloor = 0.030f * std::pow (0.01f, settings.grain.sensitivity);
    window = std::clamp (static_cast<int> (settings.bufferSeconds * rate), 2, capacity - 8);
    ppq = positionPpq; step = ppqStep; sync = hostSync; playing = transportPlaying;
    if (first) lastGrid = std::floor (ppq / std::max (0.00001, settings.grain.intervalBeats));
    first = false;
}
std::array<float, 2> CloudEngine::readStereo (double timestamp) const
{
    if (written <= oldest) return {};
    timestamp = std::clamp (timestamp, static_cast<double> (oldest), static_cast<double> (written - 1));
    const auto base = static_cast<std::uint64_t> (timestamp);
    const auto index = static_cast<int> (base % static_cast<std::uint64_t> (capacity));
    const auto previous = base > oldest ? (index == 0 ? capacity - 1 : index - 1) : index;
    const auto next = base + 1 < written ? (index + 1 == capacity ? 0 : index + 1) : index;
    const auto last = base + 2 < written ? (next + 1 == capacity ? 0 : next + 1) : next;
    const auto t = static_cast<float> (timestamp - static_cast<double> (base));
    if (t == 0.0f) return { buffer[0][static_cast<size_t> (index)], buffer[1][static_cast<size_t> (index)] };
    std::array<float, 2> result;
    for (size_t channel = 0; channel < 2; ++channel)
    {
        const auto* data = buffer[channel].data();
        const auto a = data[previous], b = data[index], c = data[next], d = data[last];
        const auto value = b + 0.5f * t * (c - a + t * (2.0f * a - 5.0f * b + 4.0f * c - d + t * (3.0f * (b - c) + d - a)));
        result[channel] = std::clamp (value, std::min (std::min (a, b), std::min (c, d)), std::max (std::max (a, b), std::max (c, d)));
    }
    return result;
}
float CloudEngine::read (int channel, double timestamp) const { return readStereo (timestamp)[static_cast<size_t> (channel)]; }
void CloudEngine::capture (int index, double jitter)
{
    auto& layer = layers[static_cast<size_t> (index)];
    const auto& g = settings.grain;
    layer.duration = std::clamp (static_cast<int> (std::llround (g.grainSamples + g.randomSizeSamples * randomBipolar (layer.rng))), 2, static_cast<int> (rate * 72.0));
    layer.length = std::min (layer.duration, window);
    if (written - oldest < static_cast<std::uint64_t> (layer.length)) return;
    const auto offset = settings.position * static_cast<double> (window - layer.length) + g.randomPositionSamples * randomBipolar (layer.rng) + jitter;
    layer.anchor = std::clamp (static_cast<double> (written - layer.length) - offset, static_cast<double> (oldest), static_cast<double> (written - layer.length));
    layer.valid = true; ++captureCount;
}
void CloudEngine::launch (int index)
{
    auto& layer = layers[static_cast<size_t> (index)];
    if (! layer.valid || written - oldest < 2) return;
    Grain* slot = nullptr;
    const auto firstSlot = static_cast<size_t> (index * grainsPerLayer);
    for (size_t i = firstSlot; i < firstSlot + static_cast<size_t> (layerLimit); ++i)
        if (! grains[i].active) { slot = &grains[i]; break; }
    if (slot == nullptr) { ++dropped; return; }
    auto& grain = *slot;
    grain = {}; grain.active = true; grain.layer = index;
    grain.length = std::min (layer.length, static_cast<int> (written - oldest));
    grain.duration = std::max (2, layer.duration); grain.envelope = settings.grain.envelope;
    grain.contour.set (grain.envelope, grain.duration * 1000.0 / rate);
    const auto low = static_cast<double> (oldest), high = static_cast<double> (written - grain.length);
    const auto selected = std::clamp (layer.anchor, low, high);
    grain.start = scanPhase == 0.0 || high <= low ? selected : low + wrap (selected - low - scanPhase * (high - low), high - low);
    const auto count = settings.grain.voiceCount;
    const auto pan = count == 1 ? 0.0f : settings.grain.spread * (2.0f * static_cast<float> (index) / static_cast<float> (count - 1) - 1.0f);
    grain.panL = std::cos ((pan + 1.0f) * 0.78539816339f) * 1.41421356237f;
    grain.panR = std::sin ((pan + 1.0f) * 0.78539816339f) * 1.41421356237f;
    const auto cents = count == 1 ? 0.0f : settings.grain.detune * (2.0f * static_cast<float> (index) / static_cast<float> (count - 1) - 1.0f);
    grain.speed = pitchRatio (settings.grain.pitch, cents, settings.grain.randomPitch, randomBipolar (layer.rng), settings.grain.root, settings.grain.scale);
    grain.repeat = settings.grain.repeat;
    grain.inverseDuration = 1.0 / grain.duration;
    grain.edgeInverse = 1.0 / std::max (1.0, std::min (rate * 0.0005, grain.duration * 0.1));
    grain.period = grain.repeat == GrainEngine::Repeat::pingPong ? 2.0 * (grain.length - 1) : grain.length;
    ++launchCount;
}
std::array<float, 2> CloudEngine::process (std::array<float, 2> input)
{
    if (! playing) return {};
    if (! settings.grain.freeze)
    {
        for (size_t ch = 0; ch < 2; ++ch)
            buffer[ch][static_cast<size_t> (written % static_cast<std::uint64_t> (capacity))] = std::isfinite (input[ch]) ? input[ch] : 0.0f;
        ++written;
    }
    oldest = std::max (oldest, written > static_cast<std::uint64_t> (window) ? written - static_cast<std::uint64_t> (window) : 0);
    const auto level = std::max (std::abs (input[0]), std::abs (input[1]));
    fast = level + (level > fast ? fastA : fastR) * (fast - level);
    slow = level + (level > slow ? slowA : slowR) * (slow - level);
    if (holdoff > 0) --holdoff;
    if (fast < std::max (transientFloor * 0.7f, slow * (1.0f + (transientRatio - 1.0f) * 0.4f))) armed = true;
    const bool transient = armed && holdoff == 0 && fast > transientFloor && fast > slow * transientRatio;
    if (transient) { armed = false; holdoff = static_cast<int> (rate * 0.015); }
    const auto grid = std::floor (ppq / std::max (0.00001, settings.grain.intervalBeats) + 1.0e-9);
    const bool boundary = grid != lastGrid; lastGrid = grid;
    for (int index = 0; index < settings.grain.voiceCount; ++index)
    {
        auto& layer = layers[static_cast<size_t> (index)];
        const auto& g = settings.grain;
        const bool due = g.capture == GrainEngine::Capture::transient ? transient
            : sync && g.capture == GrainEngine::Capture::synced ? (g.randomIntervalSamples == 0.0 ? boundary : ppq >= layer.nextBeat)
            : layer.captureTimer <= 0.0;
        if (! layer.valid || due)
        {
            capture (index, g.capture == GrainEngine::Capture::transient ? randomBipolar (layer.rng) * g.randomIntervalSamples : 0.0);
            const auto interval = std::max (rate * 0.001, g.intervalSamples + g.randomIntervalSamples * randomBipolar (layer.rng));
            layer.captureTimer = interval;
            layer.nextBeat = ppq + interval * step;
        }
        layer.captureTimer -= 1.0;
        if (layer.launchTimer <= 0.0 && layer.valid)
        { launch (index); layer.launchTimer += rate / settings.density; }
        if (layer.valid) layer.launchTimer -= 1.0;
    }
    std::array<float, 2> result {};
    float weightL = 0.0f, weightR = 0.0f;
    const auto retiringLength = std::max (1, static_cast<int> (rate * 0.005));
    const auto retireInverse = 1.0f / static_cast<float> (retiringLength);
    for (auto& grain : grains)
    {
        if (! grain.active) continue;
        const auto sourceExpired = grain.start < static_cast<double> (oldest);
        if ((grain.layer >= settings.grain.voiceCount || sourceExpired) && ! grain.retiring)
        { grain.retiring = true; grain.retireAge = retiringLength; }
        const auto normalized = grain.age * grain.inverseDuration;
        auto gain = grain.contour.value (normalized);
        gain *= static_cast<float> (std::min (1.0, std::min (grain.age, grain.duration - 1 - grain.age) * grain.edgeInverse));
        if (grain.retiring) gain *= static_cast<float> (grain.retireAge) * retireInverse;
        auto local = grain.phase;
        if (grain.repeat == GrainEngine::Repeat::reverse) local = grain.length - 1 - local;
        else if (grain.repeat == GrainEngine::Repeat::pingPong && local >= grain.length) local = 2.0 * (grain.length - 1) - local;
        local = std::clamp (local, 0.0, static_cast<double> (grain.length - 1));
        if (! sourceExpired) grain.lastSample = readStereo (grain.start + local);
        result[0] += grain.lastSample[0] * gain * grain.panL;
        result[1] += grain.lastSample[1] * gain * grain.panR;
        weightL += gain * grain.panL; weightR += gain * grain.panR;
        grain.phase += grain.speed;
        if (grain.phase >= grain.period) grain.phase = wrap (grain.phase, grain.period);
        if (++grain.age >= grain.duration) grain.active = false;
        if (grain.retiring && --grain.retireAge <= 0) grain.active = false;
    }
    const auto normal = std::max (1.0f, std::max (weightL, weightR));
    for (auto& sample : result) sample /= normal;
    scanPhase = wrap (scanPhase + settings.scanHz / rate, 1.0);
    ppq += step; expectedPpq = ppq;
    return result;
}
int CloudEngine::activeGrains() const
{
    return static_cast<int> (std::count_if (grains.begin(), grains.end(), [] (const auto& g) { return g.active; }));
}
void CloudEngine::publish (VisualState& visual)
{
    const auto count = written - oldest;
    for (size_t i = 0; i < VisualState::bins; ++i)
    {
        float peak = 0.0f;
        for (int j = 0; j < 8 && count > 0; ++j)
        {
            const auto t = static_cast<double> (oldest) + (static_cast<double> (i) + j / 8.0) / VisualState::bins * static_cast<double> (count - 1);
            const auto sample = readStereo (t);
            peak = std::max (peak, std::max (std::abs (sample[0]), std::abs (sample[1])));
        }
        visual.waveform[i].store (peak, std::memory_order_relaxed);
    }
    size_t dot = 0;
    for (const auto& g : grains)
    {
        if (! g.active || dot == VisualState::dots) continue;
        auto phase = g.phase;
        if (g.repeat == GrainEngine::Repeat::reverse) phase = g.length - 1 - phase;
        else if (g.repeat == GrainEngine::Repeat::pingPong && phase >= g.length) phase = 2.0 * (g.length - 1) - phase;
        visual.positions[dot].store (static_cast<float> ((g.start + phase - static_cast<double> (oldest)) / std::max (1.0, static_cast<double> (count))), std::memory_order_relaxed);
        visual.gains[dot].store (g.contour.value (static_cast<double> (g.age) / g.duration), std::memory_order_relaxed);
        visual.layers[dot].store (static_cast<float> (g.layer), std::memory_order_relaxed); ++dot;
    }
    for (; dot < VisualState::dots; ++dot) visual.gains[dot].store (0.0f, std::memory_order_relaxed);
    for (size_t i = 0; i < layers.size(); ++i)
        visual.captures[i].store (i < static_cast<size_t> (settings.grain.voiceCount) && layers[i].valid ? static_cast<float> ((layers[i].anchor - static_cast<double> (oldest)) / std::max (1.0, static_cast<double> (count))) : -1.0f, std::memory_order_relaxed);
    visual.active.store (activeGrains(), std::memory_order_relaxed); visual.dropped.store (dropped, std::memory_order_relaxed);
    visual.available.store (static_cast<float> (static_cast<double> (count) / rate), std::memory_order_relaxed); visual.scan.store (static_cast<float> (scanPhase), std::memory_order_relaxed);
}
}

