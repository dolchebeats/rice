#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <atomic>
#include <new>
namespace { std::atomic<bool> watchAudioAllocations { false }; std::atomic<size_t> audioAllocations { 0 }; }
void* operator new (std::size_t n) { if (watchAudioAllocations.load()) ++audioAllocations; if (auto* p = std::malloc (n == 0 ? 1 : n)) return p; throw std::bad_alloc(); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void* operator new[] (std::size_t n) { return ::operator new (n); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
#ifdef _MSC_VER
#include <crtdbg.h>
#include <cstdlib>
#endif

namespace
{
std::vector<juce::Component*> children (juce::Component& parent)
{
    std::vector<juce::Component*> result;
    for (int i = 0; i < parent.getNumChildComponents(); ++i)
    {
        auto* child = parent.getChildComponent (i); result.push_back (child);
        const auto nested = children (*child); result.insert (result.end(), nested.begin(), nested.end());
    }
    return result;
}
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

void completeEngine()
{
    AudioPluginAudioProcessor p;
    const auto targets = rice::modulationTargets();
    set (p, "texture", 100); set (p, "voices", 4); set (p, "density", 60); set (p, "sizeMs", 60); set (p, "captureMs", 200);
    set (p, "spread", 80); set (p, "bufferLength", 2); set (p, "pitch", 7); set (p, "detune", 20); set (p, "randomPitch", 3);
    set (p, "scale", 2); set (p, "position", 25); set (p, "scan", 0.2f);
    p.prepareToPlay (4000, 64);
    juce::AudioBuffer<float> audio (2, 64); juce::MidiBuffer midi;
    double energy = 0.0;
    for (int block = 0; block < 200; ++block)
    {
        if (block == 100) { set (p, "feedback", 95); set (p, "feedbackDrive", 24); set (p, "feedbackHP", 40); set (p, "feedbackLP", 1400); }
        for (int i = 0; i < 64; ++i) { audio.setSample (0, i, std::sin ((block * 64 + i) * 0.11f) * 0.2f); audio.setSample (1, i, std::cos ((block * 64 + i) * 0.11f) * 0.2f); }
        watchAudioAllocations.store (true);
        p.processBlock (audio, midi);
        watchAudioAllocations.store (false);
        for (int i = 0; i < 64; ++i)
        { check (std::isfinite (audio.getSample (0, i)) && std::abs (audio.getSample (0, i)) <= 1.2001f, "Pitched cloud and maximum filtered feedback remain finite and bounded"); energy += std::abs (audio.getSample (0, i)); }
    }
    check (energy > 1.0 && p.visual.active.load() > 0, "Full Texture signal path produces overlapping audio");
    check (audioAllocations.load() == 0, "Complete audio callback performs no new/new[] allocations");
    set (p, "freeze", 1);
    audio.clear(); p.processBlock (audio, midi);
    for (int i = 0; i < 4; ++i) { audio.clear(); p.processBlock (audio, midi); }
    float before = 0.0f; for (const auto& bin : p.visual.waveform) before += bin.load();
    energy = 0;
    for (int i = 0; i < 40; ++i) { audio.clear(); p.processBlock (audio, midi); energy += audio.getMagnitude (0, 64); }
    float after = 0.0f; for (const auto& bin : p.visual.waveform) after += bin.load();
    check (energy > 0.01 && std::abs (after - before) < 0.0001f, "Freeze holds waveform data while playback and scan continue");

    set (p, "dryWet", 50); set (p, "followerGain", 4); set (p, "followerAttack", 0.1f);
    set (p, "mod0Source", 3); set (p, "mod0Target", static_cast<float> (targets.indexOf ("dryWet") + 1)); set (p, "mod0Amount", 50);
    for (int block = 0; block < 5; ++block)
    {
        for (int i = 0; i < 64; ++i) { audio.setSample (0, i, 1); audio.setSample (1, i, 1); }
        p.processBlock (audio, midi);
    }
    check (p.effectiveValue ("dryWet") > 99 && p.parameters.getRawParameterValue ("dryWet")->load() == 50, "Follower routes to a knob without altering its saved base value");
    set (p, "mod1Source", 1); set (p, "lfo1Mode", 1); set (p, "lfo1Shape", 3);
    set (p, "mod1Target", static_cast<float> (targets.indexOf ("envAttack") + 1)); set (p, "mod1Amount", 30);
    p.processBlock (audio, midi);
    check (std::abs (p.effectiveValue ("envAttack") - p.parameters.getRawParameterValue ("envAttack")->load()) > 1, "LFO routes to envelope time");
    juce::MemoryBlock saved; p.getStateInformation (saved); AudioPluginAudioProcessor recalled;
    recalled.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    for (const auto* parameter : p.getParameters())
    {
        const auto* ranged = dynamic_cast<const juce::RangedAudioParameter*> (parameter);
        check (ranged != nullptr && std::abs (parameter->getValue() - recalled.parameters.getParameter (ranged->paramID)->getValue()) < 0.00001f, "Every envelope, clock, feedback and routing parameter survives save/restore");
    }
}
void blockInvariance()
{
    const auto run = [] (const std::vector<int>& sizes)
    {
        AudioPluginAudioProcessor p; set (p, "texture", 100); set (p, "sizeMs", 60); set (p, "voices", 2); set (p, "density", 30);
        set (p, "lfo1Shape", 1); set (p, "mod0Source", 1); set (p, "mod0Target", static_cast<float> (rice::modulationTargets().indexOf ("pitch") + 1)); set (p, "mod0Amount", 10);
        p.prepareToPlay (1000, 127); juce::MidiBuffer midi; std::vector<float> output (2000);
        int time = 0; size_t block = 0;
        while (time < 2000)
        {
            const int count = std::min (sizes[block++ % sizes.size()], 2000 - time); juce::AudioBuffer<float> audio (2, count);
            for (int i = 0; i < count; ++i) { const auto value = std::sin ((time + i) * 0.1f); audio.setSample (0, i, value); audio.setSample (1, i, value); }
            p.processBlock (audio, midi);
            for (int i = 0; i < count; ++i) output[static_cast<size_t> (time + i)] = audio.getSample (0, i);
            time += count;
        }
        return output;
    };
    const auto regular = run ({ 64 }), variable = run ({ 1, 17, 127, 23, 64 });
    for (size_t i = 0; i < regular.size(); ++i) check (std::abs (regular[i] - variable[i]) < 0.00001f, "Grain and modulation clocks are independent of host block size");
}
int main (int argc, char** argv)
{
#ifdef _MSC_VER
    _CrtSetReportMode (_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile (_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior (0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    juce::ScopedJuceInitialiser_GUI init;
    dryWetAudio();
    completeEngine();
    blockInvariance();
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
        for (const auto& spec : rice::extraSpecs())
        {
            const auto child = legacyState.getChildWithProperty ("id", spec.id);
            if (child.isValid()) legacyState.removeChild (child, nullptr);
        }
        for (int i = legacyState.getNumChildren(); --i >= 0;)
            if (legacyState.getChild (i).getProperty ("id").toString().startsWith ("mod")) legacyState.removeChild (i, nullptr);
        set (legacyReader, "texture", 100); set (legacyReader, "feedback", 80); set (legacyReader, "envAttack", 800);
        juce::MemoryBlock legacyBytes;
        const auto legacyXml = legacyState.createXml(); juce::AudioProcessor::copyXmlToBinary (*legacyXml, legacyBytes);
        legacyReader.setStateInformation (legacyBytes.getData(), static_cast<int> (legacyBytes.getSize()));
        check (legacyReader.parameters.getRawParameterValue ("texture")->load() == 0 && legacyReader.parameters.getRawParameterValue ("feedback")->load() == 0,
            "Legacy sessions reset missing new controls even in an edited instance");
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
    set (restored, "texture", 75); set (restored, "voices", 4); set (restored, "density", 30); set (restored, "captureMode", 0);
    set (restored, "captureMs", 125); set (restored, "mod0Source", 1);
    set (restored, "mod0Target", static_cast<float> (rice::modulationTargets().indexOf ("position") + 1)); set (restored, "mod0Amount", 20);
    for (int i = 0; i < 16; ++i) set (restored, "step" + juce::String (i), std::sin (static_cast<float> (i) * 0.65f) * 0.8f);
    for (int block = 0; block < 24; ++block)
    {
        for (int i = 0; i < 64; ++i) { audio.setSample (0, i, std::sin ((block * 64 + i) * 0.1f) * 0.4f); audio.setSample (1, i, std::sin ((block * 64 + i) * 0.13f) * 0.3f); }
        restored.processBlock (audio, midi);
    }
    set (restored, "freeze", 1);
    for (int block = 0; block < 4; ++block) { audio.clear(); restored.processBlock (audio, midi); }

    std::unique_ptr<juce::AudioProcessorEditor> editor (restored.createEditor());
    auto& richEditor = dynamic_cast<AudioPluginAudioProcessorEditor&> (*editor);
    check (editor->getWidth() == 1180 && editor->getHeight() == 960, "Expanded editor dimensions");
    int knobs = 0;
    const auto descendants = children (*editor);
    for (auto* component : descendants) if (dynamic_cast<juce::Slider*> (component) != nullptr) ++knobs;
    check (knobs == 13, "Main tab exposes grain, buffer and mix knobs");
    int allKnobs = knobs;
    for (int tab = 1; tab < 5; ++tab)
    {
        richEditor.getTabs().setCurrentTabIndex (tab);
        for (auto* component : children (*editor)) if (dynamic_cast<juce::Slider*> (component) != nullptr) ++allKnobs;
    }
    check (allKnobs >= 50, "All tabs expose the envelope, motion, sequence and routing controls");
    richEditor.getTabs().setCurrentTabIndex (0);
    for (const auto* prefix : { "randomSize", "randomInterval", "randomPosition" })
    {
        const juce::String id (prefix);
        const auto label = id == "randomSize" ? "RANDOM SIZE" : id == "randomInterval" ? "RANDOM INTERVAL" : "RANDOM POSITION";
        juce::Slider* knob = nullptr;
        juce::ignoreUnused (label);
        for (auto* component : children (*editor))
            if (auto* slider = dynamic_cast<juce::Slider*> (component); slider != nullptr && slider->getComponentID() == id + "Ms") knob = slider;
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
    auto& envelopeEditor = richEditor.getEnvelopeEditor();
    const auto before = restored.envelopeSettings();
    envelopeEditor.editPoint (1, { 80.0f, 0.0f }, before, restored.grainDurationMs());
    check (restored.envelopeSettings().attack > before.attack, "Dragging attack point edits the parameter");
    envelopeEditor.editPoint (3, { 0.0f, 20.0f }, before, restored.grainDurationMs());
    check (restored.envelopeSettings().sustain < before.sustain, "Dragging decay point changes sustain level");
    for (int i = 0; i < 3; ++i) envelopeEditor.editCurve (i, -40.0f, before);
    check (restored.envelopeSettings().attackCurve > 0.0f && restored.envelopeSettings().decayCurve > 0.0f && restored.envelopeSettings().releaseCurve > 0.0f, "All curve handles edit independent curves");
    const auto event = [&envelopeEditor] (juce::Point<float> pos, juce::Point<float> down)
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
            1.0f, 0, 0, 0, 0, &envelopeEditor, &envelopeEditor, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, pos != down);
    };
    const auto attackBeforeDrag = restored.envelopeSettings().attack;
    const auto node = envelopeEditor.points()[1];
    envelopeEditor.mouseDown (event (node, node)); envelopeEditor.mouseDrag (event (node + juce::Point<float> (40, 0), node)); envelopeEditor.mouseUp (event (node + juce::Point<float> (40, 0), node));
    check (restored.envelopeSettings().attack > attackBeforeDrag, "Envelope mouse hit-testing and drag gestures edit attack");
    const auto handle = envelopeEditor.curves()[0];
    envelopeEditor.mouseDown (event (handle, handle)); envelopeEditor.mouseDrag (event (handle + juce::Point<float> (0, 20), handle)); envelopeEditor.mouseUp (event (handle + juce::Point<float> (0, 20), handle));
    check (restored.envelopeSettings().attackCurve < 0.5f, "Dragging the segment middle changes curvature");
    const auto resetHandle = envelopeEditor.curves()[0]; envelopeEditor.mouseDoubleClick (event (resetHandle, resetHandle));
    check (restored.envelopeSettings().attackCurve == 0.0f, "Double-click resets a curve to linear");
    if (argc > 1)
    {
            juce::File imageFile (juce::String::fromUTF8 (argv[1]));
        juce::FileOutputStream stream (imageFile);
        stream.setPosition (0); stream.truncate();
        check (stream.openedOk(), "Snapshot output opens");
        juce::PNGImageFormat png;
        check (png.writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds()), stream), "Editor renders to PNG");
        for (int tab = 1; tab < 5; ++tab)
        {
            richEditor.getTabs().setCurrentTabIndex (tab);
            juce::FileOutputStream tabStream (imageFile.getSiblingFile ("rice-tab-" + juce::String (tab) + ".png"));
            tabStream.setPosition (0); tabStream.truncate();
            check (png.writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds()), tabStream), "Every editor tab renders");
        }
    }
    std::cout << "Plugin state, transport, and editor checks passed.\n";
}
