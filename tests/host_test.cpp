// Hosts the built plugin like a mixer chain: [LUFS Match: Tap] -> +6 dB "effect" -> [LUFS Match: Match].
// Usage: LMHostTest "<path to LUFS Match.vst3 or .component>"
#include <juce_audio_processors/juce_audio_processors.h>

#include "LoudnessEngine.h"

namespace
{
int failures = 0;

void check (const juce::String& what, double got, double want, double tol)
{
    const bool ok = std::abs (got - want) <= tol;
    failures += ok ? 0 : 1;
    std::printf ("%s %-44s got %8.3f  want %8.3f\n", ok ? "PASS" : "FAIL", what.toRawUTF8(), got, want);
}

// Hosted parameters only take normalised values.
void setParam (juce::AudioPluginInstance& p, const juce::String& name, float normalised)
{
    for (auto* param : p.getParameters())
        if (param->getName (64) == name)
        {
            param->setValueNotifyingHost (normalised);
            return;
        }
    std::printf ("missing parameter %s\n", name.toRawUTF8());
    ++failures;
}

struct Result { double gainDb, outLufs, preCorrelation; };

// Runs 12 s of noise through the chain and measures the second half.
Result runChain (juce::AudioPluginInstance& tap, juce::AudioPluginInstance& match, double sr, int bs, bool linkTap)
{
    juce::Random rng (7);
    juce::AudioBuffer<float> buf (2, bs), pre (2, bs);
    juce::MidiBuffer midi;
    LoudnessEngine meter;
    meter.prepare (sr);
    double so = 0, sp = 0, spo = 0;
    const int blocks = (int) (12 * sr / bs);
    for (int b = 0; b < blocks; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < bs; ++i)
                buf.setSample (ch, i, (rng.nextFloat() * 2 - 1) * 0.2f);
        pre.makeCopyOf (buf);
        if (linkTap)
            tap.processBlock (buf, midi);
        buf.applyGain (2.0f);   // the "effect": +6.02 dB
        match.processBlock (buf, midi);

        float* m[2] = { buf.getWritePointer (0), buf.getWritePointer (1) };
        juce::AudioBuffer<float> copy (buf);
        float* c[2] = { copy.getWritePointer (0), copy.getWritePointer (1) };
        meter.process (c, m, 2, bs, {});   // meter.preLufs reads the chain output
        if (b > blocks / 2)
            for (int i = 0; i < bs; ++i)
            {
                so += buf.getSample (0, i) * buf.getSample (0, i);
                sp += pre.getSample (0, i) * pre.getSample (0, i);
                spo += buf.getSample (0, i) * pre.getSample (0, i);
            }
    }
    return { 10 * std::log10 (so / sp), meter.preLufs.load(), spo / std::sqrt (so * sp) };
}
}

int main (int argc, char* argv[])
{
    if (argc < 2)
    {
        std::printf ("usage: LMHostTest <plugin path>\n");
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI juce;
    juce::AudioPluginFormatManager formats;
    juce::addDefaultFormatsToManager (formats);

    juce::OwnedArray<juce::PluginDescription> found;
    for (auto* f : formats.getFormats())
        f->findAllTypesForFile (found, argv[1]);
    if (found.isEmpty())
    {
        std::printf ("FAIL could not load %s\n", argv[1]);
        return 1;
    }
    std::printf ("loaded %s (%s)\n", found[0]->name.toRawUTF8(), found[0]->pluginFormatName.toRawUTF8());

    const double sr = 44100;
    const int bs = 512;
    juce::String err;
    auto tap = formats.createPluginInstance (*found[0], sr, bs, err);
    auto match = formats.createPluginInstance (*found[0], sr, bs, err);
    if (tap == nullptr || match == nullptr)
    {
        std::printf ("FAIL instance: %s\n", err.toRawUTF8());
        return 1;
    }
    for (auto* p : { tap.get(), match.get() })
    {
        p->enableAllBuses();
        p->prepareToPlay (sr, bs);
        setParam (*p, "Link", 3.0f / 15);   // link 4: avoid the default
    }
    setParam (*tap, "Role", 1);

    auto r = runChain (*tap, *match, sr, bs, true);
    check ("match: output vs pre level (dB)", r.gainDb, 0.0, 0.15);

    setParam (*match, "Bypass", 0);   // B: listen to the original
    r = runChain (*tap, *match, sr, bs, true);
    check ("B original: correlation with pre", r.preCorrelation, 1.0, 0.01);
    check ("B original: level vs pre (dB)", r.gainDb, 0.0, 0.05);
    setParam (*match, "Bypass", 1);

    setParam (*match, "Mode", 1);
    setParam (*match, "Target", (-18.0f + 60) / 60);
    r = runChain (*tap, *match, sr, bs, true);
    check ("target: output loudness (LUFS)", r.outLufs, -18.0, 0.2);
    setParam (*match, "Mode", 0);

    // Without a tap the gain holds (fresh instance so it starts from 0 dB).
    auto lone = formats.createPluginInstance (*found[0], sr, bs, err);
    lone->enableAllBuses();
    lone->prepareToPlay (sr, bs);
    setParam (*lone, "Link", 9.0f / 15);
    r = runChain (*tap, *lone, sr, bs, false);
    check ("no tap: effect gain passes through (dB)", r.gainDb, 6.02, 0.1);

    // State round trip keeps parameters.
    juce::MemoryBlock state;
    match->getStateInformation (state);
    auto clone = formats.createPluginInstance (*found[0], sr, bs, err);
    clone->setStateInformation (state.getData(), (int) state.getSize());
    for (auto* p : clone->getParameters())
        if (p->getName (64) == "Link")
            check ("state: link restored", p->getValue() * 15, 3, 0.01);

    for (auto* p : { tap.get(), match.get(), lone.get(), clone.get() })
        p->releaseResources();
    std::printf (failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
