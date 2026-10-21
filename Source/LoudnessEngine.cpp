#include "LoudnessEngine.h"

#include <algorithm>
#include <cmath>

namespace
{
double lufs (double meanSquare) { return -0.691 + 10.0 * std::log10 (std::max (meanSquare, 1e-12)); }
double dbToGain (double db) { return std::pow (10.0, db / 20.0); }
}

void LoudnessEngine::prepare (double sr)
{
    sampleRate = sr;

    // BS.1770 pre-filter from its analog prototype, valid at any sample rate.
    const double pi = 3.14159265358979323846;
    const double K = std::tan (pi * 1681.974450955533 / sr);
    const double Vh = std::pow (10.0, 3.999843853973347 / 20.0);
    const double Vb = std::pow (Vh, 0.4996667741545416);
    const double Q1 = 0.7071752369554196;
    const double n0 = 1 + K / Q1 + K * K;
    const double K2 = std::tan (pi * 38.13547087602444 / sr);
    const double Q2 = 0.5003270373238773;
    const double m0 = 1 + K2 / Q2 + K2 * K2;

    for (auto& f : kFilters)
    {
        f.shelf = { (Vh + Vb * K / Q1 + K * K) / n0, 2 * (K * K - Vh) / n0, (Vh - Vb * K / Q1 + K * K) / n0,
                    2 * (K * K - 1) / n0, (1 - K / Q1 + K * K) / n0 };
        f.highpass = { 1, -2, 1, 2 * (K2 * K2 - 1) / m0, (1 - K2 / Q2 + K2 * K2) / m0 };
    }

    hop = (int) std::floor (sr * 0.1 + 0.5);
    clearMeasurement();
    gain = preGain = base = preBase = crossfade = 0;
}

void LoudnessEngine::clearMeasurement()
{
    blocksPre.fill (0);
    blocksPost.fill (0);
    std::fill (hist.begin(), hist.end(), 0.0);
    count = writeIndex = filledBlocks = 0;
    accPre = accPost = 0;
    momPre = momPost = shortPre = shortPost = intPre = intPost = -120;
}

void LoudnessEngine::process (float* const* post, const float* const* pre, int numChannels, int numSamples,
                              const Settings& s)
{
    // Reset clears the measurement only; the current gain is kept.
    if (resetPending.exchange (false))
        clearMeasurement();

    const bool stereo = numChannels > 1;
    const double coef = std::exp (-1000.0 / (std::max (s.responseMs, 1.0f) * sampleRate));
    const double xfCoef = std::exp (-1.0 / (0.015 * sampleRate));
    const double listen = s.listenOriginal ? 1.0 : 0.0;
    const double held = s.hold ? s.holdGain : base;
    const double targetGain = std::clamp (held + s.trim, (double) -s.range, (double) s.range);
    const double targetPreGain = s.mode == 1 ? std::clamp (preBase + s.trim, (double) -s.range, (double) s.range)
                                             : (double) s.trim;

    for (int i = 0; i < numSamples; ++i)
    {
        const double qL = post[0][i], qR = stereo ? post[1][i] : 0.0;
        const double pL = pre != nullptr ? pre[0][i] : 0.0;
        const double pR = pre != nullptr && stereo ? pre[1][i] : 0.0;

        const double kpl = kFilters[0].run (pL), kpr = kFilters[1].run (pR);
        const double kql = kFilters[2].run (qL), kqr = kFilters[3].run (qR);
        accPre += kpl * kpl + kpr * kpr;
        accPost += kql * kql + kqr * kqr;

        if (++count >= hop)
            endBlock (s);

        gain = targetGain + coef * (gain - targetGain);
        preGain = targetPreGain + coef * (preGain - targetPreGain);
        crossfade = listen + xfCoef * (crossfade - listen);
        const double gq = dbToGain (gain) * (1 - crossfade);
        const double gp = dbToGain (preGain) * crossfade;

        post[0][i] = (float) (qL * gq + pL * gp);
        if (stereo)
            post[1][i] = (float) (qR * gq + pR * gp);
    }

    const double heldNow = s.hold ? s.holdGain : base;
    const double settledTarget = std::clamp (heldNow + s.trim, (double) -s.range, (double) s.range);
    preLufs.store ((float) selPre);
    postLufs.store ((float) selPost);
    outLufs.store ((float) (s.listenOriginal ? selPre + preGain : selPost + gain));
    gainDb.store ((float) gain);
    baseDb.store ((float) base);
    flags.store (blockFlags | (std::abs (gain - settledTarget) < 0.3 ? settled : 0));
}

