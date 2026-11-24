#include "GrainEngine.h"
#include "CloudEngine.h"
#include <algorithm>
#include <cmath>

namespace rice
{
void GrainEngine::prepare (double sampleRate)
{
    rate = sampleRate > 0.0 ? sampleRate : 44100.0;
    maximumGrain = static_cast<int> (std::ceil (rate * 72.0));
    // Negative position offset + recording + incremental snapshot copy.
    capacity = maximumGrain * 3 + 1;
    for (auto& channel : history)
        channel.assign (static_cast<size_t> (capacity), 0.0f);
    for (auto& lane : lanes)
    {
        for (auto& head : lane.heads)
            for (auto& channel : head.cache)
                channel.reset (new float[static_cast<size_t> (maximumGrain)]);
        // Longest grain + positive position offset, with 1 ms capture starts.
        lane.pending.reserve (144004);
    }
    fadeLength = std::max (1, static_cast<int> (rate * 0.0005));
    const auto coefficient = [this] (double seconds) { return static_cast<float> (std::exp (-1.0 / (rate * seconds))); };
    fastAttack = coefficient (0.0003);
    fastRelease = coefficient (0.005);
    slowAttack = coefficient (0.020);
    slowRelease = coefficient (0.100);
    gainSmoothing = coefficient (0.005);
    reset();
}

void GrainEngine::clearLane (Lane& lane, int index)
{
    for (auto& head : lane.heads)
    {
        head.length = head.copied = head.modeFadeRemaining = 0;
        head.phase = 0.0;
        head.frozenOwner = -1; head.aliasFade = 0;
    }
    lane.pending.clear();
    lane.current = lane.fadeRemaining = 0;
    lane.countdown = lane.nextBeat = 0.0;
    lane.liveBegin = lane.liveUntil = lane.sourceStart = 0;
    lane.displayStart = 0;
    lane.liveMix = 0.0f;
    lane.force = true;
    lane.info = {};
    lane.rng = 0x9e3779b9u ^ (static_cast<std::uint32_t> (index + 1) * 0x85ebca6bu);
}

void GrainEngine::reset()
{
    for (int i = 0; i < maxVoices; ++i)
    {
        auto& lane = lanes[static_cast<size_t> (i)];
        clearLane (lane, i);
        lane.enabled = false;
        lane.gainL = lane.gainR = lane.targetL = lane.targetR = 0.0f;
    }
    clock = captureCount = 0;
    writePosition = transientHoldoff = 0;
    expectedPpq = lastGrid = 0.0;
    fastEnvelope = slowEnvelope = 0.0f;
    normalisation = 1.0f;
    firstBlock = forceCapture = transientArmed = true;
    hostSync = wasHostSync = suspended = false;
}

void GrainEngine::cancelPendingCaptures()
{
    for (auto& lane : lanes)
        lane.pending.clear();
    forceCapture = true;
}

void GrainEngine::beginBlock (const Settings& next, double ppq, double beatsPerSample, bool followHost, bool transportPlaying)
{
    if (! transportPlaying)
    {
        if (! suspended)
        {
            if (settings.freeze)
            { for (auto& lane : lanes) { lane.pending.clear(); lane.liveMix = 0.0f; lane.liveUntil = clock; } }
            else reset();
            suspended = true;
        }
        return;
    }
    if (suspended)
    { if (settings.freeze && next.freeze) suspended = false; else reset(); }
    const auto captureChanged = next.capture != settings.capture;
    const auto pitchChanged = next.pitch != settings.pitch || next.detune != settings.detune || next.randomPitch != settings.randomPitch
        || next.root != settings.root || next.scale != settings.scale || next.voiceCount != settings.voiceCount;
    const auto scheduleChanged = captureChanged
        || (! next.modulating && next.capture == Capture::milliseconds && next.intervalSamples != settings.intervalSamples)
        || (next.capture == Capture::synced && next.intervalBeats != settings.intervalBeats)
        || (next.randomIntervalSamples == 0.0) != (settings.randomIntervalSamples == 0.0);
    if (captureChanged)
        cancelPendingCaptures();
    if (scheduleChanged)
        forceCapture = true;
    if (next.freeze && ! settings.freeze)
    {
        for (auto& lane : lanes)
        {
            if (lane.heads[static_cast<size_t> (lane.current)].length == 0 && clock > lane.sourceStart)
            {
                const auto length = static_cast<int> (std::min (clock - lane.sourceStart, static_cast<std::uint64_t> (std::max (2, lane.info.length))));
                if (length >= 2) activate (lane, { clock, static_cast<int> (lane.sourceStart % static_cast<std::uint64_t> (capacity)), length, lane.displayStart });
            }
            lane.pending.clear(); lane.liveUntil = clock; lane.liveMix = 0.0f;
        }
    }
    if (! next.freeze && settings.freeze)
    {
        forceCapture = true;
        for (auto& lane : lanes) for (auto& head : lane.heads) if (head.frozenOwner >= 0) head.aliasFade = fadeLength;
    }
    settings = next;
    settings.voiceCount = std::clamp (settings.voiceCount, 1, maxVoices);
    settings.spread = std::clamp (settings.spread, 0.0f, 1.0f);
    settings.grainSamples = std::clamp (settings.grainSamples, 2, maximumGrain);
    settings.intervalSamples = std::max (rate * 0.001, settings.intervalSamples);
    settings.intervalBeats = std::max (0.0001, settings.intervalBeats);
    settings.randomSizeSamples = std::clamp (settings.randomSizeSamples, 0.0, static_cast<double> (maximumGrain));
    settings.randomPositionSamples = std::clamp (settings.randomPositionSamples, 0.0, static_cast<double> (maximumGrain));
    settings.randomIntervalSamples = std::clamp (settings.randomIntervalSamples, 0.0, static_cast<double> (maximumGrain));
    const auto sensitivity = std::clamp (settings.sensitivity, 0.0f, 1.0f);
    transientRatio = 5.0f - 3.8f * sensitivity;
    transientFloor = 0.030f * std::pow (0.01f, sensitivity);
    hostSync = followHost && settings.capture == Capture::synced;
    ppqPosition = ppq;
    ppqStep = beatsPerSample;
    if (hostSync && (firstBlock || ! wasHostSync || std::abs (ppq - expectedPpq) > beatsPerSample * 2.0))
    {
        cancelPendingCaptures();
        lastGrid = std::floor (ppq / settings.intervalBeats + 1.0e-9);
    }
    for (int i = 0; i < maxVoices; ++i)
    {
        auto& lane = lanes[static_cast<size_t> (i)];
        const bool enabled = i < settings.voiceCount;
        if (enabled && ! lane.enabled)
        {
            clearLane (lane, i);
            if (i > 0 && settings.freeze)
            {
                const auto owner = lanes[0].current;
                const auto& reference = lanes[0].heads[static_cast<size_t> (owner)];
                if (reference.length > 0)
                {
                    auto& head = lane.heads[0]; head.frozenOwner = owner; head.length = reference.length;
                    head.start = reference.start; head.sourceTime = reference.sourceTime; head.repeat = settings.repeat;
                    head.envelope = settings.envelope; head.envelopeEnabled = settings.envelopeEnabled; head.contour.set (head.envelope, head.length * 1000.0 / rate);
                    const auto detune = settings.voiceCount == 1 ? 0.0f : settings.detune * (2.0f * static_cast<float> (i) / static_cast<float> (settings.voiceCount - 1) - 1.0f);
                    head.randomPitchDraw = static_cast<float> (randomSigned (lane, 1.0));
                    head.speed = pitchRatio (settings.pitch, detune, settings.randomPitch, head.randomPitchDraw, settings.root, settings.scale);
                    lane.info.length = head.length; lane.displayStart = head.sourceTime;
                }
            }
            // No startup gain ramp: preserve the existing live first pass exactly.
            if (firstBlock)
                lane.gainL = lane.gainR = 1.0f;
        }
        if (! enabled && lane.enabled)
            lane.pending.clear();
        lane.enabled = enabled;
        if (enabled && pitchChanged)
        {
            const auto cents = settings.voiceCount == 1 ? 0.0f : settings.detune * (2.0f * static_cast<float> (i) / static_cast<float> (settings.voiceCount - 1) - 1.0f);
            for (auto& head : lane.heads) if (head.length > 0)
                head.speed = pitchRatio (settings.pitch, cents, settings.randomPitch, head.randomPitchDraw, settings.root, settings.scale);
        }
        const auto pan = settings.voiceCount == 1 ? 0.0f
            : settings.spread * (2.0f * static_cast<float> (i) / static_cast<float> (settings.voiceCount - 1) - 1.0f);
        const auto angle = (pan + 1.0f) * 0.78539816339f;
        lane.targetL = enabled ? std::cos (angle) * 1.41421356237f : 0.0f;
        lane.targetR = enabled ? std::sin (angle) * 1.41421356237f : 0.0f;
        if (scheduleChanged)
            lane.countdown = 0.0;
    }
    if (firstBlock)
        normalisation = 1.0f / static_cast<float> (settings.voiceCount);
    wasHostSync = hostSync;
    firstBlock = false;
}

bool GrainEngine::detectTransient (float level)
{
    const auto follow = [level] (float envelope, float attack, float release)
    {
        const auto coefficient = level > envelope ? attack : release;
        return level + coefficient * (envelope - level);
    };
    fastEnvelope = follow (fastEnvelope, fastAttack, fastRelease);
    slowEnvelope = follow (slowEnvelope, slowAttack, slowRelease);
    if (transientHoldoff > 0) --transientHoldoff;
    if (fastEnvelope < std::max (transientFloor * 0.7f, slowEnvelope * (1.0f + (transientRatio - 1.0f) * 0.4f)))
        transientArmed = true;
    if (transientArmed && transientHoldoff == 0 && fastEnvelope > transientFloor && fastEnvelope > slowEnvelope * transientRatio)
    {
        transientArmed = false;
        transientHoldoff = static_cast<int> (rate * 0.015);
        return true;
    }
    return false;
}

double GrainEngine::randomSigned (Lane& lane, double amount)
{
    if (amount == 0.0) return 0.0;
    auto& seed = lane.rng;
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return (static_cast<double> (seed) / 4294967295.0 * 2.0 - 1.0) * amount;
}

double GrainEngine::nextInterval (Lane& lane)
{
    lane.info.interval = std::max (std::max (1.0, rate * 0.001),
                                   settings.intervalSamples + randomSigned (lane, settings.randomIntervalSamples));
    return lane.info.interval;
}

void GrainEngine::requestCapture (Lane& lane, double transientJitter)
{
    if (lane.pending.size() == lane.pending.capacity()) return;
    const auto length = std::clamp (static_cast<int> (std::llround (settings.grainSamples + randomSigned (lane, settings.randomSizeSamples))),
                                   std::max (2, static_cast<int> (std::round (rate * 0.001))), maximumGrain);
    auto offset = static_cast<std::int64_t> (std::llround (randomSigned (lane, settings.randomPositionSamples) + transientJitter - settings.positionSamples));
    offset = std::clamp (offset, -static_cast<std::int64_t> (maximumGrain), static_cast<std::int64_t> (maximumGrain));
    // No reads from before this transport run, or from overwritten history.
    const auto source = std::max (std::int64_t { 0 }, static_cast<std::int64_t> (clock) + offset);
    lane.sourceStart = static_cast<std::uint64_t> (source);
    lane.displayStart = static_cast<std::uint64_t> (std::max (0.0, static_cast<double> (source) + visualOffset));
    lane.liveBegin = std::max (clock, lane.sourceStart);
    lane.liveUntil = lane.liveBegin + static_cast<std::uint64_t> (length);
    lane.pending.push_back ({ lane.liveUntil, static_cast<int> (lane.sourceStart % static_cast<std::uint64_t> (capacity)), length, lane.displayStart });
    std::push_heap (lane.pending.begin(), lane.pending.end());
    lane.info.length = length;
    lane.info.positionOffset = source - static_cast<std::int64_t> (clock);
    ++lane.info.requests;
}

void GrainEngine::activate (Lane& lane, const Pending& capture)
{
    lane.current = 1 - lane.current;
    auto& head = lane.heads[static_cast<size_t> (lane.current)];
    head.start = capture.start;
    head.frozenOwner = -1; head.aliasFade = 0;
    head.sourceTime = capture.sourceTime;
    head.length = capture.length;
    head.copied = head.modeFadeRemaining = head.envelopeAge = 0;
    head.phase = 0.0;
    const auto index = static_cast<int> (&lane - lanes.data());
    const auto detune = settings.voiceCount == 1 ? 0.0f : settings.detune * (2.0f * static_cast<float> (index) / static_cast<float> (settings.voiceCount - 1) - 1.0f);
    head.randomPitchDraw = static_cast<float> (randomSigned (lane, settings.randomPitch == 0.0f ? 0.0 : 1.0));
    head.speed = pitchRatio (settings.pitch, detune, settings.randomPitch, head.randomPitchDraw, settings.root, settings.scale);
    head.envelope = settings.envelope; head.envelopeEnabled = settings.envelopeEnabled;
    head.contour.set (head.envelope, head.length * 1000.0 / rate);
    head.repeat = settings.repeat;
    if (head.repeat == Repeat::pingPong)
        head.phase = head.length % (2 * (head.length - 1));
    lane.fadeRemaining = fadeLength;
    ++captureCount;
}

float GrainEngine::read (const Head& head, int channel, int index) const
{
    if (head.frozenOwner >= 0) return read (lanes[0].heads[static_cast<size_t> (head.frozenOwner)], channel, index);
    if (index < head.copied)
        return head.cache[static_cast<size_t> (channel)][static_cast<size_t> (index)];
    return history[static_cast<size_t> (channel)][static_cast<size_t> ((head.start + index) % capacity)];
}

std::array<float, 2> GrainEngine::render (Head& voice)
{
    if (voice.length == 0) return {};
    if (voice.envelopeAge == 0)
    {
        voice.envelope = settings.envelope; voice.envelopeEnabled = settings.envelopeEnabled;
        voice.contour.set (voice.envelope, voice.length * 1000.0 / rate);
    }
    if (voice.frozenOwner < 0 && voice.copied < voice.length)
    {
        for (int channel = 0; channel < 2; ++channel)
            voice.cache[static_cast<size_t> (channel)][static_cast<size_t> (voice.copied)]
                = history[static_cast<size_t> (channel)][static_cast<size_t> ((voice.start + voice.copied) % capacity)];
        ++voice.copied;
    }
    const auto atPhase = [this, &voice] (double phase, Repeat repeat)
    {
        double index = phase;
        if (repeat == Repeat::reverse) index = voice.length - 1 - index;
        else if (repeat == Repeat::pingPong && index >= voice.length) index = 2 * (voice.length - 1) - index;
        float gain = 1.0f;
        if (repeat != Repeat::pingPong)
        {
            const auto edge = std::min (fadeLength, (voice.length - 1) / 4);
            if (edge > 0)
                gain = std::clamp (static_cast<float> (std::min (index, voice.length - 1 - index)) / static_cast<float> (edge), 0.0f, 1.0f);
        }
        index = std::clamp (index, 0.0, static_cast<double> (voice.length - 1));
        const auto first = static_cast<int> (index);
        const auto second = std::min (first + 1, voice.length - 1);
        const auto mix = static_cast<float> (index - first);
        std::array<float, 2> result;
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto a = read (voice, ch, first);
            result[static_cast<size_t> (ch)] = (a + mix * (read (voice, ch, second) - a)) * gain;
        }
        return result;
    };
    const auto advance = [&voice] (double phase, Repeat repeat)
    {
        return wrap (phase + voice.speed, repeat == Repeat::pingPong ? 2 * (voice.length - 1) : voice.length);
    };
    auto result = atPhase (voice.phase, voice.repeat);
    voice.phase = advance (voice.phase, voice.repeat);
    if (voice.modeFadeRemaining > 0)
    {
        const auto previous = atPhase (voice.oldPhase, voice.oldRepeat);
        const auto mix = static_cast<float> (fadeLength - voice.modeFadeRemaining + 1) / static_cast<float> (fadeLength);
        for (size_t channel = 0; channel < result.size(); ++channel)
            result[channel] = previous[channel] + mix * (result[channel] - previous[channel]);
        voice.oldPhase = advance (voice.oldPhase, voice.oldRepeat);
        --voice.modeFadeRemaining;
    }
    if (voice.envelopeEnabled)
    {
        const auto gain = voice.contour.value (static_cast<double> (voice.envelopeAge) / voice.length);
        for (auto& sample : result) sample *= gain;
    }
    voice.envelopeAge = (voice.envelopeAge + 1) % voice.length;
    if (voice.aliasFade > 0)
    {
        for (auto& sample : result) sample *= static_cast<float> (voice.aliasFade) / static_cast<float> (fadeLength);
        if (--voice.aliasFade == 0) { voice.length = 0; voice.frozenOwner = -1; }
    }
    return result;
}

