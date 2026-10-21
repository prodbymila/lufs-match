#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "TapBus.h"

LufsMatchProcessor::LufsMatchProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "LUFSMatch", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout LufsMatchProcessor::createLayout()
{
    using namespace juce;
    auto db = AudioParameterFloatAttributes().withLabel ("dB");
    auto lu = AudioParameterFloatAttributes().withLabel ("LUFS");
    auto version = [] (const char* id) { return ParameterID { id, 1 }; };
    StringArray links;
    for (int i = 1; i <= TapBus::numLinks; ++i)
        links.add (String (i));

    return {
        std::make_unique<AudioParameterChoice> (version ("role"), "Role", StringArray { "Match", "Tap" }, 0),
        std::make_unique<AudioParameterChoice> (version ("link"), "Link", links, 0),
        std::make_unique<AudioParameterBool> (version ("active"), "Bypass", true),
        std::make_unique<AudioParameterFloat> (version ("sensitivity"), "Sensitivity", NormalisableRange<float> (0, 100), 40.0f,
                                               AudioParameterFloatAttributes().withLabel ("%")),
        std::make_unique<AudioParameterBool> (version ("hold"), "Hold", false),
        std::make_unique<AudioParameterFloat> (version ("offset"), "Gain Offset", NormalisableRange<float> (-24, 24, 0.1f), 0.0f, db),
        std::make_unique<AudioParameterChoice> (version ("mode"), "Mode", StringArray { "Match", "Target" }, 0),
        std::make_unique<AudioParameterChoice> (version ("window"), "Window", StringArray { "M", "S", "I" }, 1),
        std::make_unique<AudioParameterFloat> (version ("target"), "Target", NormalisableRange<float> (-60, 0, 0.1f), -18.0f, lu),
        std::make_unique<AudioParameterFloat> (version ("maxgain"), "Max Gain", NormalisableRange<float> (0, 36, 0.1f), 18.0f, db),
        std::make_unique<AudioParameterFloat> (version ("gate"), "Gate", NormalisableRange<float> (-70, -20, 0.1f), -50.0f, lu),
    };
}

bool LufsMatchProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono())
           && layouts.getMainInputChannelSet() == out;
}

void LufsMatchProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    preBuffer.setSize (2, samplesPerBlock);
}

void LufsMatchProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples(), channels = buffer.getNumChannels();
    auto& slot = TapBus::get ((int) value ("link"));

    if ((int) value ("role") == tap)
    {
        slot.write (buffer.getArrayOfReadPointers(), channels, n, instanceId);
        return;   // audio passes untouched
    }

    // Transport start resets the measurement.
    if (auto* head = getPlayHead())
        if (auto pos = head->getPosition())
        {
            if (pos->getIsPlaying() && ! wasPlaying)
                engine.requestReset();
            wasPlaying = pos->getIsPlaying();
        }

    // Hold stores the base gain when engaged; it is saved with the project.
    const bool holding = value ("hold") > 0.5f;
    if (holding && ! wasHolding)
        heldBase = engine.baseDb.load();
    wasHolding = holding;

    if (preBuffer.getNumSamples() < n)
        preBuffer.setSize (2, n, false, false, true);
    const bool linked = slot.read (preBuffer.getArrayOfWritePointers(), n, lastSeen);
    tapLinked.store (linked);
    tapConflict.store (slot.hasConflict());

    LoudnessEngine::Settings s;
    s.mode = (int) value ("mode");
    s.window = (int) value ("window");
    s.target = value ("target");
    s.trim = value ("offset");
    s.responseMs = 5000.0f * std::exp (-0.055215f * value ("sensitivity"));
    s.range = value ("maxgain");
    s.gate = value ("gate");
    s.listenOriginal = value ("active") < 0.5f;
    s.hold = holding;
    s.holdGain = heldBase;

    engine.process (buffer.getArrayOfWritePointers(), linked ? preBuffer.getArrayOfReadPointers() : nullptr,
                    channels, n, s);
}

void LufsMatchProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = params.copyState();
    state.setProperty ("heldBase", heldBase, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void LufsMatchProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        heldBase = (float) state.getProperty ("heldBase", 0.0f);
        params.replaceState (state);
        // A recalled Hold keeps its saved gain instead of capturing a fresh one.
        wasHolding = value ("hold") > 0.5f;
    }
}

juce::AudioProcessorEditor* LufsMatchProcessor::createEditor() { return new LufsMatchEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LufsMatchProcessor(); }
