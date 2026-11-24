#include "VisualComponents.h"
namespace
{
const juce::Colour panel (0xff222925), ink (0xffeee9db), accent (0xffc3df89), muted (0xff9ca89c);
}
ParameterControl::ParameterControl (AudioPluginAudioProcessor& p, const juce::String& parameterId, const juce::String& title,
                                    const juce::String& modeId, const juce::StringArray& alternatives, bool choice, bool toggle)
    : processor (p), id (parameterId), variants (alternatives), isChoice (choice), isToggle (toggle)
{
    label.setText (title, juce::dontSendNotification); label.setColour (juce::Label::textColourId, ink);
    label.setJustificationType (juce::Justification::centred); label.setFont (juce::FontOptions (13.0f)); addAndMakeVisible (label);
    setComponentID (parameterId + "Control");
    auto& state = processor.parameters;
    if (isToggle)
    {
        label.setVisible (false);
        button.setButtonText (title); button.setComponentID (parameterId); button.setName (title); addAndMakeVisible (button);
        buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, parameterId, button);
    }
    else if (isChoice)
    {
        if (const auto* param = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (parameterId))) selection.addItemList (param->choices, 1);
        selection.setComponentID (parameterId); selection.setName (title); addAndMakeVisible (selection);
        choiceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, parameterId, selection);
    }
    else
    {
        knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag); knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 170, 24);
        knob.setComponentID (parameterId); knob.setName (title); addAndMakeVisible (knob);
        if (modeId.isNotEmpty())
        {
            if (const auto* param = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (modeId))) mode.addItemList (param->choices, 1);
            mode.setName (title + " units"); mode.setComponentID (modeId); addAndMakeVisible (mode);
            modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, modeId, mode);
            mode.onChange = [this] { rebind(); };
        }
        rebind();
    }
}
void ParameterControl::rebind()
{
    knobAttachment.reset();
    const auto index = mode.getSelectedItemIndex();
    const auto target = ! variants.isEmpty() && index >= 0 && index < variants.size() ? variants[index] : id;
    knobAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.parameters, target, knob);
}
void ParameterControl::paint (juce::Graphics& g) { g.setColour (panel); g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (4), 10.0f); }
void ParameterControl::resized()
{
    label.setBounds (8, 5, getWidth() - 16, 18);
    if (isToggle) { button.setBounds (20, getHeight() / 2 - 15, getWidth() - 40, 32); return; }
    if (isChoice) { selection.setBounds (18, getHeight() / 2, getWidth() - 36, 28); return; }
    const auto hasMode = mode.getNumItems() > 0;
    knob.setBounds ((getWidth() - 190) / 2, 24, 190, 96);
    if (hasMode) mode.setBounds (18, getHeight() - 34, getWidth() - 36, 26);
}
EnvelopeEditor::EnvelopeEditor (AudioPluginAudioProcessor& p) : processor (p) { setName ("AHDSR envelope editor: drag nodes for time and level; diamonds for curvature"); setComponentID ("envelopeEditor"); }
juce::Rectangle<float> EnvelopeEditor::plot() const { return getLocalBounds().toFloat().reduced (36.0f, 40.0f); }
std::array<juce::Point<float>, 6> EnvelopeEditor::points() const
{
    const auto env = processor.envelopeSettings(); const auto x = env.points (processor.grainDurationMs()); const auto r = plot();
    const std::array<float, 6> levels { 0, 1, 1, env.sustain, env.sustain, 0 };
    std::array<juce::Point<float>, 6> result;
    for (size_t i = 0; i < 6; ++i) result[i] = { r.getX() + static_cast<float> (x[i]) * r.getWidth(), r.getBottom() - levels[i] * r.getHeight() };
    if (result[2].getDistanceFrom (result[1]) < 12.0f) result[2].y -= 14.0f;
    if (result[4].getDistanceFrom (result[3]) < 12.0f) result[4].y += 14.0f;
    return result;
}
std::array<juce::Point<float>, 3> EnvelopeEditor::curves() const
{
    const auto env = processor.envelopeSettings(); const auto x = env.points (processor.grainDurationMs()); const auto r = plot();
    std::array<juce::Point<float>, 3> result;
    for (size_t i = 0; i < 3; ++i)
    {
        const auto phase = (x[i * 2] + x[i * 2 + 1]) * 0.5;
        result[i] = { r.getX() + static_cast<float> (phase) * r.getWidth(), r.getBottom() - env.value (phase, processor.grainDurationMs()) * r.getHeight() };
    }
    return result;
}
void EnvelopeEditor::paint (juce::Graphics& g)
{
    g.setColour (panel); g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (4), 12.0f);
    const auto r = plot(); const auto env = processor.envelopeSettings(); const auto duration = processor.grainDurationMs();
    g.setColour (juce::Colour (0xff3d473f));
    for (int i = 0; i <= 4; ++i) { const auto y = r.getY() + r.getHeight() * static_cast<float> (i) / 4.0f; g.drawHorizontalLine (static_cast<int> (y), r.getX(), r.getRight()); }
    juce::Path line; line.startNewSubPath (r.getX(), r.getBottom());
    for (int i = 0; i <= 512; ++i)
    {
        const double phase = static_cast<double> (i) / 512.0;
        line.lineTo (r.getX() + static_cast<float> (phase) * r.getWidth(), r.getBottom() - env.value (phase, duration) * r.getHeight());
    }
    auto fill = line; fill.lineTo (r.getRight(), r.getBottom()); fill.closeSubPath();
    g.setColour (accent.withAlpha (0.12f)); g.fillPath (fill); g.setColour (accent); g.strokePath (line, juce::PathStrokeType (2.0f));
    const auto nodes = points(); const std::array<const char*, 6> names { "", "A", "H", "D", "S", "R" };
    for (size_t i = 1; i < nodes.size(); ++i)
    {
        g.setColour (ink); g.fillEllipse (nodes[i].x - 5, nodes[i].y - 5, 10, 10);
        const int labelX = static_cast<int> (nodes[i].x) + (i == 1 ? -30 : i == 2 ? 8 : -12);
        g.setFont (juce::FontOptions (12.0f)); g.drawText (names[i], labelX, static_cast<int> (nodes[i].y) - 23, 24, 16, juce::Justification::centred);
    }
    for (const auto point : curves())
    {
        juce::Path diamond; diamond.startNewSubPath (point.x, point.y - 5); diamond.lineTo (point.x + 5, point.y); diamond.lineTo (point.x, point.y + 5); diamond.lineTo (point.x - 5, point.y); diamond.closeSubPath();
        g.setColour (accent); g.fillPath (diamond);
    }
    g.setColour (muted); g.setFont (juce::FontOptions (12.0f));
    g.drawText ("Drag points: time / sustain     Drag diamonds: curvature     Double-click diamond: linear", 20, getHeight() - 25, getWidth() - 40, 20, juce::Justification::centredLeft);
}
void EnvelopeEditor::set (const juce::String& id, float value)
{
    auto* parameter = processor.parameters.getParameter (id);
    const auto& range = parameter->getNormalisableRange();
    value = range.snapToLegalValue (juce::jlimit (range.start, range.end, value));
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value)); repaint();
}
void EnvelopeEditor::editPoint (int index, juce::Point<float> delta, const rice::Envelope& env, double duration)
{
    const auto ms = delta.x / std::max (1.0f, plot().getWidth()) * static_cast<float> (duration);
    if (index == 1) set ("envAttack", static_cast<float> (env.attack) + ms);
    if (index == 2) set ("envHold", static_cast<float> (env.hold) + ms);
    if (index == 3) set ("envDecay", static_cast<float> (env.decay) + ms);
    if (index == 4) set ("envRelease", static_cast<float> (env.release) - ms);
    if (index == 5) set ("envRelease", static_cast<float> (env.release) + ms);
    if (index == 3 || index == 4) set ("envSustain", env.sustain * 100.0f - delta.y / std::max (1.0f, plot().getHeight()) * 100.0f);
}
void EnvelopeEditor::editCurve (int index, float dy, const rice::Envelope& env)
{
    const std::array<const char*, 3> ids { "envAttackCurve", "envDecayCurve", "envReleaseCurve" };
    const std::array<float, 3> values { env.attackCurve, env.decayCurve, env.releaseCurve };
    if (index >= 0 && index < 3) set (ids[static_cast<size_t> (index)], values[static_cast<size_t> (index)] - dy / 80.0f);
}
void EnvelopeEditor::mouseDown (const juce::MouseEvent& e)
{
    selectedPoint = selectedCurve = -1; gestures.clear(); const auto nodes = points();
    float best = 14.0f;
    for (size_t i = 1; i < nodes.size(); ++i) if (nodes[i].getDistanceFrom (e.position) < best) { best = nodes[i].getDistanceFrom (e.position); selectedPoint = static_cast<int> (i); }
    if (selectedPoint < 0)
    {
        const auto handles = curves();
        for (size_t i = 0; i < handles.size(); ++i) if (handles[i].getDistanceFrom (e.position) < best) { best = handles[i].getDistanceFrom (e.position); selectedCurve = static_cast<int> (i); }
    }
    if (selectedPoint == 1) gestures.add ("envAttack");
    if (selectedPoint == 2) gestures.add ("envHold");
    if (selectedPoint == 3) { gestures.add ("envDecay"); gestures.add ("envSustain"); }
    if (selectedPoint == 4) { gestures.add ("envRelease"); gestures.add ("envSustain"); }
    if (selectedPoint == 5) gestures.add ("envRelease");
    if (selectedCurve >= 0) gestures.add (juce::StringArray { "envAttackCurve", "envDecayCurve", "envReleaseCurve" }[selectedCurve]);
    for (const auto& id : gestures) processor.parameters.getParameter (id)->beginChangeGesture();
    dragStart = e.position; initial = processor.envelopeSettings(); initialDuration = processor.grainDurationMs();
}
void EnvelopeEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (selectedPoint >= 0) editPoint (selectedPoint, e.position - dragStart, initial, initialDuration);
    if (selectedCurve >= 0) editCurve (selectedCurve, e.position.y - dragStart.y, initial);
}
void EnvelopeEditor::mouseUp (const juce::MouseEvent&)
{ for (const auto& id : gestures) processor.parameters.getParameter (id)->endChangeGesture(); gestures.clear(); selectedPoint = selectedCurve = -1; }
void EnvelopeEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto handles = curves();
    for (size_t i = 0; i < handles.size(); ++i) if (handles[i].getDistanceFrom (e.position) < 14.0f)
    {
        const auto id = juce::StringArray { "envAttackCurve", "envDecayCurve", "envReleaseCurve" }[static_cast<int> (i)];
        auto* parameter = processor.parameters.getParameter (id); parameter->beginChangeGesture(); set (id, 0.0f); parameter->endChangeGesture(); break;
    }
}
void BufferDisplay::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (12.0f); g.setColour (panel); g.fillRoundedRectangle (r, 10.0f);
    auto area = r.reduced (15.0f, 30.0f); const auto middle = area.getCentreY();
    g.setColour (accent.withAlpha (0.5f));
    for (size_t i = 0; i < rice::VisualState::bins; ++i)
    {
        const auto x = area.getX() + static_cast<float> (i) / rice::VisualState::bins * area.getWidth();
        const auto peak = std::min (1.0f, processor.visual.waveform[i].load()) * area.getHeight() * 0.45f;
        g.drawVerticalLine (static_cast<int> (x), middle - peak, middle + peak);
    }
    for (size_t i = 0; i < 16; ++i)
    {
        const auto position = processor.visual.captures[i].load(); if (position < 0.0f || position > 1.0f) continue;
        const auto x = area.getX() + position * area.getWidth();
        g.setColour (muted.withAlpha (0.3f)); g.drawVerticalLine (static_cast<int> (x), area.getY(), area.getBottom());
    }
    for (size_t i = 0; i < rice::VisualState::dots; ++i)
    {
        const auto gain = processor.visual.gains[i].load(); if (gain <= 0.001f) continue;
        const auto x = area.getX() + juce::jlimit (0.0f, 1.0f, processor.visual.positions[i].load()) * area.getWidth();
        const auto layer = processor.visual.layers[i].load(); const auto y = middle + (layer / 15.0f - 0.5f) * area.getHeight() * 0.7f;
        const auto radius = 2.0f + gain * 4.0f; g.setColour (juce::Colour::fromHSV (0.2f + layer * 0.025f, 0.45f, 0.95f, 0.8f)); g.fillEllipse (x - radius, y - radius, radius * 2, radius * 2);
    }
    g.setColour (ink); g.drawVerticalLine (static_cast<int> (area.getRight()), area.getY(), area.getBottom());
    g.setFont (juce::FontOptions (12.0f));
    const auto frozen = processor.parameters.getRawParameterValue ("freeze")->load() > 0.5f;
    const auto state = ! processor.displayPlaying.load() ? (frozen ? "STOPPED / FROZEN" : "STOPPED") : frozen ? "FROZEN" : "RECORDING";
    g.drawText (juce::String (state) + "   /   " + juce::String (processor.visual.available.load(), 2) + " s   /   "
                + juce::String (processor.visual.active.load()) + " active grains", r.toNearestInt().withHeight (26).reduced (14, 0), juce::Justification::centredLeft);
    if (processor.visual.dropped.load() > 0)
        g.drawText ("Overlap limit reached: reduce density or size", r.toNearestInt().withHeight (26).reduced (14, 0), juce::Justification::centredRight);
}
