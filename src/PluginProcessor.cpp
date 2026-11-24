#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "GrainTiming.h"
#include <algorithm>
#include <cmath>

juce::AudioProcessorValueTreeState::ParameterLayout AudioPluginAudioProcessor::createParameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    juce::StringArray divisions;
    for (const auto& division : rice::divisions())
        divisions.add (division.name);
    const auto milliseconds = []
    {
        juce::NormalisableRange<float> range (1.0f, 1000.0f, 0.1f);
        range.setSkewForCentre (100.0f);
        return range;
    };
    const auto msAttributes = juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float value, int) { return juce::String (value, 1) + " ms"; })
        .withValueFromStringFunction ([] (const juce::String& text) { return text.getFloatValue(); });
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "sizeMode", 1 }, "Grain size mode", juce::StringArray { "Milliseconds", "Tempo sync" }, 0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "sizeMs", 1 }, "Grain size (ms)", milliseconds(), 100.0f, msAttributes));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "sizeSync", 1 }, "Grain size (sync)", divisions, rice::divisionIndex ("1/16")));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "captureMode", 1 }, "Capture mode", juce::StringArray { "Milliseconds", "Tempo sync", "Transient" }, 0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "captureMs", 1 }, "Capture interval (ms)", milliseconds(), 250.0f, msAttributes));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "captureSync", 1 }, "Capture interval (sync)", divisions, rice::divisionIndex ("1/4")));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "sensitivity", 1 }, "Transient sensitivity", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float value, int) { return juce::String (value, 1) + " %"; })
            .withValueFromStringFunction ([] (const juce::String& text) { return text.getFloatValue(); })));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "repeat", 1 }, "Repeat mode", juce::StringArray { "Straight", "Reverse", "Back-and-forth" }, 0));
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "voices", 1 }, "Voices", 1, 16, 1));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "spread", 1 }, "Spread", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float value, int) { return juce::String (value, 1) + " %"; })
            .withValueFromStringFunction ([] (const juce::String& text) { return text.getFloatValue(); })));
    juce::StringArray randomDivisions { "Off" };
    randomDivisions.addArray (divisions);
    for (const auto* prefix : { "randomSize", "randomInterval", "randomPosition" })
    {
        const juce::String id (prefix);
        const auto name = id == "randomSize" ? "Random Size" : id == "randomInterval" ? "Random Interval" : "Random Position";
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id + "Mode", 1 }, juce::String (name) + " mode", juce::StringArray { "Milliseconds", "Tempo sync" }, 0));
        juce::NormalisableRange<float> amountRange (0.0f, 1000.0f, 0.1f);
        amountRange.setSkewForCentre (100.0f);
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id + "Ms", 1 }, juce::String (name) + " (ms)", amountRange, 0.0f, msAttributes));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id + "Sync", 1 }, juce::String (name) + " (sync)", randomDivisions, 0));
    }
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "dryWet", 1 }, "Dry/Wet", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float value, int) { return juce::String (value, 1) + " %"; })
            .withValueFromStringFunction ([] (const juce::String& text) { return text.getFloatValue(); })));
    rice::addExtraParameters (layout);
    return layout;
}

