#include "GrainEngine.h"
#include <algorithm>
#include <cmath>

namespace rice
{
void GrainEngine::prepare (double sampleRate)
{
    rate = sampleRate > 0.0 ? sampleRate : 44100.0;
    // Four dotted whole notes at 20 BPM = 72 seconds. Allocate off the audio thread.
    maximumGrain = static_cast<int> (std::ceil (rate * 72.0));
    capacity = maximumGrain * 2 + 1;
    for (auto& channel : history)
        channel.assign (static_cast<size_t> (capacity), 0.0f);
    for (auto& voice : voices)
        for (auto& channel : voice.cache)
            channel.resize (static_cast<size_t> (maximumGrain));
    pending.reserve (72004);
    fadeLength = std::max (1, static_cast<int> (rate * 0.0005));
    const auto coefficient = [this] (double seconds) { return static_cast<float> (std::exp (-1.0 / (rate * seconds))); };
    fastAttack = coefficient (0.0003);
    fastRelease = coefficient (0.005);
    slowAttack = coefficient (0.020);
    slowRelease = coefficient (0.100);
    reset();
}

void GrainEngine::reset()
{
    for (auto& channel : history)
        std::fill (channel.begin(), channel.end(), 0.0f);
    for (auto& voice : voices)
        voice.length = voice.copied = voice.phase = 0;
    pending.clear();
    clock = captureCount = 0;
    writePosition = currentVoice = fadeRemaining = transientHoldoff = 0;
    countdown = expectedPpq = lastGrid = 0.0;
    fastEnvelope = slowEnvelope = 0.0f;
    firstBlock = forceCapture = transientArmed = true;
    hostSync = wasHostSync = false;
}

void GrainEngine::cancelPendingCaptures()
{
    pending.clear();
    forceCapture = true;
}

void GrainEngine::beginBlock (const Settings& next, double ppq, double beatsPerSample, bool followHost)
{
    if (next.capture != settings.capture)
        pending.clear();
    if (next.capture != settings.capture
        || (next.capture == Capture::milliseconds && next.intervalSamples != settings.intervalSamples)
        || (next.capture == Capture::synced && next.intervalBeats != settings.intervalBeats))
    {
        countdown = 0.0;
        forceCapture = true;
    }
    settings = next;
    settings.grainSamples = std::clamp (settings.grainSamples, 2, maximumGrain);
    settings.intervalSamples = std::max (rate * 0.001, settings.intervalSamples);
    settings.intervalBeats = std::max (0.0001, settings.intervalBeats);
    const auto sensitivity = std::clamp (settings.sensitivity, 0.0f, 1.0f);
    transientRatio = 5.0f - 3.8f * sensitivity;
    transientFloor = 0.030f * std::pow (0.01f, sensitivity);
    hostSync = followHost && settings.capture == Capture::synced;
    ppqPosition = ppq;
    ppqStep = beatsPerSample;
    if (hostSync && (firstBlock || ! wasHostSync || std::abs (ppq - expectedPpq) > beatsPerSample * 2.0))
    {
        pending.clear();
        lastGrid = std::floor (ppq / settings.intervalBeats + 1.0e-9);
        forceCapture = true;
    }
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
    if (transientHoldoff > 0)
        --transientHoldoff;
    if (fastEnvelope < std::max (transientFloor * 0.7f, slowEnvelope * (1.0f + (transientRatio - 1.0f) * 0.4f)))
        transientArmed = true;
    if (transientArmed && transientHoldoff == 0 && fastEnvelope > transientFloor
        && fastEnvelope > slowEnvelope * transientRatio)
    {
        transientArmed = false;
        transientHoldoff = static_cast<int> (rate * 0.015);
        return true;
    }
    return false;
}

void GrainEngine::requestCapture()
{
    if (pending.size() == pending.capacity())
        return;
    pending.push_back ({ clock + static_cast<std::uint64_t> (settings.grainSamples), writePosition, settings.grainSamples });
    std::push_heap (pending.begin(), pending.end());
}

void GrainEngine::activate (const Pending& capture)
{
    currentVoice = 1 - currentVoice;
    auto& voice = voices[currentVoice];
    voice.start = capture.start;
    voice.length = capture.length;
    voice.copied = voice.phase = 0;
    voice.modeFadeRemaining = 0;
    voice.repeat = settings.repeat;
    fadeRemaining = fadeLength;
    ++captureCount;
}

float GrainEngine::read (const Voice& voice, int channel, int index) const
{
    if (index < voice.copied)
        return voice.cache[static_cast<size_t> (channel)][static_cast<size_t> (index)];
    return history[static_cast<size_t> (channel)][static_cast<size_t> ((voice.start + index) % capacity)];
}

std::array<float, 2> GrainEngine::render (Voice& voice)
{
    if (voice.length == 0)
        return {};
    // Spread snapshot copying across frames. The history protects uncopied samples.
    if (voice.copied < voice.length)
    {
        for (int channel = 0; channel < 2; ++channel)
            voice.cache[static_cast<size_t> (channel)][static_cast<size_t> (voice.copied)]
                = history[static_cast<size_t> (channel)][static_cast<size_t> ((voice.start + voice.copied) % capacity)];
        ++voice.copied;
    }
    const auto atPhase = [this, &voice] (int phase, Repeat repeat)
    {
        int index = phase;
        if (repeat == Repeat::reverse)
            index = voice.length - 1 - index;
        else if (repeat == Repeat::pingPong && index >= voice.length)
            index = 2 * (voice.length - 1) - index;
        float gain = 1.0f;
        if (repeat != Repeat::pingPong)
        {
            const auto edge = std::min (fadeLength, (voice.length - 1) / 4);
            if (edge > 0)
                gain = std::min (1.0f, static_cast<float> (std::min (index, voice.length - 1 - index)) / static_cast<float> (edge));
        }
        return std::array<float, 2> { read (voice, 0, index) * gain, read (voice, 1, index) * gain };
    };
    const auto advance = [&voice] (int phase, Repeat repeat)
    {
        return (phase + 1) % (repeat == Repeat::pingPong ? 2 * (voice.length - 1) : voice.length);
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
    return result;
}

std::array<float, 2> GrainEngine::process (std::array<float, 2> input)
{
    for (size_t channel = 0; channel < input.size(); ++channel)
    {
        if (! std::isfinite (input[channel]))
            input[channel] = 0.0f;
        history[channel][static_cast<size_t> (writePosition)] = input[channel];
    }
    const auto transient = detectTransient (std::max (std::abs (input[0]), std::abs (input[1])));
    bool trigger = forceCapture;
    forceCapture = false;
    if (settings.capture == Capture::transient)
        trigger = trigger || transient;
    else if (hostSync)
    {
        const auto grid = std::floor (ppqPosition / settings.intervalBeats + 1.0e-9);
        trigger = trigger || grid != lastGrid;
        lastGrid = grid;
    }
    else
    {
        trigger = trigger || countdown <= 0.0;
        if (trigger)
            countdown = settings.intervalSamples + std::min (0.0, countdown);
        countdown -= 1.0;
    }
    if (trigger)
        requestCapture();
    while (! pending.empty() && pending.front().due <= clock)
    {
        const auto capture = pending.front();
        std::pop_heap (pending.begin(), pending.end());
        pending.pop_back();
        activate (capture);
    }
    auto& voice = voices[currentVoice];
    if (voice.repeat != settings.repeat)
    {
        voice.oldRepeat = voice.repeat;
        voice.oldPhase = voice.phase;
        voice.modeFadeRemaining = fadeLength;
        voice.repeat = settings.repeat;
        voice.phase = 0;
    }
    auto result = voice.length == 0 ? input : render (voice);
    if (fadeRemaining > 0)
    {
        auto& previousVoice = voices[1 - currentVoice];
        const auto previous = previousVoice.length > 0 ? render (previousVoice) : input;
        const auto mix = static_cast<float> (fadeLength - fadeRemaining + 1) / static_cast<float> (fadeLength);
        for (size_t channel = 0; channel < result.size(); ++channel)
            result[channel] = previous[channel] + mix * (result[channel] - previous[channel]);
        --fadeRemaining;
    }
    writePosition = (writePosition + 1) % capacity;
    ++clock;
    ppqPosition += ppqStep;
    expectedPpq = ppqPosition;
    return result;
}
}
