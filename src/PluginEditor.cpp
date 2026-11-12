#include "PluginEditor.h"

namespace
{
const juce::Colour background { 0xff171b19 }, panel { 0xff222925 }, ink { 0xffeee9db }, accent { 0xffc3df89 };
}

AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    lookAndFeel.setColour (juce::Slider::rotarySliderFillColourId, accent);
    lookAndFeel.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff38433b));
    lookAndFeel.setColour (juce::Slider::thumbColourId, ink);
    lookAndFeel.setColour (juce::Slider::textBoxTextColourId, ink);
    lookAndFeel.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lookAndFeel.setColour (juce::ComboBox::backgroundColourId, background);
    lookAndFeel.setColour (juce::ComboBox::textColourId, ink);
    lookAndFeel.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff475349));
    lookAndFeel.setColour (juce::PopupMenu::backgroundColourId, background);
    lookAndFeel.setColour (juce::PopupMenu::textColourId, ink);
    lookAndFeel.setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff475349));
    setLookAndFeel (&lookAndFeel);
    for (auto* knob : { &sizeKnob, &captureKnob })
    {
        knob->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 150, 28);
        knob->setColour (juce::Slider::textBoxBackgroundColourId, panel);
        addAndMakeVisible (*knob);
    }
    sizeKnob.setName ("Grain size");
    captureKnob.setName ("Capture interval or transient sensitivity");
    sizeKnob.setTooltip ("Length of each recorded grain. Hear the first pass live, then repeat it in the selected direction.");
    captureKnob.setTooltip ("Time between capture starts. In Transient mode, higher sensitivity detects quieter attacks.");
    sizeMode.addItemList ({ "Milliseconds", "Tempo sync" }, 1);
    captureMode.addItemList ({ "Milliseconds", "Tempo sync", "Transient" }, 1);
    repeatMode.addItemList ({ "Straight", "Reverse", "Back-and-forth" }, 1);
    sizeMode.setName ("Grain size units");
    captureMode.setName ("Capture trigger mode");
    repeatMode.setName ("Grain playback direction");
    for (auto* combo : { &sizeMode, &captureMode, &repeatMode })
        addAndMakeVisible (*combo);
    for (auto* label : { &sizeLabel, &captureLabel, &repeatLabel, &tempoLabel, &hintLabel })
    {
        label->setColour (juce::Label::textColourId, ink);
        label->setJustificationType (juce::Justification::centred);
        addAndMakeVisible (*label);
    }
    sizeLabel.setText ("GRAIN SIZE", juce::dontSendNotification);
    repeatLabel.setText ("REPEAT", juce::dontSendNotification);
    hintLabel.setText ("T = triplet     D = dotted     1 = whole note", juce::dontSendNotification);
    hintLabel.setColour (juce::Label::textColourId, juce::Colour (0xff9ca89c));
    sizeModeAttachment = std::make_unique<ComboAttachment> (processor.parameters, "sizeMode", sizeMode);
    captureModeAttachment = std::make_unique<ComboAttachment> (processor.parameters, "captureMode", captureMode);
    repeatAttachment = std::make_unique<ComboAttachment> (processor.parameters, "repeat", repeatMode);
    sizeMode.onChange = [this] { updateKnobs(); };
    captureMode.onChange = [this] { updateKnobs(); };
    updateKnobs();
    setSize (620, 470);
    startTimerHz (30);
    timerCallback();
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void AudioPluginAudioProcessorEditor::updateKnobs()
{
    const auto size = juce::roundToInt (processor.parameters.getRawParameterValue ("sizeMode")->load());
    const auto capture = juce::roundToInt (processor.parameters.getRawParameterValue ("captureMode")->load());
    if (size != lastSizeMode)
    {
        sizeAttachment.reset();
        sizeAttachment = std::make_unique<SliderAttachment> (processor.parameters, size == 0 ? "sizeMs" : "sizeSync", sizeKnob);
        lastSizeMode = size;
    }
    if (capture != lastCaptureMode)
    {
        captureAttachment.reset();
        captureAttachment = std::make_unique<SliderAttachment> (processor.parameters,
            capture == 0 ? "captureMs" : capture == 1 ? "captureSync" : "sensitivity", captureKnob);
        captureLabel.setText (capture == 2 ? "SENSITIVITY" : "CAPTURE INTERVAL", juce::dontSendNotification);
        lastCaptureMode = capture;
    }
}

void AudioPluginAudioProcessorEditor::timerCallback()
{
    updateKnobs();
    const auto bpm = processor.displayBpm.load (std::memory_order_relaxed);
    tempoLabel.setText (juce::String (bpm, 1) + (processor.usingHostTempo.load() ? " BPM / HOST" : " BPM / FALLBACK"), juce::dontSendNotification);
    const auto count = processor.displayCaptureCount.load (std::memory_order_relaxed);
    captureFlash = count != lastCaptureCount ? 1.0f : captureFlash * 0.78f;
    lastCaptureCount = count;
    repaint();
}

void AudioPluginAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (ink);
    g.setFont (juce::FontOptions (44.0f, juce::Font::bold));
    g.drawText ("rice", 30, 16, 150, 58, juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (13.0f));
    g.setColour (juce::Colour (0xff9ca89c));
    g.drawText ("CAPTURE / REPEAT", 150, 34, 220, 24, juce::Justification::centredLeft);
    g.setColour (panel);
    g.fillRoundedRectangle (26.0f, 90.0f, 278.0f, 270.0f, 14.0f);
    g.fillRoundedRectangle (316.0f, 90.0f, 278.0f, 270.0f, 14.0f);
    g.setColour (accent.withAlpha (0.2f + captureFlash * 0.8f));
    g.fillEllipse (572.0f, 28.0f, 9.0f, 9.0f);
}

void AudioPluginAudioProcessorEditor::resized()
{
    sizeLabel.setBounds (46, 104, 238, 26);
    captureLabel.setBounds (336, 104, 238, 26);
    sizeKnob.setBounds (65, 136, 200, 170);
    captureKnob.setBounds (355, 136, 200, 170);
    sizeMode.setBounds (64, 316, 202, 28);
    captureMode.setBounds (354, 316, 202, 28);
    repeatLabel.setBounds (38, 382, 100, 30);
    repeatMode.setBounds (146, 382, 218, 30);
    tempoLabel.setBounds (378, 382, 216, 30);
    hintLabel.setBounds (26, 428, 568, 24);
}