std::array<float, 2> GrainEngine::renderLane (Lane& lane)
{
    auto& head = lane.heads[static_cast<size_t> (lane.current)];
    if (head.repeat != settings.repeat)
    {
        head.oldRepeat = head.repeat; head.oldPhase = head.phase;
        head.modeFadeRemaining = fadeLength; head.repeat = settings.repeat; head.phase = 0;
    }
    const auto liveIndex = (lane.sourceStart + (clock >= lane.liveBegin ? clock - lane.liveBegin : 0)) % static_cast<std::uint64_t> (capacity);
    const bool live = clock >= lane.liveBegin && clock < lane.liveUntil;
    std::array<float, 2> source {};
    if (live)
    {
        source = { history[0][static_cast<size_t> (liveIndex)], history[1][static_cast<size_t> (liveIndex)] };
        if (settings.envelopeEnabled)
        {
            const auto length = std::max (std::uint64_t { 1 }, lane.liveUntil - lane.liveBegin);
            const auto gain = settings.envelope.value (static_cast<double> (clock - lane.liveBegin) / static_cast<double> (length), static_cast<double> (length) * 1000.0 / rate);
            for (auto& sample : source) sample *= gain;
        }
    }
    auto result = head.length == 0 ? source : render (head);
    if (lane.fadeRemaining > 0)
    {
        auto& previous = lane.heads[static_cast<size_t> (1 - lane.current)];
        const auto old = previous.length > 0 ? render (previous) : source;
        const auto mix = static_cast<float> (fadeLength - lane.fadeRemaining + 1) / static_cast<float> (fadeLength);
        for (size_t channel = 0; channel < result.size(); ++channel)
            result[channel] = old[channel] + mix * (result[channel] - old[channel]);
        --lane.fadeRemaining;
    }
    const auto liveStep = 1.0f / static_cast<float> (fadeLength);
    lane.liveMix = live ? std::min (1.0f, lane.liveMix + liveStep) : std::max (0.0f, lane.liveMix - liveStep);
    // Continue the selected source during the short fade out; it is available now.
    if (! live && lane.liveMix > 0.0f && clock >= lane.liveUntil)
        source = { history[0][static_cast<size_t> (liveIndex)], history[1][static_cast<size_t> (liveIndex)] };
    for (size_t channel = 0; channel < result.size(); ++channel)
        result[channel] += lane.liveMix * (source[channel] - result[channel]);
    return result;
}