// Every 100 ms: momentary, short-term and integrated loudness, then the gain target.
void LoudnessEngine::endBlock (const Settings& s)
{
    blocksPre[(size_t) writeIndex] = accPre / hop;
    blocksPost[(size_t) writeIndex] = accPost / hop;
    writeIndex = (writeIndex + 1) % numBlocks;
    filledBlocks = std::min (filledBlocks + 1, numBlocks);
    accPre = accPost = 0;
    count = 0;

    double mP = 0, mQ = 0, sP = 0, sQ = 0;
    for (int i = 0; i < numBlocks; ++i)
    {
        const auto k = (size_t) ((writeIndex + 2 * numBlocks - 1 - i) % numBlocks);
        if (i < 4)
        {
            mP += blocksPre[k];
            mQ += blocksPost[k];
        }
        sP += blocksPre[k];
        sQ += blocksPost[k];
    }
    const int n4 = std::min (filledBlocks, 4);
    mP /= n4;
    mQ /= n4;
    momPre = lufs (mP);
    momPost = lufs (mQ);
    shortPre = lufs (sP / filledBlocks);
    shortPost = lufs (sQ / filledBlocks);

    // Integrated: 400 ms blocks with 75 % overlap, absolute gate -70 LUFS, relative gate -10 LU.
    if (filledBlocks >= 4)
    {
        auto add = [this] (double l, double ms, int offset) {
            if (l <= -70)
                return;
            const auto bin = (size_t) std::clamp ((int) std::floor ((l + 70) * 10), 0, numBins - 1);
            hist[(size_t) offset * numBins + bin] += 1;
            hist[(size_t) (offset + 1) * numBins + bin] += ms;
        };
        add (momPre, mP, 0);
        add (momPost, mQ, 2);
    }
    intPre = gatedIntegrated (0);
    intPost = gatedIntegrated (2);

    selPre = s.window == 0 ? momPre : s.window == 2 ? intPre : shortPre;
    selPost = s.window == 0 ? momPost : s.window == 2 ? intPost : shortPost;

    // Only adapt while both signals are above the gate, so tails and silence never pump the gain.
    const bool hasPre = momPre > s.gate, hasPost = momPost > s.gate;
    const bool adapt = (s.mode == 1 ? hasPost : hasPre && hasPost) && ! s.hold && selPost > -70;
    if (adapt)
        base = s.mode == 1 ? s.target - selPost : selPre - selPost;
    if (s.mode == 1 && hasPre && selPre > -70)
        preBase = s.target - selPre;
    blockFlags = (adapt ? adapting : 0) | (hasPre ? prePresent : 0) | (hasPost ? postPresent : 0);
}

double LoudnessEngine::gatedIntegrated (int offset) const
{
    const double* counts = hist.data() + (size_t) offset * numBins;
    const double* sums = counts + numBins;
    double tc = 0, ts = 0;
    for (int b = 0; b < numBins; ++b)
    {
        tc += counts[b];
        ts += sums[b];
    }
    if (tc <= 0)
        return -120;
    const double rel = lufs (ts / tc) - 10;
    const int first = std::clamp ((int) std::ceil ((rel + 70) * 10), 0, numBins - 1);
    double gc = 0, gs = 0;
    for (int b = first; b < numBins; ++b)
    {
        gc += counts[b];
        gs += sums[b];
    }
    return gc > 0 ? lufs (gs / gc) : -120;
}
