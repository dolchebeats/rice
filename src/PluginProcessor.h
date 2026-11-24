#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "GrainEngine.h"
#include "CloudEngine.h"
#include "Modulation.h"
#include "AdvancedParameters.h"
#include <limits>

class AudioPluginAudioProcessor final : public juce::AudioProcessor
{
public:
    AudioPluginAudioProcessor();
    ~AudioPluginAudioProcessor() override = default;
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return std::numeric_limits<double>::infinity(); }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState parameters;
    std::atomic<double> displayBpm { 120.0 };
    std::atomic<bool> usingHostTempo { false };
    std::atomic<bool> displayPlaying { true };
    std::atomic<std::uint64_t> displayCaptureCount { 0 };
    rice::VisualState visual;
    std::array<std::atomic<float>, 5> displayMods {};
    rice::Envelope envelopeSettings() const;
    double grainDurationMs() const;
    float effectiveValue (const juce::String& id) const;
    std::array<std::atomic<float>, 20 + rice::ExtraCount + 24> displayedEffective {};

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameters();
    rice::GrainEngine engine;
    rice::CloudEngine cloud;
    rice::Modulation modulation;
    struct Ref { juce::RangedAudioParameter* parameter; std::atomic<float>* value; };
    std::vector<Ref> refs;
    std::vector<float> base, effective;
    std::vector<size_t> targetIndices;
    std::array<float, 2> previousWet {}, highState {}, lowState {};
    float feedbackAmount = 0.0f, drive = 1.0f, hpPole = 0.0f, lpPole = 0.0f;
    bool wasPlaying = true;
    int visualCountdown = 0;
    int controlCountdown = 0;
    std::array<float, 5> lastSources {};
    std::array<std::atomic<float>*, 20> values {};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wetMix { 1.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> textureMix { 0.0f };
    void updateDsp (const std::array<float, 5>& sources, double ppq, double step, bool sync, bool playing);
    double rate = 44100.0;
    double freePpq = 0.0;
    float previousSizeMode = -1.0f, previousSizeValue = -1.0f;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessor)
};
