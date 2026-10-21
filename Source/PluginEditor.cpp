#include "PluginEditor.h"

using juce::Graphics;
using juce::MouseEvent;

juce::Font ui::font (float size, bool bold)
{
    return juce::Font (juce::FontOptions (size * scale).withStyle (bold ? "Bold" : "Regular"));
}

juce::Rectangle<float> ui::r (float x, float y, float w, float h) { return { x * scale, y * scale, w * scale, h * scale }; }

ParamControl::ParamControl (juce::RangedAudioParameter& p)
    : param (p), attachment (p, [this] (float v) { value = v; repaint(); })
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

//==============================================================================
void IconToggle::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (value > 0.5f ? ui::teal : ui::off);
    const float lw = 2.5f * ui::scale;
    if (kind == power)
    {
        const float rad = std::min (b.getWidth(), b.getHeight()) * 0.36f, gap = 0.62f;
        const auto c = b.getCentre().translated (0, ui::scale);
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, rad, rad, 0, gap, juce::MathConstants<float>::twoPi - gap, true);
        g.strokePath (arc, juce::PathStrokeType (lw * 0.95f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const float d = 2.6f * ui::scale;
        g.fillEllipse (c.x - d, c.y - rad - d, 2 * d, 2 * d);
    }
    else
    {
        const float top = 3 * ui::scale, bottom = b.getHeight() - 3 * ui::scale;
        const std::pair<float, float> bars[] = { { 0.2f, top }, { 0.5f, top + (bottom - top) * 0.3f }, { 0.8f, top } };
        for (auto [fx, y0] : bars)
        {
            juce::Path bar;
            bar.startNewSubPath (b.getWidth() * fx, y0);
            bar.lineTo (b.getWidth() * fx, bottom);
            g.strokePath (bar, juce::PathStrokeType (lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }
}

void IconToggle::mouseDown (const MouseEvent&) { attachment.setValueAsCompleteGesture (value > 0.5f ? 0.0f : 1.0f); }

//==============================================================================
void SensitivityBar::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float lw = b.getHeight() * 0.34f, y = b.getCentreY(), x0 = lw / 2 + 1, x1 = b.getWidth() - lw / 2 - 1;
    auto stroke = [&] (float to, juce::Colour c) {
        juce::Path line;
        line.startNewSubPath (x0, y);
        line.lineTo (to, y);
        g.setColour (c);
        g.strokePath (line, juce::PathStrokeType (lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    stroke (x1, ui::track);
    if (value > 0)
        stroke (x0 + (x1 - x0) * value / 100.0f, ui::fill);
}

float SensitivityBar::valueAt (float x) const
{
    const float lw = (float) getHeight() * 0.34f, x0 = lw / 2 + 1, x1 = (float) getWidth() - lw / 2 - 1;
    return std::round (juce::jlimit (0.0f, 1.0f, (x - x0) / (x1 - x0)) * 100.0f);
}

void SensitivityBar::mouseDown (const MouseEvent& e)
{
    attachment.beginGesture();
    attachment.setValueAsPartOfGesture (valueAt (e.position.x));
}

void SensitivityBar::mouseDrag (const MouseEvent& e) { attachment.setValueAsPartOfGesture (valueAt (e.position.x)); }
void SensitivityBar::mouseUp (const MouseEvent&) { attachment.endGesture(); }

//==============================================================================
void Segmented::paint (Graphics& g)
{
    const auto choices = dynamic_cast<juce::AudioParameterChoice&> (param).choices;
    const float w = (float) getWidth() / (float) choices.size(), pad = 2 * ui::scale;
    for (int i = 0; i < choices.size(); ++i)
    {
        const auto cell = juce::Rectangle<float> (w * (float) i, 0, w, (float) getHeight()).reduced (pad * 0.5f, 0);
        const bool on = (int) value == i;
        g.setColour (on ? ui::green : ui::ctrl);
        g.fillRect (cell);
        g.setColour (on ? juce::Colour (0xff1a1a1a) : juce::Colour (0xffc7c7c7));
        g.setFont (ui::font (9));
        g.drawText (choices[i], cell, juce::Justification::centred);
    }
}

void Segmented::mouseDown (const MouseEvent& e)
{
    const int n = dynamic_cast<juce::AudioParameterChoice&> (param).choices.size();
    attachment.setValueAsCompleteGesture ((float) juce::jlimit (0, n - 1, (int) (e.position.x * n / (float) getWidth())));
}

//==============================================================================
void Dial::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setFont (ui::font (8.5f));
    g.setColour (juce::Colour (0xffb8b8b8));
    g.drawText (param.getName (32), b.withHeight (11 * ui::scale), juce::Justification::centred);
    g.drawText (param.getCurrentValueAsText() + " " + param.getLabel(), b.withTop (b.getBottom() - 11 * ui::scale),
                juce::Justification::centred);

    const auto knob = b.withTrimmedTop (11 * ui::scale).withTrimmedBottom (11 * ui::scale);
    const float rad = std::min (knob.getWidth(), knob.getHeight()) * 0.42f;
    const auto c = knob.getCentre();
    const float a0 = juce::degreesToRadians (-135.0f), a1 = juce::degreesToRadians (135.0f);
    const float a = a0 + (a1 - a0) * param.convertTo0to1 (value);
    const juce::PathStrokeType stroke (3.0f * ui::scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    juce::Path trackArc, valueArc, needle;
    trackArc.addCentredArc (c.x, c.y, rad, rad, 0, a0, a1, true);
    valueArc.addCentredArc (c.x, c.y, rad, rad, 0, a0, a, true);
    g.setColour (juce::Colour (0xff525252));
    g.strokePath (trackArc, stroke);
    g.setColour (isEnabled() ? ui::green : ui::off);
    g.strokePath (valueArc, stroke);
    needle.startNewSubPath (c);
    needle.lineTo (c.getPointOnCircumference (rad * 0.85f, a));
    g.setColour (juce::Colour (0xffd9d9d9));
    g.strokePath (needle, juce::PathStrokeType (1.5f * ui::scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void Dial::mouseDown (const MouseEvent&)
{
    startNorm = param.convertTo0to1 (value);
    attachment.beginGesture();
}

void Dial::mouseDrag (const MouseEvent& e)
{
    const float sens = e.mods.isShiftDown() ? 1500.0f : 300.0f;
    const float norm = juce::jlimit (0.0f, 1.0f, startNorm - (float) e.getDistanceFromDragStartY() / sens);
    attachment.setValueAsPartOfGesture (param.convertFrom0to1 (norm));
}

void Dial::mouseUp (const MouseEvent&) { attachment.endGesture(); }

void Dial::mouseDoubleClick (const MouseEvent&)
{
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
}

void NumberBox::paint (Graphics& g)
{
    g.setColour (juce::Colour (0xff999997));
    g.setFont (ui::font (10));
    g.drawText (param.getCurrentValueAsText() + " " + param.getLabel(), getLocalBounds(), juce::Justification::centredLeft);
}

//==============================================================================
LufsMatchEditor::LufsMatchEditor (LufsMatchProcessor& p_)
    : AudioProcessorEditor (p_), proc (p_),
      power (p ("active"), IconToggle::power), hold (p ("hold"), IconToggle::hold), sensitivity (p ("sensitivity")),
      offset (p ("offset")), mode (p ("mode")), window (p ("window")), role (p ("role")), target (p ("target")),
      maxGain (p ("maxgain")), gate (p ("gate"))
{
    for (int i = 1; i <= 16; ++i)
        link.addItem ("Link " + juce::String (i), i);
    linkAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.params, "link", link);
    link.setColour (juce::ComboBox::backgroundColourId, ui::ctrl);
    link.setColour (juce::ComboBox::textColourId, juce::Colour (0xffd9d9d9));
    link.setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    link.setColour (juce::ComboBox::arrowColourId, juce::Colour (0xff999999));

    reset.setColour (juce::TextButton::buttonColourId, ui::ctrl);
    reset.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffc7c7c7));
    reset.onClick = [this] { proc.engine.requestReset(); };

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &power, &hold, &sensitivity, &offset, &mode, &window, &role, &target, &maxGain, &gate, &reset, &link })
        addAndMakeVisible (c);

    setSize ((int) (336 * ui::scale), (int) (169 * ui::scale));
    startTimerHz (20);
    timerCallback();
}

void LufsMatchEditor::place (juce::Component& c, float x, float y, float w, float h) { c.setBounds (ui::r (x, y, w, h).toNearestInt()); }

void LufsMatchEditor::resized()
{
    place (power, 3, 127, 31, 24);
    place (sensitivity, 42.6f, 134.5f, 101, 18);
    place (hold, 154, 128, 25, 22);
    place (offset, 93, 113, 64, 15);

    const float x = 190;
    place (mode, x, 5, 68, 15);
    place (window, x + 72, 5, 68, 15);
    place (target, x - 2, 24, 46, 48);
    place (maxGain, x + 46, 24, 46, 48);
    place (gate, x + 94, 24, 46, 48);
    place (reset, x, 76, 68, 15);
    place (role, x, 110, 90, 16);
    place (link, x + 94, 110, 46, 16);
}

void LufsMatchEditor::timerCallback()
{
    pre = proc.engine.preLufs.load();
    post = proc.engine.outLufs.load();
    gain = proc.engine.gainDb.load();
    flags = proc.engine.flags.load();

    const bool tapNow = isTap();
    if (tapNow != tapRole)
    {
        tapRole = tapNow;
        for (juce::Component* c : std::initializer_list<juce::Component*> {
                 &power, &hold, &sensitivity, &offset, &mode, &window, &target, &maxGain, &gate, &reset })
            c->setVisible (! tapRole);
    }
    target.setEnabled ((int) proc.params.getRawParameterValue ("mode")->load() == 1);
    repaint();
}

juce::String LufsMatchEditor::status() const
{
    const int n = (int) proc.params.getRawParameterValue ("link")->load() + 1;
    const bool listenB = proc.params.getRawParameterValue ("active")->load() < 0.5f;
    const bool holding = proc.params.getRawParameterValue ("hold")->load() > 0.5f;
    const bool targetMode = (int) proc.params.getRawParameterValue ("mode")->load() == 1;
    if (proc.tapConflict.load())
        return "Two Taps on link " + juce::String (n);
    if (tapRole)
        return "Sending to link " + juce::String (n);

    juce::String s;
    if (holding)
        s = "HOLD - gain frozen";
    else if (! targetMode && ! proc.tapLinked.load())
        s = "No Tap on link " + juce::String (n);
    else if (! targetMode && (flags & LoudnessEngine::postPresent) && ! (flags & LoudnessEngine::prePresent))
        s = "Tap is silent";
    else if (flags & LoudnessEngine::adapting)
        s = targetMode ? "Matching target" : "Matching Pre FX";
    else
        s = "Waiting for signal above gate";
    return listenB ? "B: ORIGINAL | " + s : s;
}

void LufsMatchEditor::paint (Graphics& g)
{
    using namespace ui;
    g.fillAll (bg);
    auto text = [&] (const juce::String& t, float x, float y, float w, float h, float size, juce::Colour c,
                     juce::Justification j = juce::Justification::centred, bool bold = false) {
        g.setColour (c);
        g.setFont (font (size, bold));
        g.drawFittedText (t, r (x, y, w, h).toNearestInt(), j, 2);
    };
    auto fmt = [] (float v, int decimals) { return v > -70.0f ? juce::String (v, decimals) : juce::String ("--"); };

    text ("LM", 5, 9, 30, 26, 17, juce::Colour (0xff9e9e9e), juce::Justification::centredLeft, true);
    g.setColour (inner);
    g.fillRoundedRectangle (r (0, 50.5f, 184, 79.5f), 6 * scale);
    g.setColour (innerEdge);
    g.drawRoundedRectangle (r (0, 50.5f, 184, 79.5f), 6 * scale, scale);
    g.setColour (juce::Colour (0xff4d4d4d));
    g.fillRect (r (185, 6, 1, 157));

    if (tapRole)
    {
        text ("TAP", 42, 12, 101, 34, 24, yellow, juce::Justification::centred, true);
        text ("Put this first in the chain.\nPut LUFS Match (Role: Match)\nlast, on the same link.", 6, 58, 172, 66, 10.5f, label);
    }
    else
    {
        text ("Difference", 42, 3, 101, 18, 11, juce::Colour (0xffb5b5b3));
        text (juce::String (gain, 2), 30, 16, 88, 34, 24, yellow, juce::Justification::centredRight);
        text ("DB", 121, 29, 22, 16, 9.5f, label, juce::Justification::centredLeft);
        text ("LUFS", 72, 52, 40, 16, 10.4f, juce::Colour (0x99b5b5b3));
        text ("Pre", 6, 58, 76, 18, 13, label);
        text ("Post", 102, 58, 76, 18, 13, label);
        text (fmt (pre, 1), 0, 74, 88, 38, 30, green);
        text (fmt (post, 1), 96, 74, 88, 38, 30, (flags & LoudnessEngine::settled) ? green : salmon);
        g.setColour (juce::Colour (0xff4d4d4d));
        g.fillRect (r (91.5f, 81, 1.5f, 14.5f));
        text ("Gain Offset", 30, 112, 60, 16, 9.5f, dim);
        text ("Bypass", -1.5f, 151, 40, 16, 10, dim);
        text ("Sensitivity", 62, 150, 62, 18, 11, dim);
        text ("Hold", 151, 150.5f, 31, 18, 11, dim);
        g.setColour (juce::Colour (0xff6b6b6b));
        g.fillRect (r (38.6f, 159, 24.6f, 1.5f));
        g.fillRect (r (124.2f, 159, 24.6f, 1.5f));
    }

    text (status(), tapRole ? 190 : 262, tapRole ? 76 : 74, tapRole ? 140 : 72, 24, 8, dim, juce::Justification::centredLeft);
    text ("ROLE", 190, 97, 60, 12, 8, dim, juce::Justification::centredLeft);
    text ("LINK", 284, 97, 46, 12, 8, dim, juce::Justification::centredLeft);
    text ("One Tap + one Match per link.\nPlugins must not be bridged.", 190, 132, 142, 28, 7.5f, juce::Colour (0x66ffffff),
          juce::Justification::topLeft);
}
