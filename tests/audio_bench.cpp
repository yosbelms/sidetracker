// Measures CE's eight mono processing chains and dry/send mixers. Samples are
// injected at the player output. Resampling, envelopes, FX returns, sequencing,
// input capture and hardware DMA are outside this benchmark.
#include "audio_test_support.h"
#include "effect_bitDepth.h"
#include "filter_variable.h"
#include "amplifier.h"
#include "monoToStereo.h"
#include "mixer.h"
#include <cstdio>
#include <memory>
#include <vector>
#ifndef CE_BENCH_M4
#include <chrono>
#endif

namespace
{
using namespace ce::test;

class BenchSource : public AudioStream
{
  public:
    BenchSource() : AudioStream(0, nullptr) {}
    void update() override
    {
        if (!enabled) return;
        audio_block_t* block = allocate();
        if (!block) return;
        std::copy(samples.begin(), samples.end(), block->data);
        transmit(block);
        release(block);
    }
    Samples samples{};
    bool enabled = false;
};

struct Graph
{
    BenchSource source[8];
    AudioBitDepth crusher[8];
    AudioFilterStateVariable filter[8];
    AudioAmplifier amplifier[8];
    MonoToStereo pan[8];
    AudioMixer<8> leftMixer, rightMixer;
    AudioMixer<10> delayMixer, reverbMixer;
    BlockSink left, right, delay, reverb;
    std::vector<std::unique_ptr<AudioConnection>> connections;

    Graph()
    {
        for (int voice = 0; voice < 8; ++voice)
        {
            for (int sample = 0; sample < AUDIO_BLOCK_SAMPLES; ++sample)
                source[voice].samples[sample] = int16_t(((sample + voice * 11) % 128) * 128 - 8192);
            connect(&source[voice], 0, &crusher[voice], 0);
            connect(&crusher[voice], 0, &filter[voice], 0);
            connect(&filter[voice], 0, &amplifier[voice], 0);
            connect(&amplifier[voice], 0, &pan[voice], 0);
            connect(&pan[voice], 0, &leftMixer, voice);
            connect(&pan[voice], 1, &rightMixer, voice);
            connect(&filter[voice], 0, &delayMixer, voice);
            connect(&filter[voice], 0, &reverbMixer, voice);
            amplifier[voice].gain(0.25f);
            filter[voice].setCutoff(0.5f);
        }
        connect(&leftMixer, 0, &left, 0);
        connect(&rightMixer, 0, &right, 0);
        connect(&delayMixer, 0, &delay, 0);
        connect(&reverbMixer, 0, &reverb, 0);
    }

    void connect(AudioStream* from, int output, AudioStream* to, int input)
    {
        connections.emplace_back(new AudioConnection(from, output, to, input));
    }

    void configure(int scenario)
    {
        for (int voice = 0; voice < 8; ++voice)
        {
            source[voice].enabled = scenario != 0;
            crusher[voice].setBitDepth(scenario == 3 ? 8 : 16);
            filter[voice].setOverdriveAmount(scenario == 3 ? 20 : 0);
            filter[voice].setType(0);
            if (scenario >= 2) filter[voice].connect();
            else filter[voice].disconnect();
            delayMixer.gain(voice, 0.1f);
            reverbMixer.gain(voice, 0.1f);
        }
    }

    void changeFilters(int block)
    {
        for (auto& voice : filter)
        {
            voice.setType(uint8_t(block % 3));
            if (block % 2) voice.connect();
            else voice.disconnect();
        }
    }

    uint32_t checksum(uint32_t hash) const
    {
        for (const BlockSink* sink : {&left, &right, &delay, &reverb})
            for (int16_t sample : sink->samples)
            {
                hash ^= uint16_t(sample);
                hash *= 16777619u;
            }
        return hash;
    }
};

uint64_t stamp()
{
#ifdef CE_BENCH_M4
    // QEMU mps2-an386 CMSDK timer 0 runs at 25 MHz. With -icount shift=0,
    // each executed instruction advances virtual time by 1 ns (40 per tick).
    return *reinterpret_cast<volatile uint32_t*>(0x40000004);
#else
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}
}  // namespace

int main()
{
#ifdef CE_BENCH_M4
    *reinterpret_cast<volatile uint32_t*>(0x40000008) = 0xffffffffu;
    *reinterpret_cast<volatile uint32_t*>(0x40000000) = 1;
#endif
    AudioStream::initialize_memory(audioPool, std::size(audioPool));
    AudioFilterStateVariable::initCutoffLookup();
    static Graph graph;
    const char* names[] = {"idle", "dry8", "filter8", "drive8", "changes8"};
    constexpr int blocks = 512;
    for (int scenario = 0; scenario < 5; ++scenario)
    {
        graph.configure(scenario);
        for (int warmup = 0; warmup < 256; ++warmup) AudioStream::update_all();
        AudioMemoryUsageMaxReset();
        uint32_t hash = 2166136261u;
        uint64_t total = 0, maximum = 0;
        for (int block = 0; block < blocks; ++block)
        {
            const uint64_t before = stamp();
            if (scenario == 4) graph.changeFilters(block);
            AudioStream::update_all();
#ifdef CE_BENCH_M4
            const uint64_t elapsed = uint64_t(uint32_t(before - stamp())) * 40;
#else
            const uint64_t elapsed = stamp() - before;
#endif
            total += elapsed;
            maximum = std::max(maximum, elapsed);
            hash = graph.checksum(hash);  // Excluded from the measured interval.
            if (AudioMemoryUsage() != 0)
            {
                std::fprintf(stderr, "Unreleased audio blocks in %s\n", names[scenario]);
                return 1;
            }
        }
#ifdef CE_BENCH_M4
        const char* metric = "instructions";
#else
        const char* metric = "ns";
#endif
        std::printf("{\"scenario\":\"%s\",\"metric\":\"%s\",\"average\":%lu,\"maximum\":%lu,"
                    "\"checksum\":%lu,\"peak_blocks\":%u}\n", names[scenario], metric,
                    static_cast<unsigned long>(total / blocks), static_cast<unsigned long>(maximum),
                    static_cast<unsigned long>(hash), AudioMemoryUsageMax());
    }
    return 0;
}
