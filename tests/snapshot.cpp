// Renders the editor to PNG after running audio through a Tap -> +6 dB -> Match chain.
// Usage: LMSnapshot <out-match.png> <out-tap.png>
#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace
{
void save (juce::Component& c, const juce::File& file)
{
    const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat().writeImageToStream (image, out);
    std::printf ("wrote %s\n", file.getFullPathName().toRawUTF8());
}
}

int main (int argc, char* argv[])
{
    if (argc < 3)
        return 2;
    juce::ScopedJuceInitialiser_GUI juce;
    LufsMatchProcessor tap, match;
    tap.params.getParameter ("role")->setValueNotifyingHost (1.0f);
    for (auto* p : { &tap, &match })
        p->prepareToPlay (44100, 512);

    juce::Random rng (3);
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    for (int b = 0; b < 44100 * 8 / 512; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                buf.setSample (ch, i, (rng.nextFloat() * 2 - 1) * 0.2f);
        tap.processBlock (buf, midi);
        buf.applyGain (2.0f);
        match.processBlock (buf, midi);
    }

    std::unique_ptr<juce::AudioProcessorEditor> matchEditor (match.createEditor()), tapEditor (tap.createEditor());
    save (*matchEditor, juce::File (argv[1]));
    save (*tapEditor, juce::File (argv[2]));
    return 0;
}
