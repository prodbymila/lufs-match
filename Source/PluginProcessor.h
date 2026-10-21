#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "LoudnessEngine.h"

class LufsMatchProcessor : public juce::AudioProcessor
{
public:
    enum Role { match = 0, tap = 1 };

    LufsMatchProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "LUFS Match"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState params;
    LoudnessEngine engine;

    // UI status, read from the message thread.
    std::atomic<bool> tapLinked { false }, tapConflict { false };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    float value (const char* id) const { return params.getRawParameterValue (id)->load(); }

    juce::AudioBuffer<float> preBuffer;
    uint64_t lastSeen = 0;
    bool wasPlaying = false, wasHolding = false;
    float heldBase = 0.0f;
    const uint64_t instanceId = (uint64_t) juce::Random::getSystemRandom().nextInt64();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LufsMatchProcessor)
};
