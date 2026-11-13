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
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    AudioPluginAudioProcessor original;
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
    for (const auto* id : { "voices", "spread", "randomSizeMs", "randomIntervalMode", "randomIntervalSync", "randomPositionMs", "captureMode" })
        check (std::abs (original.parameters.getRawParameterValue (id)->load() - restored.parameters.getRawParameterValue (id)->load()) < 0.001f, "New controls survive session-state roundtrip");
    {
        auto legacyState = original.parameters.copyState();
        for (int i = legacyState.getNumChildren(); --i >= 0;)
        {
            const auto id = legacyState.getChild (i).getProperty ("id").toString();
            if (id == "voices" || id == "spread" || id.startsWith ("random"))
                legacyState.removeChild (i, nullptr);
        }
        AudioPluginAudioProcessor legacyReader;
        legacyReader.parameters.replaceState (legacyState);
        check (legacyReader.parameters.getRawParameterValue ("voices")->load() == 1.0f
            && legacyReader.parameters.getRawParameterValue ("randomSizeMs")->load() == 0.0f,
            "Older sessions keep the single-voice, no-randomness defaults");
    }
    PlayHead playHead;
    restored.setPlayHead (&playHead);
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
    check (knobs == 7, "Editor contains both existing knobs and all five new knobs");
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
