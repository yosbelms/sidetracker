#include "MasterSpectrum.h"
#include <cr_section_macros.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

extern "C" const int16_t AudioWaveformSine[257];

namespace masterSpectrum
{
namespace
{
constexpr unsigned kFrames = 1024;
constexpr unsigned kDisabled = kFrames + 1;
constexpr unsigned kQuarter = kFrames / 4;

// Log-spaced band edges from the first FFT bin to 20 kHz at 44.1 kHz.
constexpr uint16_t edges[kPoints + 1] = {
    1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 4, 4, 5, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19,
    22, 25, 28, 32, 36, 41, 46, 53, 60, 68, 78, 88, 100, 114, 129, 147, 167, 190,
    216, 245, 279, 317, 360, 409, 465,
};

// The audio callback publishes one window and leaves it alone until the UI consumes it.
std::atomic<unsigned> captured{kDisabled};
__NOINIT(EXTERNAL_RAM) static int16_t samples[2][kFrames];
struct Complex
{
    float real, imag;
};
__NOINIT(EXTERNAL_RAM) static Complex bins[kFrames];

float sine(unsigned phase)
{
    phase &= kFrames - 1;
    const unsigned index = phase / 4;
    const float fraction = (phase % 4) * 0.25f;
    return (AudioWaveformSine[index] +
            fraction * (AudioWaveformSine[index + 1] - AudioWaveformSine[index])) / 32768.0f;
}

void transform()
{
    for (unsigned i = 0, reversed = 0; i < kFrames; ++i)
    {
        // Hann window with coherent gain correction for a full-scale sinusoid.
        const float window = (1 - sine(i + kQuarter)) / (32768.0f * kQuarter * 2);
        bins[reversed] = {samples[0][i] * window, samples[1][i] * window};

        unsigned bit = kFrames / 2;
        while (bit && (reversed & bit))
        {
            reversed ^= bit;
            bit >>= 1;
        }
        reversed ^= bit;
    }

    for (unsigned size = 2; size <= kFrames; size *= 2)
    {
        const unsigned half = size / 2;
        const unsigned stride = kFrames / size;
        for (unsigned i = 0; i < half; ++i)
        {
            const float cos = sine(i * stride + kQuarter);
            const float sin = -sine(i * stride);
            for (unsigned j = i; j < kFrames; j += size)
            {
                const auto a = bins[j];
                const auto b = bins[j + half];
                const float real = cos * b.real - sin * b.imag;
                const float imag = sin * b.real + cos * b.imag;
                bins[j] = {a.real + real, a.imag + imag};
                bins[j + half] = {a.real - real, a.imag - imag};
            }
        }
    }
}

float stereoPower(unsigned bin)
{
    // Left and right are packed into the real and imaginary FFT inputs. The
    // two conjugate bins cancel the cross terms, including for out-of-phase audio.
    const auto a = bins[bin];
    const auto b = bins[kFrames - bin];
    return 0.25f * (a.real * a.real + a.imag * a.imag + b.real * b.real + b.imag * b.imag);
}
}

void setEnabled(bool enabled)
{
    captured.store(enabled ? 0 : kDisabled, std::memory_order_relaxed);
}

void capture(const int16_t* left, const int16_t* right, unsigned frames)
{
    const unsigned offset = captured.load(std::memory_order_relaxed);
    if (offset >= kFrames) return;

    const unsigned count = std::min(frames, kFrames - offset);
    const auto bytes = count * sizeof(int16_t);
    if (left) std::memcpy(samples[0] + offset, left, bytes);
    else std::memset(samples[0] + offset, 0, bytes);
    if (right) std::memcpy(samples[1] + offset, right, bytes);
    else std::memset(samples[1] + offset, 0, bytes);
    captured.store(offset + count, std::memory_order_release);
}

bool read(uint8_t (&levels)[kPoints])
{
    if (captured.load(std::memory_order_acquire) != kFrames) return false;
    transform();
    captured.store(0, std::memory_order_release);

    bool changed = false;
    for (unsigned i = 0; i < kPoints; ++i)
    {
        float power = 0;
        const unsigned end = std::max<unsigned>(edges[i + 1], edges[i] + 1);
        for (unsigned bin = edges[i]; bin < end; ++bin)
            power = std::max(power, stereoPower(bin));

        constexpr float kFloorDb = -72;
        const float db = 10 * std::log10(std::max(power, 0.0000000630957f));
        const int target = std::clamp(static_cast<int>((db - kFloorDb) * (255 / -kFloorDb)), 0, 255);
        const auto next = std::max(target, levels[i] - 10);
        changed |= next != levels[i];
        levels[i] = next;
    }
    return changed;
}
}