AudioPluginAudioProcessor::AudioPluginAudioProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "RiceParameters", createParameters())
{
    const std::array<const char*, 20> ids { "sizeMode", "sizeMs", "sizeSync", "captureMode", "captureMs", "captureSync", "sensitivity", "repeat",
        "voices", "spread", "randomSizeMode", "randomSizeMs", "randomSizeSync", "randomIntervalMode", "randomIntervalMs", "randomIntervalSync",
        "randomPositionMode", "randomPositionMs", "randomPositionSync", "dryWet" };
    const auto add = [this] (const juce::String& id) { refs.push_back ({ parameters.getParameter (id), parameters.getRawParameterValue (id) }); };
    for (size_t i = 0; i < ids.size(); ++i) { values[i] = parameters.getRawParameterValue (ids[i]); add (ids[i]); }
    for (const auto& spec : rice::extraSpecs()) add (spec.id);
    for (int i = 0; i < 8; ++i)
        for (const auto* suffix : { "Source", "Target", "Amount" }) add ("mod" + juce::String (i) + suffix);
    base.resize (refs.size()); effective.resize (refs.size());
    for (size_t i = 0; i < refs.size(); ++i) displayedEffective[i].store (refs[i].value->load());
    for (const auto& id : rice::modulationTargets())
    {
        const auto* parameter = parameters.getParameter (id);
        const auto found = std::find_if (refs.begin(), refs.end(), [parameter] (const Ref& r) { return r.parameter == parameter; });
        targetIndices.push_back (static_cast<size_t> (found - refs.begin()));
    }
}
void AudioPluginAudioProcessor::prepareToPlay (double sampleRate, int)
{
    rate = sampleRate; engine.prepare (rate); cloud.prepare (rate); modulation.prepare (rate);
    wetMix.reset (rate, 0.02); textureMix.reset (rate, 0.02);
    wetMix.setCurrentAndTargetValue (values[19]->load() * 0.01f);
    textureMix.setCurrentAndTargetValue (parameters.getRawParameterValue ("texture")->load() * 0.01f);
    previousSizeMode = previousSizeValue = -1.0f;
    previousWet = highState = lowState = {}; lastSources = {}; wasPlaying = true; visualCountdown = controlCountdown = 0; freePpq = 0.0;
    cloud.publish (visual);
}
void AudioPluginAudioProcessor::reset()
{
    engine.reset(); cloud.reset(); modulation.reset(); previousWet = highState = lowState = {}; lastSources = {}; freePpq = 0.0;
    wetMix.setCurrentAndTargetValue (values[19]->load() * 0.01f);
    textureMix.setCurrentAndTargetValue (parameters.getRawParameterValue ("texture")->load() * 0.01f);
    displayCaptureCount.store (0); visualCountdown = controlCountdown = 0;
    cloud.publish (visual);
}
bool AudioPluginAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return (layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono() || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo())
        && layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet();
}
rice::Envelope AudioPluginAudioProcessor::envelopeSettings() const
{
    const auto get = [this] (const char* id) { return parameters.getRawParameterValue (id)->load(); };
    return { get ("envAttack"), get ("envHold"), get ("envDecay"), get ("envRelease"), get ("envSustain") * 0.01f,
             get ("envAttackCurve"), get ("envDecayCurve"), get ("envReleaseCurve") };
}
double AudioPluginAudioProcessor::grainDurationMs() const
{
    return parameters.getRawParameterValue ("sizeMode")->load() < 0.5f ? static_cast<double> (parameters.getRawParameterValue ("sizeMs")->load())
        : rice::millisecondsForDivision (juce::roundToInt (parameters.getRawParameterValue ("sizeSync")->load()), displayBpm.load());
}
float AudioPluginAudioProcessor::effectiveValue (const juce::String& id) const
{
    const auto* parameter = parameters.getParameter (id);
    for (size_t i = 0; i < refs.size(); ++i) if (refs[i].parameter == parameter) return displayedEffective[i].load();
    return 0.0f;
}
void AudioPluginAudioProcessor::updateDsp (const std::array<float, 5>& sources, double ppq, double step, bool sync, bool playing)
{
    std::copy (base.begin(), base.end(), effective.begin());
    bool routed = false;
    for (size_t i = 0; i < 8; ++i)
    {
        const auto offset = 20 + rice::ExtraCount + i * 3;
        const auto source = juce::roundToInt (base[offset]), target = juce::roundToInt (base[offset + 1]);
        if (source < 1 || source > 5 || target < 1 || target > static_cast<int> (targetIndices.size())) continue;
        const auto index = targetIndices[static_cast<size_t> (target - 1)];
        auto& parameter = *refs[index].parameter;
        const auto normal = juce::jlimit (0.0f, 1.0f, parameter.convertTo0to1 (effective[index]) + sources[static_cast<size_t> (source - 1)] * base[offset + 2] * 0.01f);
        effective[index] = parameter.getNormalisableRange().snapToLegalValue (parameter.convertFrom0to1 (normal));
        routed = routed || base[offset + 2] != 0.0f;
    }
    for (size_t i = 0; i < refs.size(); ++i) displayedEffective[i].store (effective[i], std::memory_order_relaxed);
    const auto q = [this] (rice::Extra id) { return effective[20 + static_cast<size_t> (id)]; };
    const auto division = [] (float value) { return rice::divisions()[static_cast<size_t> (juce::jlimit (0, 29, juce::roundToInt (value)))].beats; };
    rice::Modulation::Settings motion;
    for (size_t i = 0; i < 2; ++i)
    {
        const auto start = 20 + rice::Lfo1Mode + i * 4;
        motion.lfos[i] = { effective[start + 1], division (effective[start + 2]), juce::roundToInt (effective[start + 3]), effective[start] > 0.5f };
    }
    motion.followerAttack = q (rice::FollowerAttack); motion.followerRelease = q (rice::FollowerRelease); motion.followerGain = q (rice::FollowerGain);
    motion.steps = juce::roundToInt (q (rice::SequenceLength)); motion.sequenceBeats = division (q (rice::SequenceSync)); motion.randomHz = q (rice::RandomRate);
    for (size_t i = 0; i < 16; ++i) motion.sequence[i] = effective[20 + rice::Step0 + i];
    modulation.configure (motion);
    const auto bpm = displayBpm.load();
    const auto grainMs = effective[0] < 0.5f ? static_cast<double> (effective[1]) : rice::millisecondsForDivision (juce::roundToInt (effective[2]), bpm);
    rice::GrainEngine::Settings grain;
    grain.grainSamples = juce::roundToInt (grainMs * rate * 0.001);
    grain.capture = static_cast<rice::GrainEngine::Capture> (juce::jlimit (0, 2, juce::roundToInt (effective[3])));
    grain.intervalBeats = division (effective[5]);
    grain.intervalSamples = (grain.capture == rice::GrainEngine::Capture::synced ? rice::millisecondsForDivision (juce::roundToInt (effective[5]), bpm) : effective[4]) * rate * 0.001;
    grain.sensitivity = effective[6] * 0.01f; grain.repeat = static_cast<rice::GrainEngine::Repeat> (juce::jlimit (0, 2, juce::roundToInt (effective[7])));
    grain.voiceCount = juce::jlimit (1, 16, juce::roundToInt (effective[8])); grain.spread = effective[9] * 0.01f;
    const auto randomSamples = [this, bpm] (size_t offset)
    {
        const auto index = juce::roundToInt (effective[offset + 2]);
        return (effective[offset] < 0.5f ? static_cast<double> (effective[offset + 1]) : index == 0 ? 0.0 : rice::millisecondsForDivision (index - 1, bpm)) * rate * 0.001;
    };
    grain.randomSizeSamples = randomSamples (10); grain.randomIntervalSamples = randomSamples (13); grain.randomPositionSamples = randomSamples (16);
    grain.envelope = { q (rice::Attack), q (rice::Hold), q (rice::Decay), q (rice::Release), q (rice::Sustain) * 0.01f,
                       q (rice::AttackCurve), q (rice::DecayCurve), q (rice::ReleaseCurve) };
    grain.envelopeEnabled = q (rice::BeatEnvelope) > 0.5f; grain.freeze = q (rice::Freeze) > 0.5f; grain.modulating = routed;
    grain.pitch = q (rice::Pitch); grain.detune = q (rice::Detune); grain.randomPitch = q (rice::RandomPitch); grain.root = juce::roundToInt (q (rice::Root)); grain.scale = juce::roundToInt (q (rice::Scale));
    const auto position = q (rice::Position) * 0.01;
    const auto scanPosition = cloud.scanPosition() != 0.0 ? rice::wrap (position + cloud.scanPosition(), 1.0) : position;
    grain.positionSamples = scanPosition * q (rice::BufferLength) * rate;
    engine.beginBlock (grain, ppq, step, sync, playing);
    cloud.configure ({ grain, q (rice::BufferLength), position, q (rice::Scan), q (rice::Density) }, ppq, step, sync, playing);
    wetMix.setTargetValue (effective[19] * 0.01f); textureMix.setTargetValue (q (rice::Texture) * 0.01f);
    feedbackAmount = q (rice::Feedback) * 0.01f; drive = std::pow (10.0f, q (rice::Drive) * 0.05f);
    hpPole = static_cast<float> (std::exp (-6.28318530718 * std::clamp (static_cast<double> (q (rice::HighPass)), 5.0, rate * 0.45) / rate));
    lpPole = static_cast<float> (std::exp (-6.28318530718 * std::clamp (static_cast<double> (q (rice::LowPass)), 5.0, rate * 0.45) / rate));
}
void AudioPluginAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi); juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels() == 0) return;
    double bpm = 120.0, ppq = 0.0; bool hostTempo = false, sync = false, playing = true;
    if (auto* playhead = getPlayHead()) if (const auto position = playhead->getPosition())
    {
        playing = position->getIsPlaying() || position->getIsRecording();
        if (const auto tempo = position->getBpm(); tempo && std::isfinite (*tempo) && *tempo > 0.0) { bpm = std::clamp (*tempo, 20.0, 999.0); hostTempo = true; }
        if (const auto beat = position->getPpqPosition(); beat && std::isfinite (*beat)) { ppq = *beat; sync = playing; }
    }
    if (! sync) ppq = freePpq;
    const auto step = bpm / (60.0 * rate);
    displayBpm.store (bpm); usingHostTempo.store (hostTempo);
    displayPlaying.store (playing);
    bool changed = false;
    for (size_t i = 0; i < refs.size(); ++i)
    {
        const auto value = refs[i].value->load (std::memory_order_relaxed);
        changed = changed || value != base[i]; base[i] = value;
    }
    if (changed || (sync && std::abs (ppq - freePpq) > step * 2.0)) controlCountdown = 0;
    const auto sizeMode = base[0], sizeValue = base[sizeMode < 0.5f ? 1 : 2];
    if (sizeMode != previousSizeMode || sizeValue != previousSizeValue)
    { engine.cancelPendingCaptures(); previousSizeMode = sizeMode; previousSizeValue = sizeValue; }
    if (playing != wasPlaying)
    { modulation.reset(); previousWet = highState = lowState = {}; lastSources = {}; wasPlaying = playing; controlCountdown = 0; }
    auto sources = lastSources;
    auto* left = buffer.getWritePointer (0); auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto l = std::isfinite (left[sample]) ? left[sample] : 0.0f;
        const auto r = right != nullptr && std::isfinite (right[sample]) ? right[sample] : right == nullptr ? l : 0.0f;
        if (controlCountdown-- <= 0) { updateDsp (sources, ppq, step, sync, playing); controlCountdown = 31; }
        if (playing) sources = modulation.process (std::max (std::abs (l), std::abs (r)), ppq);
        std::array<float, 2> input { l, r };
        for (size_t ch = 0; ch < 2; ++ch)
        {
            highState[ch] = previousWet[ch] + hpPole * (highState[ch] - previousWet[ch]);
            const auto hp = previousWet[ch] - highState[ch];
            lowState[ch] = hp + lpPole * (lowState[ch] - hp);
            input[ch] += feedbackAmount * std::tanh (lowState[ch] * drive);
        }
        engine.setVisualClock (cloud.recordedFrames());
        const auto beat = engine.process (input), texture = cloud.process (input);
        const auto blend = textureMix.getNextValue(), mix = wetMix.getNextValue();
        for (size_t ch = 0; ch < 2; ++ch) previousWet[ch] = beat[ch] * (1.0f - blend) + texture[ch] * blend;
        const auto blendDry = [mix] (float dry, float wet) { return mix == 0.0f ? dry : mix == 1.0f ? wet : dry * (1.0f - mix) + wet * mix; };
        left[sample] = blendDry (l, right != nullptr ? previousWet[0] : 0.5f * (previousWet[0] + previousWet[1]));
        if (right != nullptr) right[sample] = blendDry (r, previousWet[1]);
        ppq += playing ? step : 0.0;
        if (--visualCountdown <= 0)
        {
            cloud.publish (visual);
            if (blend < 0.999f) engine.publishMarkers (visual, cloud.recordedFrames(), visual.available.load() * rate, blend > 0.001f);
            visualCountdown = std::max (1, static_cast<int> (rate / 30.0));
        }
    }
    freePpq = ppq;
    lastSources = sources;
    for (size_t i = 0; i < sources.size(); ++i) displayMods[i].store (sources[i], std::memory_order_relaxed);
    displayCaptureCount.store (engine.getCaptureCount() + cloud.captures(), std::memory_order_relaxed);
}
juce::AudioProcessorEditor* AudioPluginAudioProcessor::createEditor() { return new AudioPluginAudioProcessorEditor (*this); }
void AudioPluginAudioProcessor::getStateInformation (juce::MemoryBlock& data)
{ if (auto xml = parameters.copyState().createXml()) copyXmlToBinary (*xml, data); }
void AudioPluginAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size)) if (xml->hasTagName (parameters.state.getType()))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        // Old sessions must restore the old sound even when loaded into an
        // instance whose newly-added controls have already been edited.
        for (const auto& ref : refs)
            if (! state.getChildWithProperty ("id", ref.parameter->paramID).isValid())
            {
                juce::ValueTree child ("PARAM"); child.setProperty ("id", ref.parameter->paramID, nullptr);
                child.setProperty ("value", ref.parameter->convertFrom0to1 (ref.parameter->getDefaultValue()), nullptr);
                state.addChild (child, -1, nullptr);
            }
        parameters.replaceState (state);
    }
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AudioPluginAudioProcessor(); }
