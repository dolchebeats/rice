#pragma once
#include "PluginProcessor.h"

class AudioPluginAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor&);
    ~AudioPluginAudioProcessorEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    void timerCallback() override;
    void updateKnobs();
    AudioPluginAudioProcessor& processor;
    juce::LookAndFeel_V4 lookAndFeel;
    juce::Slider sizeKnob, captureKnob;
    juce::ComboBox sizeMode, captureMode, repeatMode;
    juce::Label sizeLabel, captureLabel, repeatLabel, tempoLabel, hintLabel;
    juce::TooltipWindow tooltip { this, 600 };
    std::unique_ptr<SliderAttachment> sizeAttachment, captureAttachment;
    std::unique_ptr<ComboAttachment> sizeModeAttachment, captureModeAttachment, repeatAttachment;
    int lastSizeMode = -1, lastCaptureMode = -1;
    std::uint64_t lastCaptureCount = 0;
    float captureFlash = 0.0f;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessorEditor)
};
