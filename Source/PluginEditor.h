#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

// Same look as the LUFS Match Max for Live device: a 184-unit main face plus an advanced strip.
// Everything is drawn in 336 x 169 design units.
namespace ui
{
constexpr float scale = 2.0f;
const juce::Colour bg { 0xff333333 }, inner { 0xff2c2c2c }, innerEdge { 0xff292929 }, ctrl { 0xff454545 },
    label { 0xe6b5b5b3 }, dim { 0x99ffffff }, yellow { 0xffdee28e }, green { 0xff8ceb7d }, salmon { 0xffe69588 },
    teal { 0xffabd8d5 }, off { 0xff808080 }, track { 0xff474747 }, fill { 0xff7ac86e };

juce::Font font (float size, bool bold = false);
juce::Rectangle<float> r (float x, float y, float w, float h);
}

// Base for controls bound to one plugin parameter.
class ParamControl : public juce::Component
{
public:
    explicit ParamControl (juce::RangedAudioParameter& p);

protected:
    juce::RangedAudioParameter& param;
    float value = 0.0f;   // denormalised
    juce::ParameterAttachment attachment;
};

class IconToggle : public ParamControl
{
public:
    enum Kind { power, hold };
    IconToggle (juce::RangedAudioParameter& p, Kind k) : ParamControl (p), kind (k) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    Kind kind;
};

class SensitivityBar : public ParamControl
{
public:
    using ParamControl::ParamControl;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    float valueAt (float x) const;
};

class Segmented : public ParamControl
{
public:
    using ParamControl::ParamControl;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
};

class Dial : public ParamControl
{
public:
    using ParamControl::ParamControl;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    float startNorm = 0.0f;
};

class NumberBox : public Dial
{
public:
    using Dial::Dial;
    void paint (juce::Graphics&) override;
};

class LufsMatchEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LufsMatchEditor (LufsMatchProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    juce::RangedAudioParameter& p (const char* id) { return *proc.params.getParameter (id); }
    void place (juce::Component& c, float x, float y, float w, float h);
    juce::String status() const;
    bool isTap() const { return (int) proc.params.getRawParameterValue ("role")->load() == LufsMatchProcessor::tap; }

    LufsMatchProcessor& proc;
    IconToggle power, hold;
    SensitivityBar sensitivity;
    NumberBox offset;
    Segmented mode, window, role;
    Dial target, maxGain, gate;
    juce::TextButton reset { "Reset" };
    juce::ComboBox link;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> linkAttachment;   // after items exist

    float pre = -120, post = -120, gain = 0;
    int flags = 0;
    bool tapRole = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LufsMatchEditor)
};
