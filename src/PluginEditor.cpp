#include "PluginEditor.h"

namespace
{
const juce::Colour background { 0xff171b19 }, panel { 0xff222925 }, ink { 0xffeee9db }, accent { 0xffc3df89 };
const std::array<juce::String, 3> randomPrefixes { "randomSize", "randomInterval", "randomPosition" };
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
    const auto addKnob = [this] (juce::Slider& knob)
    {
        knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 158, 28);
        knob.setColour (juce::Slider::textBoxBackgroundColourId, panel);
        addAndMakeVisible (knob);
    };
    for (auto* knob : { &sizeKnob, &captureKnob, &voicesKnob, &spreadKnob })
        addKnob (*knob);
    for (auto& knob : randomKnobs)
        addKnob (knob);
    sizeKnob.setName ("Grain size");
    captureKnob.setName ("Capture interval or transient sensitivity");
    voicesKnob.setName ("Voices");
    spreadKnob.setName ("Spread");
    sizeKnob.setTooltip ("Length of each recorded grain. Hear the first pass live, then repeat it in the selected direction.");
    captureKnob.setTooltip ("Time between capture starts. In Transient mode, higher sensitivity detects quieter attacks.");
    voicesKnob.setTooltip ("1 to 16 independent grain voices. The output is normalised as voices are added.");
    spreadKnob.setTooltip ("Space the voices evenly across the stereo field. One voice stays centered.");
    sizeMode.addItemList ({ "Milliseconds", "Tempo sync" }, 1);
    captureMode.addItemList ({ "Milliseconds", "Tempo sync", "Transient" }, 1);
    repeatMode.addItemList ({ "Straight", "Reverse", "Back-and-forth" }, 1);
    sizeMode.setName ("Grain size units");
    captureMode.setName ("Capture trigger mode");
    repeatMode.setName ("Grain playback direction");
    for (auto* combo : { &sizeMode, &captureMode, &repeatMode })
        addAndMakeVisible (*combo);
    const auto addLabel = [this] (juce::Label& label, const juce::String& text)
    {
        label.setColour (juce::Label::textColourId, ink);
        label.setJustificationType (juce::Justification::centred);
        label.setText (text, juce::dontSendNotification);
        addAndMakeVisible (label);
    };
    addLabel (sizeLabel, "GRAIN SIZE");
    addLabel (captureLabel, "CAPTURE INTERVAL");
    addLabel (voicesLabel, "VOICES");
    addLabel (spreadLabel, "SPREAD");
    addLabel (repeatLabel, "REPEAT");
    addLabel (tempoLabel, "");
    addLabel (hintLabel, "Random amounts = +/- per voice and capture     |     T = triplet     D = dotted     1 = whole note");
    hintLabel.setColour (juce::Label::textColourId, juce::Colour (0xff9ca89c));
    const std::array<juce::String, 3> labels { "RANDOM SIZE", "RANDOM INTERVAL", "RANDOM POSITION" };
    const std::array<juce::String, 3> tips {
        "Pick a new grain length within +/- this amount for each voice at each capture. Zero / Off disables it.",
        "Pick a new capture interval within +/- this amount per voice. In Transient mode, jitter the capture around each attack.",
        "Pick a grain source start within +/- this amount per voice. Earlier audio comes from history; later audio must arrive first." };
    for (size_t i = 0; i < randomKnobs.size(); ++i)
    {
        addLabel (randomLabels[i], labels[i]);
        randomKnobs[i].setName (labels[i]);
        randomKnobs[i].setTooltip (tips[i]);
        randomModes[i].addItemList ({ "Milliseconds", "Tempo sync" }, 1);
        randomModes[i].setName (labels[i] + " units");
        addAndMakeVisible (randomModes[i]);
        randomModeAttachments[i] = std::make_unique<ComboAttachment> (processor.parameters, randomPrefixes[i] + "Mode", randomModes[i]);
        randomModes[i].onChange = [this] { updateKnobs(); };
    }
    sizeModeAttachment = std::make_unique<ComboAttachment> (processor.parameters, "sizeMode", sizeMode);
    captureModeAttachment = std::make_unique<ComboAttachment> (processor.parameters, "captureMode", captureMode);
    repeatAttachment = std::make_unique<ComboAttachment> (processor.parameters, "repeat", repeatMode);
    voicesAttachment = std::make_unique<SliderAttachment> (processor.parameters, "voices", voicesKnob);
    spreadAttachment = std::make_unique<SliderAttachment> (processor.parameters, "spread", spreadKnob);
    sizeMode.onChange = [this] { updateKnobs(); };
    captureMode.onChange = [this] { updateKnobs(); };
    updateKnobs();
    setSize (900, 684);
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
    // Attachments update the dropdown before APVTS raw-value listeners finish.
    // Read the displayed selection so onChange can rebind without a timer delay.
    const auto size = sizeMode.getSelectedItemIndex();
    const auto capture = captureMode.getSelectedItemIndex();
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
    for (size_t i = 0; i < randomKnobs.size(); ++i)
    {
        const auto mode = randomModes[i].getSelectedItemIndex();
        if (mode != lastRandomModes[i])
        {
            randomAttachments[i].reset();
            randomAttachments[i] = std::make_unique<SliderAttachment> (processor.parameters,
                randomPrefixes[i] + (mode == 0 ? "Ms" : "Sync"), randomKnobs[i]);
            lastRandomModes[i] = mode;
        }
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
    g.drawText ("CAPTURE / REPEAT / SCATTER", 150, 34, 300, 24, juce::Justification::centredLeft);
    g.setColour (panel);
    for (int i = 0; i < 4; ++i)
        g.fillRoundedRectangle (26.0f + 214.0f * static_cast<float> (i), 94.0f, 206.0f, 244.0f, 14.0f);
    for (int i = 0; i < 3; ++i)
        g.fillRoundedRectangle (26.0f + 286.0f * static_cast<float> (i), 354.0f, 276.0f, 244.0f, 14.0f);
    g.setColour (accent.withAlpha (0.2f + captureFlash * 0.8f));
    g.fillEllipse (858.0f, 40.0f, 9.0f, 9.0f);
}

void AudioPluginAudioProcessorEditor::resized()
{
    const std::array<juce::Label*, 4> labels { &sizeLabel, &captureLabel, &voicesLabel, &spreadLabel };
    const std::array<juce::Slider*, 4> knobs { &sizeKnob, &captureKnob, &voicesKnob, &spreadKnob };
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        const int x = 26 + static_cast<int> (i) * 214;
        labels[i]->setBounds (x + 8, 106, 190, 26);
        knobs[i]->setBounds (x + 13, 138, 180, 146);
    }
    sizeMode.setBounds (44, 296, 170, 28);
    captureMode.setBounds (258, 296, 170, 28);
    for (size_t i = 0; i < randomKnobs.size(); ++i)
    {
        const int x = 26 + static_cast<int> (i) * 286;
        randomLabels[i].setBounds (x + 8, 366, 260, 26);
        randomKnobs[i].setBounds (x + 48, 398, 180, 146);
        randomModes[i].setBounds (x + 38, 556, 200, 28);
    }
    repeatLabel.setBounds (26, 616, 100, 30);
    repeatMode.setBounds (134, 616, 218, 30);
    tempoLabel.setBounds (618, 26, 224, 38);
    hintLabel.setBounds (26, 650, 848, 24);
}
