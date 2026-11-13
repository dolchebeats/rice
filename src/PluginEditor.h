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
    juce::Slider sizeKnob, captureKnob, voicesKnob, spreadKnob;
    std::array<juce::Slider, 3> randomKnobs;
    juce::ComboBox sizeMode, captureMode, repeatMode;
    std::array<juce::ComboBox, 3> randomModes;
    juce::Label sizeLabel, captureLabel, voicesLabel, spreadLabel, repeatLabel, tempoLabel, hintLabel;
    std::array<juce::Label, 3> randomLabels;
    juce::TooltipWindow tooltip { this, 600 };
    std::unique_ptr<SliderAttachment> sizeAttachment, captureAttachment, voicesAttachment, spreadAttachment;
    std::array<std::unique_ptr<SliderAttachment>, 3> randomAttachments;
    std::unique_ptr<ComboAttachment> sizeModeAttachment, captureModeAttachment, repeatAttachment;
    std::array<std::unique_ptr<ComboAttachment>, 3> randomModeAttachments;
    std::array<int, 3> lastRandomModes { -1, -1, -1 };
    int lastSizeMode = -1, lastCaptureMode = -1;
    std::uint64_t lastCaptureCount = 0;
    float captureFlash = 0.0f;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessorEditor)
};
