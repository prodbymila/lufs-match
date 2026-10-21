// Offline checks for LoudnessEngine. Build and run:
//   clang++ -std=c++17 -O2 -I../Source engine_test.cpp ../Source/LoudnessEngine.cpp -o engine_test && ./engine_test
#include "LoudnessEngine.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace
{
int failures = 0;
constexpr double pi = 3.14159265358979323846;   // M_PI is not standard (MSVC)

void check (const char* what, double got, double want, double tol)
{
    const bool ok = std::abs (got - want) <= tol;
    failures += ok ? 0 : 1;
    std::printf ("%s %-46s got %8.3f  want %8.3f\n", ok ? "PASS" : "FAIL", what, got, want);
}

// Runs `seconds` of audio through the engine in 512-sample blocks.
// make(i, ch) gives the pre signal; post = pre * postGain.
template <typename Make>
LoudnessEngine& run (LoudnessEngine& e, double sr, double seconds, double postGain, const LoudnessEngine::Settings& s,
                     Make make, double* outRms = nullptr, double* preRms = nullptr)
{
    const int bs = 512;
    std::vector<float> pl (bs), pr (bs), ql (bs), qr (bs);
    const float* pre[2] = { pl.data(), pr.data() };
    float* post[2] = { ql.data(), qr.data() };
    const long total = (long) (sr * seconds);
    double so = 0, sp = 0;
    long tail = 0;
    for (long start = 0; start < total; start += bs)
    {
        for (int i = 0; i < bs; ++i)
        {
            pl[i] = (float) make (start + i, 0);
            pr[i] = (float) make (start + i, 1);
            ql[i] = (float) (pl[i] * postGain);
            qr[i] = (float) (pr[i] * postGain);
        }
        e.process (post, pre, 2, bs, s);
        if (start > total / 2)   // measure the settled second half
            for (int i = 0; i < bs; ++i, ++tail)
            {
                so += ql[i] * ql[i];
                sp += pl[i] * pl[i];
            }
    }
    if (outRms) *outRms = std::sqrt (so / tail);
    if (preRms) *preRms = std::sqrt (sp / tail);
    return e;
}
}

int main()
{
    LoudnessEngine::Settings s;

    // EBU Tech 3341 case 1: stereo 1 kHz sine at -23 dBFS reads -23.0 LUFS at any sample rate.
    for (double sr : { 32000.0, 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        LoudnessEngine e;
        e.prepare (sr);
        const double a = std::pow (10.0, -23.0 / 20.0);
        run (e, sr, 5, 1.0, s, [&] (long i, int) { return a * std::sin (2 * pi * 1000 * i / sr); });
        char name[64];
        std::snprintf (name, sizeof name, "sine -23 dBFS short-term @ %.0f Hz", sr);
        check (name, e.preLufs.load(), -23.0, 0.1);
    }

    std::mt19937 rng (7);
    std::normal_distribution<double> noise (0.0, 0.1);
    auto pink = [&] (long, int) { return noise (rng); };

    {   // Match: post is 6.02 dB louder, the engine pulls it back to the pre loudness.
        LoudnessEngine e;
        e.prepare (48000);
        double out, in;
        run (e, 48000, 12, 2.0, s, pink, &out, &in);
        check ("match: applied gain (dB)", e.gainDb.load(), -6.02, 0.1);
        check ("match: output vs pre level (dB)", 20 * std::log10 (out / in), 0.0, 0.1);
    }
    {   // Target mode: output lands on the target loudness.
        LoudnessEngine e;
        e.prepare (44100);
        auto t = s;
        t.mode = 1;
        t.target = -18;
        run (e, 44100, 12, 2.0, t, pink);
        check ("target: output loudness (LUFS)", e.outLufs.load(), -18.0, 0.15);
    }
    {   // Integrated window with gating: a long silence must not move the result.
        LoudnessEngine e;
        e.prepare (48000);
        auto t = s;
        t.window = 2;
        run (e, 48000, 6, 1.0, t, pink);
        const double before = e.preLufs.load();
        run (e, 48000, 6, 1.0, t, [] (long, int) { return 0.0; });
        // Blocks straddling the cut legitimately count; an ungated mean would drop ~3 dB.
        check ("integrated: 6 s of silence moves it < 0.2 LU", e.preLufs.load(), before, 0.2);
    }
    {   // Hold keeps the stored gain plus trim.
        LoudnessEngine e;
        e.prepare (48000);
        auto t = s;
        t.hold = true;
        t.holdGain = 3;
        t.trim = -1;
        run (e, 48000, 4, 2.0, t, pink);
        check ("hold: applied gain = held + trim (dB)", e.gainDb.load(), 2.0, 0.05);
    }
    {   // Max Gain clamps a large correction.
        LoudnessEngine e;
        e.prepare (48000);
        auto t = s;
        t.range = 6;
        run (e, 48000, 8, 0.1, t, pink);   // post 20 dB down, still above the gate
        check ("range: correction clamped to +6 dB", e.gainDb.load(), 6.0, 0.05);
    }
    {   // No tap linked: no pre signal, gain stays put.
        LoudnessEngine e;
        e.prepare (48000);
        std::vector<float> l (512, 0.1f), r (512, 0.1f);
        float* post[2] = { l.data(), r.data() };
        for (int k = 0; k < 400; ++k)
            e.process (post, nullptr, 2, 512, s);
        check ("no tap: gain stays at 0 dB", e.gainDb.load(), 0.0, 1e-6);
    }

    std::printf (failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
