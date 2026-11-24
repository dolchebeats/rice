#pragma once
#include "VisualComponents.h"
class AudioPluginAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor&);
    ~AudioPluginAudioProcessorEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    juce::TabbedComponent& getTabs() { return tabs; }
    EnvelopeEditor& getEnvelopeEditor() { return envelope; }
private:
    void timerCallback() override;
    void addControl (int panel, const juce::String& id, const juce::String& name, int col, int row,
                     const juce::String& mode = {}, const juce::StringArray& variants = {}, bool choice = false, bool toggle = false);
    AudioPluginAudioProcessor& processor;
    juce::LookAndFeel_V4 look;
    BufferDisplay waveform;
    EnvelopeEditor envelope;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    std::array<juce::Component, 5> panels;
    std::vector<std::unique_ptr<ParameterControl>> controls;
    std::array<juce::ComboBox, 8> routeSources, routeTargets;
    std::array<juce::Slider, 8> routeDepths;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>, 8> sourceAttachments, targetAttachments;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 8> depthAttachments;
    std::array<juce::Slider, 16> steps;
    std::array<juce::Label, 16> stepLabels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 16> stepAttachments;
    std::array<juce::Label, 5> notes;
    juce::Label status;
    juce::TooltipWindow tooltip { this, 600 };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessorEditor)
};