std::array<float, 2> GrainEngine::process (std::array<float, 2> input)
{
    if (suspended) return {};
    for (size_t channel = 0; channel < input.size(); ++channel)
    {
        if (! std::isfinite (input[channel])) input[channel] = 0.0f;
        history[channel][static_cast<size_t> (writePosition)] = input[channel];
    }
    const auto transient = detectTransient (std::max (std::abs (input[0]), std::abs (input[1])));
    const auto grid = std::floor (ppqPosition / settings.intervalBeats + 1.0e-9);
    const auto gridTrigger = grid != lastGrid;
    lastGrid = grid;
    std::array<float, 2> result {};
    float totalL = 0.0f, totalR = 0.0f;
    for (auto& lane : lanes)
    {
        lane.gainL = lane.targetL + gainSmoothing * (lane.gainL - lane.targetL);
        lane.gainR = lane.targetR + gainSmoothing * (lane.gainR - lane.targetR);
        totalL += lane.gainL;
        totalR += lane.gainR;
    }
    // Include fading-out lanes in the divisor when automating the voice count.
    normalisation = 1.0f / std::max (1.0f, std::max (totalL, totalR));
    for (auto& lane : lanes)
    {
        if (! lane.enabled && std::abs (lane.gainL) + std::abs (lane.gainR) < 0.00001f) continue;
        if (lane.enabled)
        {
            bool trigger = forceCapture || lane.force;
            lane.force = false;
            if (settings.capture == Capture::transient)
                trigger = trigger || transient;
            else if (hostSync)
                trigger = trigger || (settings.randomIntervalSamples == 0.0 ? gridTrigger : ppqPosition + 1.0e-9 >= lane.nextBeat);
            else
                trigger = trigger || lane.countdown <= 0.0;
            if (trigger && ! settings.freeze)
            {
                const auto jitter = settings.capture == Capture::transient ? randomSigned (lane, settings.randomIntervalSamples) : 0.0;
                requestCapture (lane, jitter);
                const auto interval = nextInterval (lane);
                lane.countdown = interval + std::min (0.0, lane.countdown);
                lane.nextBeat = ppqPosition + interval * ppqStep;
            }
            lane.countdown -= 1.0;
        }
        while (! lane.pending.empty() && lane.pending.front().due <= clock)
        {
            const auto capture = lane.pending.front();
            std::pop_heap (lane.pending.begin(), lane.pending.end()); lane.pending.pop_back();
            activate (lane, capture);
        }
        const auto audio = renderLane (lane);
        result[0] += audio[0] * lane.gainL * normalisation;
        result[1] += audio[1] * lane.gainR * normalisation;
    }
    forceCapture = false;
    writePosition = (writePosition + 1) % capacity;
    ++clock; ppqPosition += ppqStep; expectedPpq = ppqPosition;
    return result;
}
void GrainEngine::publishMarkers (VisualState& visual, std::uint64_t recorded, double available, bool append) const
{
    size_t dot = append ? static_cast<size_t> (std::max (0, visual.active.load())) : 0;
    if (! append) for (auto& gain : visual.gains) gain.store (0.0f, std::memory_order_relaxed);
    if (suspended) { if (! append) visual.active.store (0); return; }
    const auto oldestTime = static_cast<double> (recorded) - available;
    for (size_t i = 0; i < lanes.size() && dot < VisualState::dots; ++i)
    {
        const auto& lane = lanes[i]; if (! lane.enabled) continue;
        const auto& head = lane.heads[static_cast<size_t> (lane.current)];
        const auto live = clock >= lane.liveBegin && clock < lane.liveUntil;
        if (! live && head.length == 0) continue;
        auto phase = head.phase;
        if (head.repeat == Repeat::reverse) phase = head.length - 1 - phase;
        else if (head.repeat == Repeat::pingPong && phase >= head.length) phase = 2.0 * (head.length - 1) - phase;
        const auto t = live ? static_cast<double> (lane.displayStart + clock - lane.liveBegin) : static_cast<double> (head.sourceTime) + phase;
        const auto liveGain = live && settings.envelopeEnabled ? settings.envelope.value (static_cast<double> (clock - lane.liveBegin) / static_cast<double> (std::max (std::uint64_t { 1 }, lane.liveUntil - lane.liveBegin)), static_cast<double> (lane.liveUntil - lane.liveBegin) * 1000.0 / rate) : 1.0f;
        const auto gain = live ? liveGain : ! head.envelopeEnabled ? 1.0f : head.contour.value (static_cast<double> (head.envelopeAge) / head.length);
        visual.positions[dot].store (static_cast<float> (std::clamp ((t - oldestTime) / std::max (1.0, available), 0.0, 1.0)), std::memory_order_relaxed);
        visual.gains[dot].store (gain, std::memory_order_relaxed); visual.layers[dot].store (static_cast<float> (i), std::memory_order_relaxed); ++dot;
        visual.captures[i].store (static_cast<float> (std::clamp ((static_cast<double> (lane.displayStart) - oldestTime) / std::max (1.0, available), 0.0, 1.0)), std::memory_order_relaxed);
    }
    visual.active.store (static_cast<int> (dot), std::memory_order_relaxed);
}
}
