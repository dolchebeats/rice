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
    return layout;
}

AudioPluginAudioProcessor::AudioPluginAudioProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "RiceParameters", createParameters())
{
    const std::array<const char*, 8> ids { "sizeMode", "sizeMs", "sizeSync", "captureMode", "captureMs", "captureSync", "sensitivity", "repeat" };
    for (size_t i = 0; i < ids.size(); ++i)
        values[i] = parameters.getRawParameterValue (ids[i]);
}

void AudioPluginAudioProcessor::prepareToPlay (double sampleRate, int)
{
    rate = sampleRate;
    engine.prepare (rate);
    previousSizeMode = previousSizeValue = -1.0f;
}

void AudioPluginAudioProcessor::reset()
{
    engine.reset();
    displayCaptureCount.store (0, std::memory_order_relaxed);
}

bool AudioPluginAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return (layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
         || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo())
        && layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet();
}

void AudioPluginAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels() == 0)
        return;
    double bpm = 120.0, ppq = 0.0;
    bool hostTempo = false, followHost = false, transportPlaying = true;
    if (auto* hostPlayHead = getPlayHead())
        if (const auto position = hostPlayHead->getPosition())
        {
            transportPlaying = position->getIsPlaying() || position->getIsRecording();
            if (const auto tempo = position->getBpm(); tempo && std::isfinite (*tempo) && *tempo > 0.0)
            {
                bpm = std::clamp (*tempo, 20.0, 999.0);
                hostTempo = true;
            }
            if (const auto beats = position->getPpqPosition(); beats && std::isfinite (*beats))
            {
                ppq = *beats;
                followHost = hostTempo && position->getIsPlaying();
            }
        }
    displayBpm.store (bpm, std::memory_order_relaxed);
    usingHostTempo.store (hostTempo, std::memory_order_relaxed);
    const auto load = [this] (size_t index) { return values[index]->load (std::memory_order_relaxed); };
    const auto sizeMode = load (0);
    const auto sizeValue = sizeMode < 0.5f ? load (1) : load (2);
    if (sizeMode != previousSizeMode || sizeValue != previousSizeValue)
    {
        engine.cancelPendingCaptures();
        previousSizeMode = sizeMode;
        previousSizeValue = sizeValue;
    }
    const auto grainMs = sizeMode < 0.5f ? static_cast<double> (sizeValue)
                                       : rice::millisecondsForDivision (juce::roundToInt (sizeValue), bpm);
    const auto captureMode = juce::roundToInt (load (3));
    const auto syncIndex = juce::jlimit (0, 29, juce::roundToInt (load (5)));
    const auto intervalMs = captureMode == 1 ? rice::millisecondsForDivision (syncIndex, bpm) : static_cast<double> (load (4));
    rice::GrainEngine::Settings settings;
    settings.grainSamples = juce::roundToInt (grainMs * rate * 0.001);
    settings.intervalSamples = intervalMs * rate * 0.001;
    settings.intervalBeats = rice::divisions()[static_cast<size_t> (syncIndex)].beats;
    settings.sensitivity = load (6) * 0.01f;
    settings.repeat = static_cast<rice::GrainEngine::Repeat> (juce::jlimit (0, 2, juce::roundToInt (load (7))));
    settings.capture = static_cast<rice::GrainEngine::Capture> (juce::jlimit (0, 2, captureMode));
    engine.beginBlock (settings, ppq, bpm / (60.0 * rate), followHost, transportPlaying);
    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto result = engine.process ({ left[sample], right != nullptr ? right[sample] : left[sample] });
        left[sample] = result[0];
        if (right != nullptr)
            right[sample] = result[1];
    }
    displayCaptureCount.store (engine.getCaptureCount(), std::memory_order_relaxed);
}

juce::AudioProcessorEditor* AudioPluginAudioProcessor::createEditor()
{
    return new AudioPluginAudioProcessorEditor (*this);
}

void AudioPluginAudioProcessor::getStateInformation (juce::MemoryBlock& data)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, data);
}

void AudioPluginAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioPluginAudioProcessor();
}
