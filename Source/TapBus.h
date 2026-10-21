#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <juce_core/juce_core.h>

// In-process link between a "Tap" instance (first in the mixer chain) and a "Match" instance
// (last in the chain). The Tap copies the Pre-FX audio into a ring buffer; the Match reads the
// most recent block. Both run in the same host process, so a bridged plugin cannot link.
class TapBus
{
public:
    static constexpr int numLinks = 16;
    static constexpr int ringSize = 1 << 16;

    struct Slot
    {
        std::array<std::array<float, ringSize>, 2> data {};
        std::atomic<uint64_t> written { 0 };
        std::atomic<uint64_t> writer { 0 };
        std::atomic<uint32_t> lastWriteMs { 0 }, conflictMs { 0 };

        void write (const float* const* in, int numChannels, int n, uint64_t id)
        {
            const uint32_t now = juce::Time::getMillisecondCounter();
            if (writer.load() != id && now - lastWriteMs.load() < 200)
                conflictMs.store (now);   // two taps on one link
            writer.store (id);
            lastWriteMs.store (now);

            const uint64_t w = written.load (std::memory_order_relaxed);
            for (int ch = 0; ch < 2; ++ch)
            {
                const float* src = in[std::min (ch, numChannels - 1)];
                for (int i = 0; i < n; ++i)
                    data[(size_t) ch][(size_t) ((w + (uint64_t) i) % ringSize)] = src[i];
            }
            written.store (w + (uint64_t) n, std::memory_order_release);
        }

        // Copies the latest n samples. Returns false when the tap has not written since the last
        // read: a stopped or removed tap must not feed stale audio into the measurement.
        bool read (float* const* out, int n, uint64_t& lastSeen)
        {
            const uint64_t w = written.load (std::memory_order_acquire);
            const bool fresh = w != 0 && w != lastSeen;
            lastSeen = w;
            if (! fresh)
                return false;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                {
                    const int64_t pos = (int64_t) w - n + i;
                    out[ch][i] = pos < 0 ? 0.0f : data[(size_t) ch][(size_t) ((uint64_t) pos % ringSize)];
                }
            return true;
        }

        bool hasConflict() const { return juce::Time::getMillisecondCounter() - conflictMs.load() < 1000; }
    };

    static Slot& get (int link)
    {
        static std::array<Slot, numLinks> slots;
        return slots[(size_t) std::clamp (link, 0, numLinks - 1)];
    }
};
