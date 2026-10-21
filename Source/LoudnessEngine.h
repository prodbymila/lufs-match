#pragma once

#include <array>
#include <atomic>
#include <vector>

// ITU-R BS.1770-4 / EBU R128 loudness matcher.
// C++ port of the gen~ DSP in the "LUFS Match" Max for Live device (src/lufs_match.genexpr).
// The processed ("post") signal is gained so its loudness follows the Pre-FX ("pre") signal,
// or a fixed LUFS target. Measurement and gain are open loop: the gain never feeds back.
class LoudnessEngine
{
public:
    struct Settings
    {
        int mode = 0;            // 0 = match pre, 1 = match target
        int window = 1;          // 0 = momentary 400 ms, 1 = short-term 3 s, 2 = integrated
        float target = -18.0f;   // LUFS, target mode
        float trim = 0.0f;       // dB, applied to both A and B
        float responseMs = 500.0f;
        float range = 18.0f;     // max correction, +/- dB
        float gate = -50.0f;     // LUFS, no adaptation below this
        bool listenOriginal = false;
        bool hold = false;
        float holdGain = 0.0f;   // dB, base gain used while holding
    };

    enum Flags { adapting = 1, prePresent = 2, postPresent = 4, settled = 8 };

    void prepare (double sampleRate);
    void requestReset() { resetPending.store (true); }

    // post: processed signal, gained in place. pre: Pre-FX signal, or nullptr when no tap is linked.
    void process (float* const* post, const float* const* pre, int numChannels, int numSamples,
                  const Settings& s);

    // Read from any thread.
    std::atomic<float> preLufs { -120.0f }, postLufs { -120.0f }, outLufs { -120.0f };
    std::atomic<float> gainDb { 0.0f }, baseDb { 0.0f };
    std::atomic<int> flags { 0 };

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, s1 = 0, s2 = 0;

        double run (double x)
        {
            const double y = b0 * x + s1;
            s1 = b1 * x - a1 * y + s2;
            s2 = b2 * x - a2 * y;
            return y;
        }
    };

    struct KFilter
    {
        Biquad shelf, highpass;
        double run (double x) { return highpass.run (shelf.run (x)); }
    };

    static constexpr int numBlocks = 30;   // 30 x 100 ms = 3 s
    static constexpr int numBins = 1000;   // 0.1 LU bins, -70..+30 LUFS

    void clearMeasurement();
    void endBlock (const Settings& s);
    double gatedIntegrated (int histOffset) const;

    double sampleRate = 48000.0;
    int hop = 4800, count = 0, writeIndex = 0, filledBlocks = 0;
    std::array<KFilter, 4> kFilters;   // pre L, pre R, post L, post R
    double accPre = 0, accPost = 0;
    std::array<double, numBlocks> blocksPre {}, blocksPost {};
    std::vector<double> hist = std::vector<double> (numBins * 4, 0.0);   // count/sum pre, count/sum post

    double momPre = -120, momPost = -120, shortPre = -120, shortPost = -120;
    double intPre = -120, intPost = -120, selPre = -120, selPost = -120;
    double base = 0, preBase = 0, gain = 0, preGain = 0, crossfade = 0;
    int blockFlags = 0;
    std::atomic<bool> resetPending { false };
};
