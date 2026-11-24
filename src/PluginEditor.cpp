#include "PluginEditor.h"
namespace { const juce::Colour ink (0xffeee9db), accent (0xffc3df89), background (0xff171b19); }
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), waveform (p), envelope (p)
{
    look.setColour (juce::Slider::rotarySliderFillColourId, accent);
    look.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff3c473e));
    look.setColour (juce::Slider::thumbColourId, ink); look.setColour (juce::Slider::textBoxTextColourId, ink);
    look.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    look.setColour (juce::ComboBox::backgroundColourId, background); look.setColour (juce::ComboBox::textColourId, ink);
    look.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff475349));
    look.setColour (juce::PopupMenu::backgroundColourId, background); look.setColour (juce::PopupMenu::textColourId, ink);
    look.setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff475349));
    look.setColour (juce::ToggleButton::textColourId, ink); look.setColour (juce::ToggleButton::tickColourId, accent);
    setLookAndFeel (&look);
    addAndMakeVisible (waveform); addAndMakeVisible (tabs); addAndMakeVisible (status);
    const std::array<const char*, 5> names { "Grains & Buffer", "Pitch & Feedback", "Envelope", "Motion", "Mod Matrix" };
    for (size_t i = 0; i < panels.size(); ++i) tabs.addTab (names[i], background, &panels[i], false);
    addControl (0, "sizeMs", "GRAIN SIZE", 0, 0, "sizeMode", { "sizeMs", "sizeSync" });
    addControl (0, "captureMs", "CAPTURE / SENSITIVITY", 1, 0, "captureMode", { "captureMs", "captureSync", "sensitivity" });
    addControl (0, "voices", "VOICES", 2, 0); addControl (0, "spread", "SPREAD", 3, 0);
    addControl (0, "randomSizeMs", "RANDOM SIZE", 0, 1, "randomSizeMode", { "randomSizeMs", "randomSizeSync" });
    addControl (0, "randomIntervalMs", "RANDOM INTERVAL", 1, 1, "randomIntervalMode", { "randomIntervalMs", "randomIntervalSync" });
    addControl (0, "randomPositionMs", "RANDOM POSITION", 2, 1, "randomPositionMode", { "randomPositionMs", "randomPositionSync" });
    addControl (0, "dryWet", "DRY / WET", 3, 1);
    addControl (0, "texture", "BEAT / TEXTURE", 0, 2); addControl (0, "density", "DENSITY (TEXTURE)", 1, 2);
    addControl (0, "position", "BUFFER POSITION", 2, 2); addControl (0, "scan", "SCAN SPEED", 3, 2);
    addControl (0, "bufferLength", "BUFFER LENGTH", 0, 3); addControl (0, "freeze", "FREEZE BUFFER", 1, 3, {}, {}, false, true);
    addControl (0, "repeat", "REPEAT DIRECTION", 2, 3, {}, {}, true);
    addControl (1, "pitch", "PITCH", 0, 0); addControl (1, "detune", "VOICE DETUNE", 1, 0); addControl (1, "randomPitch", "RANDOM PITCH", 2, 0);
    addControl (1, "scale", "PITCH SCALE", 3, 0, {}, {}, true); addControl (1, "root", "SCALE ROOT", 0, 1, {}, {}, true);
    addControl (1, "feedback", "FEEDBACK", 1, 1); addControl (1, "feedbackHP", "FEEDBACK HIGH-PASS", 2, 1); addControl (1, "feedbackLP", "FEEDBACK LOW-PASS", 3, 1);
    addControl (1, "feedbackDrive", "FEEDBACK SATURATION", 1, 2);
    panels[2].addAndMakeVisible (envelope);
    addControl (2, "beatEnvelope", "SHAPE BEAT GRAINS", 3, 3, {}, {}, false, true);
    const std::array<const char*, 8> envelopeIds { "envAttack", "envHold", "envDecay", "envSustain", "envRelease", "envAttackCurve", "envDecayCurve", "envReleaseCurve" };
    const std::array<const char*, 8> envelopeNames { "ATTACK", "HOLD", "DECAY", "SUSTAIN", "RELEASE", "ATTACK CURVE", "DECAY CURVE", "RELEASE CURVE" };
    for (size_t i = 0; i < 8; ++i) addControl (2, envelopeIds[i], envelopeNames[i], static_cast<int> (i % 4), 2 + static_cast<int> (i / 4));
    // Place the Beat envelope toggle in the graph header rather than over a curve control.
    controls[controls.size() - 9]->setBounds (860, 235, 270, 55);
    addControl (3, "lfo1Rate", "LFO 1 RATE", 0, 0, "lfo1Mode", { "lfo1Rate", "lfo1Sync" });
    addControl (3, "lfo1Shape", "LFO 1 SHAPE", 1, 0, {}, {}, true);
    addControl (3, "lfo2Rate", "LFO 2 RATE", 2, 0, "lfo2Mode", { "lfo2Rate", "lfo2Sync" });
    addControl (3, "lfo2Shape", "LFO 2 SHAPE", 3, 0, {}, {}, true);
    addControl (3, "followerAttack", "FOLLOWER ATTACK", 0, 1); addControl (3, "followerRelease", "FOLLOWER RELEASE", 1, 1);
    addControl (3, "followerGain", "FOLLOWER GAIN", 2, 1); addControl (3, "randomRate", "SMOOTH RANDOM RATE", 3, 1);
    addControl (3, "sequenceLength", "SEQUENCE LENGTH", 0, 2); addControl (3, "sequenceSync", "SEQUENCE STEP DIVISION", 1, 2);
    for (size_t i = 0; i < steps.size(); ++i)
    {
        auto& slider = steps[i]; slider.setSliderStyle (juce::Slider::LinearVertical); slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 58, 24);
        slider.setName ("Sequence step " + juce::String (static_cast<int> (i + 1))); slider.setComponentID ("step" + juce::String (static_cast<int> (i)));
        panels[3].addAndMakeVisible (slider); stepAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.parameters, slider.getComponentID(), slider);
        stepLabels[i].setText (juce::String (static_cast<int> (i + 1)), juce::dontSendNotification); stepLabels[i].setJustificationType (juce::Justification::centred);
        stepLabels[i].setColour (juce::Label::textColourId, ink); panels[3].addAndMakeVisible (stepLabels[i]);
    }
    for (size_t i = 0; i < 8; ++i)
    {
        const auto prefix = "mod" + juce::String (static_cast<int> (i));
        for (auto* combo : { &routeSources[i], &routeTargets[i] }) panels[4].addAndMakeVisible (*combo);
        routeSources[i].addItemList (dynamic_cast<juce::AudioParameterChoice*> (processor.parameters.getParameter (prefix + "Source"))->choices, 1);
        routeTargets[i].addItemList (dynamic_cast<juce::AudioParameterChoice*> (processor.parameters.getParameter (prefix + "Target"))->choices, 1);
        routeSources[i].setName ("Route " + juce::String (static_cast<int> (i + 1)) + " source"); routeTargets[i].setName ("Route " + juce::String (static_cast<int> (i + 1)) + " target");
        sourceAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.parameters, prefix + "Source", routeSources[i]);
        targetAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.parameters, prefix + "Target", routeTargets[i]);
        routeDepths[i].setSliderStyle (juce::Slider::LinearHorizontal); routeDepths[i].setTextBoxStyle (juce::Slider::TextBoxRight, false, 80, 26);
        routeDepths[i].setName ("Route " + juce::String (static_cast<int> (i + 1)) + " depth"); routeDepths[i].setComponentID (prefix + "Amount");
        panels[4].addAndMakeVisible (routeDepths[i]); depthAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.parameters, prefix + "Amount", routeDepths[i]);
    }
    const std::array<juce::String, 5> hints {
        "Beat keeps live first passes. Texture launches overlapping grains from captured audio. Density is launches per second per voice.",
        "Wet output -> high-pass -> low-pass -> saturation -> recording buffer. Freeze holds the buffer and stops feedback recording.",
        "Envelope times fit the grain lifetime; any remaining time is sustain. Texture always uses this envelope; Beat shaping is optional.",
        "Assign sources in Mod Matrix. Free LFOs use Hz; synced LFOs and the sequencer follow DAW beats. Smooth random glides between values.",
        "Choose a source and target, then set signed depth. Each route adds modulation without changing the saved knob value." };
    for (size_t i = 0; i < notes.size(); ++i)
    {
        notes[i].setText (hints[i], juce::dontSendNotification); notes[i].setColour (juce::Label::textColourId, juce::Colour (0xff9ca89c));
        notes[i].setFont (juce::FontOptions (13.0f)); notes[i].setJustificationType (juce::Justification::centredLeft); panels[i].addAndMakeVisible (notes[i]);
    }
    status.setColour (juce::Label::textColourId, juce::Colour (0xff9ca89c)); status.setJustificationType (juce::Justification::centredLeft);
    setSize (1180, 960); startTimerHz (30); timerCallback();
}
AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor() { stopTimer(); tabs.clearTabs(); setLookAndFeel (nullptr); }
void AudioPluginAudioProcessorEditor::addControl (int page, const juce::String& id, const juce::String& name, int col, int row,
                                                 const juce::String& mode, const juce::StringArray& variants, bool choice, bool toggle)
{
    auto control = std::make_unique<ParameterControl> (processor, id, name, mode, variants, choice, toggle);
    control->setBounds (12 + col * 284, 10 + row * 160, 278, 156); panels[static_cast<size_t> (page)].addAndMakeVisible (*control); controls.push_back (std::move (control));
}
void AudioPluginAudioProcessorEditor::resized()
{
    waveform.setBounds (8, 60, getWidth() - 16, 160); tabs.setBounds (8, 225, getWidth() - 16, 700); status.setBounds (24, 930, getWidth() - 48, 24);
    envelope.setBounds (12, 6, 1130, 230);
    for (size_t i = 0; i < 5; ++i) notes[i].setBounds (20, i == 4 ? 18 : i == 2 ? 246 : 648, 1110, i == 4 ? 42 : 20);
    // Keep envelope's graph instruction clear of its Beat-shaping toggle.
    notes[2].setBounds (20, 244, 820, 50);
    notes[3].setBounds (20, 648, 1110, 20);
    for (size_t i = 0; i < 16; ++i) { const int x = 20 + static_cast<int> (i) * 70; stepLabels[i].setBounds (x, 492, 64, 22); steps[i].setBounds (x, 515, 64, 126); }
    for (size_t i = 0; i < 8; ++i)
    {
        const int y = 70 + static_cast<int> (i) * 66;
        routeSources[i].setBounds (24, y, 210, 32); routeTargets[i].setBounds (254, y, 405, 32); routeDepths[i].setBounds (682, y, 430, 32);
    }
}
void AudioPluginAudioProcessorEditor::timerCallback()
{
    status.setText (juce::String (processor.displayBpm.load(), 1) + (processor.usingHostTempo.load() ? " BPM / HOST" : " BPM / FALLBACK")
        + "     |     LFO 1 " + juce::String (processor.displayMods[0].load(), 2) + "    LFO 2 " + juce::String (processor.displayMods[1].load(), 2)
        + "    Follower " + juce::String (processor.displayMods[2].load(), 2) + "    Seq " + juce::String (processor.displayMods[3].load(), 2)
        + "    Random " + juce::String (processor.displayMods[4].load(), 2), juce::dontSendNotification);
    waveform.repaint(); envelope.repaint();
}
void AudioPluginAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (background); g.setColour (ink); g.setFont (juce::FontOptions (38.0f, juce::Font::bold)); g.drawText ("rice", 26, 8, 130, 48, juce::Justification::centredLeft);
    g.setColour (accent); g.setFont (juce::FontOptions (13.0f)); g.drawText ("CAPTURE / SHAPE / SCATTER", 155, 19, 450, 26, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff9ca89c)); g.drawText ("Grain dots: position + envelope level     |     Vertical marks: captures", 620, 19, 530, 26, juce::Justification::centredRight);
}
