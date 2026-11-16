#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
void check (bool passed, const char* message)
{
    if (! passed) { std::cerr << message << '\n'; std::exit (1); }
}
void set (AudioPluginAudioProcessor& processor, const juce::String& id, float value)
{
    auto* parameter = processor.parameters.getParameter (id);
    check (parameter != nullptr, "Parameter exists");
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}
class PlayHead final : public juce::AudioPlayHead
{
public:
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing);
        info.setBpm (120.0);
        info.setPpqPosition (0.0);
        return info;
    }
};

void dryWetAudio()
{
    AudioPluginAudioProcessor processor;
    set (processor, "sizeMs", 4.0f);
    set (processor, "captureMs", 1000.0f);
    processor.prepareToPlay (1000.0, 64);
    juce::AudioBuffer<float> audio (2, 64);
    juce::MidiBuffer midi;
    const auto process = [&] (float input)
    {
        for (int i = 0; i < 64; ++i)
        {
            audio.setSample (0, i, input);
            audio.setSample (1, i, -input);
        }
        processor.processBlock (audio, midi);
    };
    process (0.2f); // Capture a constant grain.
    process (0.8f);
    check (std::abs (audio.getSample (0, 63) - 0.2f) < 0.00001f, "100% wet contains only the grain");
    set (processor, "dryWet", 0.0f);
    process (0.8f);
    for (int i = 1; i < 64; ++i)
        check (std::abs (audio.getSample (0, i) - audio.getSample (0, i - 1)) <= 0.031f, "Mix automation ramps instead of jumping");
    check (audio.getSample (0, 63) == 0.8f && audio.getSample (1, 63) == -0.8f, "0% wet preserves the original stereo input exactly");
    set (processor, "dryWet", 50.0f);
    process (0.8f);
    check (std::abs (audio.getSample (0, 63) - 0.5f) < 0.00001f
        && std::abs (audio.getSample (1, 63) + 0.5f) < 0.00001f, "50% wet linearly blends both stereo channels");
    set (processor, "dryWet", 100.0f);
    process (0.8f);
    check (std::abs (audio.getSample (0, 63) - 0.2f) < 0.00001f, "Grain processing continues while the output is dry");
    set (processor, "dryWet", 0.0f);
    auto layout = processor.getBusesLayout();
    layout.inputBuses.set (0, juce::AudioChannelSet::mono());
    layout.outputBuses.set (0, juce::AudioChannelSet::mono());
    check (processor.setBusesLayout (layout), "Mono layout accepted");
    processor.prepareToPlay (1000.0, 64);
    juce::AudioBuffer<float> mono (1, 64);
    for (int i = 0; i < 64; ++i) mono.setSample (0, i, 0.37f);
    processor.processBlock (mono, midi);
    check (mono.getSample (0, 0) == 0.37f && mono.getSample (0, 63) == 0.37f, "Dry mono input is unchanged from the first sample");
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    dryWetAudio();
    AudioPluginAudioProcessor original;
    check (original.parameters.getRawParameterValue ("dryWet")->load() == 100.0f, "Mix defaults to fully wet");
    set (original, "dryWet", 39.0f);
    check (original.parameters.getRawParameterValue ("voices")->load() == 1.0f, "Default voice count is one");
    set (original, "voices", 16.0f);
    set (original, "spread", 87.0f);
    set (original, "randomSizeMs", 13.0f);
    set (original, "randomIntervalMode", 1.0f);
    set (original, "randomIntervalSync", 8.0f);
    set (original, "randomPositionMs", 70.0f);
    set (original, "captureMode", 2.0f);
    juce::MemoryBlock state;
    original.getStateInformation (state);
    AudioPluginAudioProcessor restored;
    restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    for (const auto* id : { "voices", "spread", "randomSizeMs", "randomIntervalMode", "randomIntervalSync", "randomPositionMs", "captureMode", "dryWet" })
        check (std::abs (original.parameters.getRawParameterValue (id)->load() - restored.parameters.getRawParameterValue (id)->load()) < 0.001f, "New controls survive session-state roundtrip");
    {
        auto legacyState = original.parameters.copyState();
        for (int i = legacyState.getNumChildren(); --i >= 0;)
        {
            const auto id = legacyState.getChild (i).getProperty ("id").toString();
            if (id == "voices" || id == "spread" || id == "dryWet" || id.startsWith ("random"))
                legacyState.removeChild (i, nullptr);
        }
        AudioPluginAudioProcessor legacyReader;
        legacyReader.parameters.replaceState (legacyState);
        check (legacyReader.parameters.getRawParameterValue ("voices")->load() == 1.0f
            && legacyReader.parameters.getRawParameterValue ("randomSizeMs")->load() == 0.0f,
            "Older sessions keep the single-voice, no-randomness defaults");
        check (legacyReader.parameters.getRawParameterValue ("dryWet")->load() == 100.0f, "Older sessions retain fully wet playback");
    }
    PlayHead playHead;
    restored.setPlayHead (&playHead);
    set (restored, "dryWet", 100.0f);
    restored.prepareToPlay (1000.0, 64);
    juce::AudioBuffer<float> audio (2, 64);
    juce::MidiBuffer midi;
    for (int block = 0; block < 10; ++block)
    {
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < 64; ++i) audio.setSample (channel, i, 0.25f);
        restored.processBlock (audio, midi);
        check (audio.getMagnitude (0, 64) <= 0.25001f, "Processor routes new controls to a normalised polyphonic mix");
    }
    playHead.playing = false;
    restored.processBlock (audio, midi);
    check (audio.getMagnitude (0, 64) == 0.0f, "Processor honours host transport stop");
    restored.setPlayHead (nullptr);

    std::unique_ptr<juce::AudioProcessorEditor> editor (restored.createEditor());
    check (editor->getWidth() == 900 && editor->getHeight() == 684, "Expanded editor dimensions");
    int knobs = 0;
    for (int i = 0; i < editor->getNumChildComponents(); ++i)
        if (dynamic_cast<juce::Slider*> (editor->getChildComponent (i)) != nullptr) ++knobs;
    check (knobs == 8, "Editor includes the Dry/Wet knob");
    for (const auto* prefix : { "randomSize", "randomInterval", "randomPosition" })
    {
        const juce::String id (prefix);
        const auto label = id == "randomSize" ? "RANDOM SIZE" : id == "randomInterval" ? "RANDOM INTERVAL" : "RANDOM POSITION";
        juce::Slider* knob = nullptr;
        for (int i = 0; i < editor->getNumChildComponents(); ++i)
            if (auto* slider = dynamic_cast<juce::Slider*> (editor->getChildComponent (i)); slider != nullptr && slider->getName() == label)
                knob = slider;
        check (knob != nullptr, "Each random control is accessible by name");
        set (restored, id + "Ms", 19.0f);
        set (restored, id + "Sync", 5.0f);
        set (restored, id + "Mode", 1.0f);
        check (std::abs (knob->getValue() - 5.0) < 0.001, "Host sync-mode automation rebinds the knob");
        knob->setValue (9.0, juce::sendNotificationSync);
        check (restored.parameters.getRawParameterValue (id + "Sync")->load() == 9.0f, "Sync knob edits reach the correct parameter");
        set (restored, id + "Mode", 0.0f);
        check (std::abs (knob->getValue() - 19.0) < 0.001, "Switching back restores the separate millisecond amount");
    }
    if (argc > 1)
    {
        juce::File imageFile (juce::String::fromUTF8 (argv[1]));
        juce::FileOutputStream stream (imageFile);
        stream.setPosition (0); stream.truncate();
        check (stream.openedOk(), "Snapshot output opens");
        juce::PNGImageFormat png;
        check (png.writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds()), stream), "Editor renders to PNG");
    }
    std::cout << "Plugin state, transport, and editor checks passed.\n";
}
