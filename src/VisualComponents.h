#pragma once
#include "PluginProcessor.h"

class ParameterControl final : public juce::Component
{
public:
    ParameterControl (AudioPluginAudioProcessor&, const juce::String& id, const juce::String& title,
                      const juce::String& modeId = {}, const juce::StringArray& variants = {}, bool choice = false, bool toggle = false);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    void rebind();
    AudioPluginAudioProcessor& processor;
    juce::Label label;
    juce::Slider knob;
    juce::ComboBox mode, selection;
    juce::ToggleButton button;
    juce::String id;
    juce::StringArray variants;
    bool isChoice = false, isToggle = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> knobAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment, choiceAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
};
class EnvelopeEditor final : public juce::Component
{
public:
    explicit EnvelopeEditor (AudioPluginAudioProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    std::array<juce::Point<float>, 6> points() const;
    std::array<juce::Point<float>, 3> curves() const;
    void editPoint (int index, juce::Point<float> delta, const rice::Envelope& initial, double duration);
    void editCurve (int index, float deltaY, const rice::Envelope& initial);
private:
    juce::Rectangle<float> plot() const;
    void set (const juce::String&, float value);
    AudioPluginAudioProcessor& processor;
    int selectedPoint = -1, selectedCurve = -1;
    juce::Point<float> dragStart;
    rice::Envelope initial;
    double initialDuration = 100.0;
    juce::StringArray gestures;
};
class BufferDisplay final : public juce::Component
{
public:
    explicit BufferDisplay (AudioPluginAudioProcessor& p) : processor (p) { setName ("Audio buffer and grain display"); }
    void paint (juce::Graphics&) override;
private:
    AudioPluginAudioProcessor& processor;
};
