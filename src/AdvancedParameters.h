#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "GrainTiming.h"
namespace rice
{
enum Extra : size_t
{
    Texture, Density, BufferLength, Position, Scan, Freeze, Pitch, Detune, RandomPitch, Root, Scale, BeatEnvelope,
    Attack, Hold, Decay, Sustain, Release, AttackCurve, DecayCurve, ReleaseCurve, Feedback, HighPass, LowPass, Drive,
    Lfo1Mode, Lfo1Rate, Lfo1Sync, Lfo1Shape, Lfo2Mode, Lfo2Rate, Lfo2Sync, Lfo2Shape,
    FollowerAttack, FollowerRelease, FollowerGain, SequenceLength, SequenceSync, RandomRate,
    Step0, ExtraCount = Step0 + 16
};
struct Spec
{
    juce::String id, name, unit;
    float min = 0.0f, max = 1.0f, initial = 0.0f, interval = 0.01f, center = 0.0f;
    juce::StringArray choices;
    bool boolean = false, knob = true;
};
inline const std::vector<Spec>& extraSpecs()
{
    static const auto specs = []
    {
        std::vector<Spec> result;
        const auto f = [&] (const char* id, const char* name, float min, float max, float initial, const char* unit = "", float step = 0.01f, float center = 0.0f)
        { result.push_back ({ id, name, unit, min, max, initial, step, center, {}, false, true }); };
        const auto c = [&] (const char* id, const char* name, juce::StringArray choices, int initial = 0, bool knob = false)
        { result.push_back ({ id, name, {}, 0, static_cast<float> (choices.size() - 1), static_cast<float> (initial), 1, 0, choices, false, knob }); };
        const auto b = [&] (const char* id, const char* name) { result.push_back ({ id, name, {}, 0, 1, 0, 1, 0, {}, true, false }); };
        juce::StringArray notes; for (const auto& d : divisions()) notes.add (d.name);
        f ("texture", "Beat / Texture", 0, 100, 0, "%");
        f ("density", "Density", 0.25f, 200, 10, "gr/s", 0.01f, 20);
        f ("bufferLength", "Buffer length", 1, 72, 8, "s", 0.1f, 8);
        f ("position", "Buffer position", 0, 100, 0, "%");
        f ("scan", "Scan speed", -2, 2, 0, "cycles/s", 0.001f);
        b ("freeze", "Freeze buffer");
        f ("pitch", "Pitch", -36, 36, 0, "st", 0.01f);
        f ("detune", "Voice detune", 0, 100, 0, "cents");
        f ("randomPitch", "Random pitch", 0, 24, 0, "st");
        c ("root", "Scale root", { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" });
        c ("scale", "Pitch scale", { "Off", "Chromatic", "Major", "Minor", "Fifths", "Octaves" });
        b ("beatEnvelope", "Shape Beat grains");
        f ("envAttack", "Attack", 0, 2000, 5, "ms", 0.1f, 100);
        f ("envHold", "Hold", 0, 2000, 0, "ms", 0.1f, 100);
        f ("envDecay", "Decay", 0, 2000, 30, "ms", 0.1f, 100);
        f ("envSustain", "Sustain", 0, 100, 70, "%");
        f ("envRelease", "Release", 0, 2000, 30, "ms", 0.1f, 100);
        f ("envAttackCurve", "Attack curve", -1, 1, 0);
        f ("envDecayCurve", "Decay curve", -1, 1, 0);
        f ("envReleaseCurve", "Release curve", -1, 1, 0);
        f ("feedback", "Feedback", 0, 95, 0, "%");
        f ("feedbackHP", "Feedback high-pass", 20, 20000, 40, "Hz", 1, 1000);
        f ("feedbackLP", "Feedback low-pass", 20, 20000, 18000, "Hz", 1, 3000);
        f ("feedbackDrive", "Feedback saturation", 0, 24, 0, "dB");
        for (int i = 1; i <= 2; ++i)
        {
            const auto prefix = "lfo" + juce::String (i);
            c ((prefix + "Mode").toRawUTF8(), ("LFO " + juce::String (i) + " clock").toRawUTF8(), { "Free Hz", "Tempo sync" });
            f ((prefix + "Rate").toRawUTF8(), ("LFO " + juce::String (i) + " rate").toRawUTF8(), 0.01f, 40, 1, "Hz", 0.01f, 2);
            c ((prefix + "Sync").toRawUTF8(), ("LFO " + juce::String (i) + " division").toRawUTF8(), notes, divisionIndex ("1/4"), true);
            c ((prefix + "Shape").toRawUTF8(), ("LFO " + juce::String (i) + " shape").toRawUTF8(), { "Sine", "Triangle", "Saw", "Square" });
        }
        f ("followerAttack", "Follower attack", 0.1f, 500, 10, "ms", 0.1f, 30);
        f ("followerRelease", "Follower release", 1, 2000, 100, "ms", 0.1f, 150);
        f ("followerGain", "Follower gain", 0, 4, 1, "x");
        f ("sequenceLength", "Sequence length", 1, 16, 16, "steps", 1);
        c ("sequenceSync", "Sequence step", notes, divisionIndex ("1/16"), true);
        f ("randomRate", "Smooth random rate", 0.01f, 40, 1, "Hz", 0.01f, 2);
        for (int i = 0; i < 16; ++i)
            f (("step" + juce::String (i)).toRawUTF8(), ("Step " + juce::String (i + 1)).toRawUTF8(), -1, 1, 0);
        jassert (result.size() == ExtraCount);
        return result;
    }();
    return specs;
}
inline juce::StringArray modulationTargets()
{
    juce::StringArray result { "sizeMs", "sizeSync", "captureMs", "captureSync", "sensitivity", "voices", "spread", "randomSizeMs", "randomSizeSync", "randomIntervalMs", "randomIntervalSync", "randomPositionMs", "randomPositionSync", "dryWet" };
    for (const auto& s : extraSpecs()) if (s.knob) result.add (s.id);
    return result;
}
inline void addExtraParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    for (const auto& spec : extraSpecs())
    {
        if (spec.boolean)
            layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { spec.id, 1 }, spec.name, spec.initial > 0.5f));
        else if (! spec.choices.isEmpty())
            layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { spec.id, 1 }, spec.name, spec.choices, static_cast<int> (spec.initial)));
        else
        {
            juce::NormalisableRange<float> range (spec.min, spec.max, spec.interval);
            if (spec.center > spec.min && spec.center < spec.max) range.setSkewForCentre (spec.center);
            layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { spec.id, 1 }, spec.name, range, spec.initial,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction ([unit = spec.unit] (float v, int) { return juce::String (v, 2) + (unit.isEmpty() ? "" : " " + unit); })
                    .withValueFromStringFunction ([] (const juce::String& text) { return text.getFloatValue(); })));
        }
    }
    juce::StringArray targets { "None", "Grain size (ms)", "Grain size (sync)", "Capture interval (ms)", "Capture interval (sync)", "Transient sensitivity", "Voices", "Spread", "Random Size (ms)", "Random Size (sync)", "Random Interval (ms)", "Random Interval (sync)", "Random Position (ms)", "Random Position (sync)", "Dry/Wet" };
    for (const auto& spec : extraSpecs()) if (spec.knob) targets.add (spec.name);
    for (int i = 0; i < 8; ++i)
    {
        const auto prefix = "mod" + juce::String (i);
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { prefix + "Source", 1 }, "Mod " + juce::String (i + 1) + " source", juce::StringArray { "Off", "LFO 1", "LFO 2", "Follower", "Sequencer", "Smooth random" }, 0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { prefix + "Target", 1 }, "Mod " + juce::String (i + 1) + " target", targets, 0));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { prefix + "Amount", 1 }, "Mod " + juce::String (i + 1) + " depth", juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " %"; })
                .withValueFromStringFunction ([] (const juce::String& text) { return text.getFloatValue(); })));
    }
}
}
