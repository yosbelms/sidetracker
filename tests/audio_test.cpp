#include "audio_test_support.h"
#include "AudioStream.h"
#include "amplifier.h"
#include "dspinst.h"
#include "filter_variable.h"
#include "monoToStereo.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

namespace
{
using namespace ce::test;

// The send and sidechain taps hold references while the amplifier and panner
// run, exercising the same copy-on-write requirements as the firmware graph.
struct VoiceGraph
{
    BlockSource source;
    AudioFilterStateVariable filter;
    AudioAmplifier amplifier;
    MonoToStereo pan;
    BlockSink delay;
    BlockSink reverb;
    BlockSink sidechain;
    BlockSink left;
    BlockSink right;
    AudioConnection sourceToFilter{&source, &filter};
    AudioConnection filterToAmplifier{&filter, &amplifier};
    AudioConnection filterToDelay{&filter, &delay};
    AudioConnection filterToReverb{&filter, &reverb};
    AudioConnection amplifierToPan{&amplifier, &pan};
    AudioConnection amplifierToSidechain{&amplifier, &sidechain};
    AudioConnection panToLeft{&pan, 0, &left, 0};
    AudioConnection panToRight{&pan, 1, &right, 0};

    void render(const Samples& input)
    {
        source.push(input);
        AudioStream::update_all();
    }
};

struct SharedFilterGraph
{
    BlockSource source;
    AudioFilterStateVariable filter;
    BlockSink original, filtered;
    AudioConnection sourceToFilter{&source, &filter};
    AudioConnection sourceToOriginal{&source, &original};
    AudioConnection filterToOutput{&filter, &filtered};
};

// AudioStream's registry assumes firmware lifetime for its nodes. Keep test
// graphs alive until all tests finish so update_all never traverses dead nodes.
std::vector<std::unique_ptr<VoiceGraph>> graphs;

Samples signal()
{
    Samples samples;
    for (size_t i = 0; i < samples.size(); ++i)
        samples[i] = int16_t(int(i) * 97 - 6000);
    return samples;
}

class AudioGraphTest : public testing::Test
{
  protected:
    void SetUp() override
    {
        ASSERT_EQ(AudioMemoryUsage(), 0);
        // Avoid relying on zero-filled storage for filter control defaults.
        void* storage = ::operator new(sizeof(VoiceGraph));
        std::memset(storage, 0xa5, sizeof(VoiceGraph));
        graphs.emplace_back(new (storage) VoiceGraph);
        graph = graphs.back().get();
    }

    void TearDown() override
    {
        EXPECT_EQ(AudioMemoryUsage(), 0) << "Every rendered block must be released";
    }

    VoiceGraph* graph = nullptr;
};

TEST(DspInstructions, RoundedMultiplyMatchesArmSignedHighWord)
{
    EXPECT_EQ(multiply_32x32_rshift32_rounded(0x40000000, 0x40000000), 0x10000000);
    EXPECT_EQ(multiply_32x32_rshift32_rounded(-0x40000000, 0x40000000), -0x10000000);
    EXPECT_EQ(multiply_32x32_rshift32_rounded(INT32_MIN, INT32_MIN), 0x40000000);
    EXPECT_EQ(multiply_32x32_rshift32_rounded(INT32_MIN, INT32_MAX), -0x3fffffff);
    EXPECT_EQ(multiply_32x32_rshift32_rounded(1, INT32_MAX), 0);
    EXPECT_EQ(multiply_32x32_rshift32_rounded(1, INT32_MIN), 0);
    EXPECT_EQ(multiply_32x32_rshift32_rounded(2, INT32_MAX), 1);
    EXPECT_EQ(multiply_32x32_rshift32_rounded(-2, INT32_MAX), -1);
}

TEST(DspInstructions, SignedSaturationClampsBothLimits)
{
    EXPECT_EQ(signed_saturate_rshift(40000, 16, 0), INT16_MAX);
    EXPECT_EQ(signed_saturate_rshift(-40000, 16, 0), INT16_MIN);
    EXPECT_EQ(signed_saturate_rshift(123456, 16, 4), 7716);
    EXPECT_EQ(signed_saturate_rshift(-123456, 16, 4), -7716);
}

TEST_F(AudioGraphTest, DefaultFilterBypassesWithoutAllocatingAnOutputBlock)
{
    const Samples input = signal();
    graph->render(input);
    ASSERT_TRUE(graph->delay.received);
    EXPECT_EQ(graph->delay.samples, input);
    EXPECT_EQ(graph->delay.blockIndex, graph->source.blockIndex);
    EXPECT_EQ(graph->reverb.blockIndex, graph->source.blockIndex);
    EXPECT_EQ(graph->sidechain.samples, input);
    EXPECT_EQ(graph->left.samples, input);
    EXPECT_EQ(graph->right.samples, input);
}

TEST_F(AudioGraphTest, NoInputProducesNoOutputOrAllocations)
{
    AudioMemoryUsageMaxReset();
    AudioStream::update_all();
    EXPECT_FALSE(graph->delay.received);
    EXPECT_FALSE(graph->reverb.received);
    EXPECT_FALSE(graph->left.received);
    EXPECT_FALSE(graph->right.received);
    EXPECT_EQ(AudioMemoryUsageMax(), 0);
}

TEST_F(AudioGraphTest, SharedFilterAllocationFailurePreservesTheOtherConsumer)
{
    static SharedFilterGraph shared;
    // The original tap requires the source block to remain unmodified.
    AudioStream::initialize_memory(audioPool, 1);
    shared.filter.connect();
    EXPECT_TRUE(shared.source.push(signal()));
    AudioStream::update_all();
    EXPECT_TRUE(shared.original.received);
    EXPECT_EQ(shared.original.samples, signal());
    EXPECT_FALSE(shared.filtered.received);
    EXPECT_EQ(AudioMemoryUsage(), 0);
    AudioStream::initialize_memory(audioPool, std::size(audioPool));
}

TEST_F(AudioGraphTest, PrivateFilterCanRunWithOnlyOnePoolBlock)
{
    static BlockSource source;
    static AudioFilterStateVariable filter;
    static BlockSink output;
    static AudioConnection toFilter{&source, &filter};
    static AudioConnection toOutput{&filter, &output};
    AudioStream::initialize_memory(audioPool, 1);
    filter.connect();
    EXPECT_TRUE(source.push(signal()));
    AudioStream::update_all();
    EXPECT_TRUE(output.received);
    EXPECT_EQ(output.blockIndex, source.blockIndex);
    EXPECT_NE(output.samples, signal());
    EXPECT_EQ(AudioMemoryUsage(), 0);
    AudioStream::initialize_memory(audioPool, std::size(audioPool));
}

TEST_F(AudioGraphTest, ActiveFilterReusesPrivateInputAndSharesItBetweenBothSends)
{
    graph->filter.connect();
    graph->filter.setCutoff(0.5f);
    graph->render(signal());
    ASSERT_TRUE(graph->delay.received);
    EXPECT_NE(graph->delay.samples, signal());
    EXPECT_EQ(graph->delay.samples, graph->reverb.samples);
    EXPECT_EQ(graph->delay.blockIndex, graph->reverb.blockIndex);
    EXPECT_EQ(graph->delay.blockIndex, graph->source.blockIndex);
    EXPECT_EQ(graph->sidechain.samples, graph->delay.samples);
}

TEST_F(AudioGraphTest, SharedAndPrivateFilterPathsHaveIdenticalAudioAcrossControlChanges)
{
    static SharedFilterGraph shared;
    uint32_t random = 1234;
    for (int block = 0; block < 120; ++block)
    {
        for (AudioFilterStateVariable* filter : {&shared.filter, &graph->filter})
        {
            filter->setType(uint8_t(block % 3));
            filter->setCutoff(float(block % 9) / 10.0f);
            filter->resonance(0.7f + float(block % 3) * 0.5f);
            filter->setOverdriveAmount(block % 4 == 0 ? 20 : 0);
            if (block % 5) filter->connect();
            else filter->disconnect();
        }
        Samples input;
        for (auto& sample : input)
        {
            random = random * 1664525u + 1013904223u;
            sample = int16_t(int(random % 12000) - 6000);
        }
        graph->render(input);
        const Samples expected = graph->delay.samples;
        ASSERT_TRUE(shared.source.push(input));
        AudioStream::update_all();
        EXPECT_EQ(shared.original.samples, input) << block;
        EXPECT_EQ(shared.filtered.samples, expected) << block;
        EXPECT_EQ(AudioMemoryUsage(), 0) << block;
    }
}

TEST_F(AudioGraphTest, EqualPanGainsShareAnOutputWithoutChangingSidechainAudio)
{
    const Samples input = signal();
    graph->pan.gain(0.5f, MonoToStereo::left);
    graph->pan.gain(0.5f, MonoToStereo::right);
    graph->render(input);
    EXPECT_EQ(graph->left.samples, graph->right.samples);
    EXPECT_EQ(graph->left.blockIndex, graph->right.blockIndex);
    EXPECT_EQ(graph->sidechain.samples, input);
    graph->render(input);
    for (size_t i = 0; i < input.size(); ++i)
        EXPECT_EQ(graph->left.samples[i], int32_t(input[i]) >> 1) << i;
}

TEST_F(AudioGraphTest, PanTransitionFromSharedToSeparateOutputsPreservesBothRamps)
{
    Samples input;
    input.fill(8192);
    graph->pan.gain(0.5f, MonoToStereo::left);
    graph->pan.gain(0.5f, MonoToStereo::right);
    graph->render(input);
    graph->pan.gain(1.0f, MonoToStereo::right);
    graph->render(input);
    for (size_t i = 0; i < input.size(); ++i)
    {
        EXPECT_EQ(graph->left.samples[i], 4096) << i;
        EXPECT_EQ(graph->right.samples[i], 4096 + int(i) * 32) << i;
    }
    EXPECT_EQ(graph->sidechain.samples, input);
    EXPECT_NE(graph->left.blockIndex, graph->right.blockIndex);
}

TEST_F(AudioGraphTest, EqualPanTargetsKeepSeparateOutputsUntilDifferentRampsFinish)
{
    Samples input;
    input.fill(8192);
    graph->pan.gain(0.0f, MonoToStereo::left);
    graph->render(input);
    graph->render(input);
    graph->pan.gain(0.5f, MonoToStereo::left);
    graph->pan.gain(0.5f, MonoToStereo::right);
    graph->render(input);
    for (size_t i = 0; i < input.size(); ++i)
    {
        EXPECT_EQ(graph->left.samples[i], int(i) * 32) << i;
        EXPECT_EQ(graph->right.samples[i], 8192 - int(i) * 32) << i;
    }
    EXPECT_NE(graph->left.blockIndex, graph->right.blockIndex);
    graph->render(input);
    EXPECT_EQ(graph->left.blockIndex, graph->right.blockIndex);
    EXPECT_EQ(graph->left.samples, graph->right.samples);
    EXPECT_EQ(graph->left.samples.front(), 4096);
}

TEST_F(AudioGraphTest, UnsupportedFilterTypeRetainsItsSilentOutput)
{
    graph->filter.setType(255);
    graph->filter.connect();
    graph->render(signal());
    EXPECT_EQ(graph->delay.samples, Samples{});
}

TEST_F(AudioGraphTest, VolumeChangesPreservePreVolumeSends)
{
    const Samples input = signal();
    graph->amplifier.gain(0.5f);
    graph->render(input);  // Finish the gain ramp.
    graph->render(input);
    EXPECT_EQ(graph->delay.samples, input);
    EXPECT_EQ(graph->reverb.samples, input);
    EXPECT_NE(graph->sidechain.blockIndex, graph->delay.blockIndex);
    for (size_t i = 0; i < input.size(); ++i)
    {
        const int16_t expected = int32_t(input[i]) >> 1;
        EXPECT_EQ(graph->sidechain.samples[i], expected) << i;
        EXPECT_EQ(graph->left.samples[i], expected) << i;
        EXPECT_EQ(graph->right.samples[i], expected) << i;
    }
}

TEST_F(AudioGraphTest, VolumeAfterAnActiveFilterPreservesFilteredSends)
{
    graph->filter.connect();
    graph->amplifier.gain(0.5f);
    graph->render(signal());
    graph->render(signal());
    for (size_t i = 0; i < AUDIO_BLOCK_SAMPLES; ++i)
    {
        EXPECT_EQ(graph->reverb.samples[i], graph->delay.samples[i]);
        EXPECT_EQ(graph->sidechain.samples[i], int32_t(graph->delay.samples[i]) >> 1) << i;
    }
}

TEST_F(AudioGraphTest, PanChangesPreserveSendsAndSidechain)
{
    const Samples input = signal();
    graph->pan.gain(0.0f, MonoToStereo::left);
    graph->render(input);
    graph->render(input);
    EXPECT_EQ(graph->delay.samples, input);
    EXPECT_EQ(graph->reverb.samples, input);
    EXPECT_EQ(graph->sidechain.samples, input);
    EXPECT_EQ(graph->left.samples, Samples{});
    EXPECT_EQ(graph->right.samples, input);
    EXPECT_NE(graph->left.blockIndex, graph->right.blockIndex);
}

TEST_F(AudioGraphTest, GainRampCompletesWithinOneBlock)
{
    Samples input;
    input.fill(8192);
    graph->amplifier.gain(0.0f);
    graph->render(input);
    for (size_t i = 0; i < input.size(); ++i)
        EXPECT_EQ(graph->sidechain.samples[i], 8192 - int(i) * 64) << i;
    graph->render(input);
    EXPECT_EQ(graph->sidechain.samples, Samples{});
    EXPECT_EQ(graph->delay.samples, input);
}

TEST_F(AudioGraphTest, AmplifierClipsWithoutChangingTheSendSignal)
{
    Samples input;
    for (size_t i = 0; i < input.size(); ++i)
        input[i] = i % 2 == 0 ? 24000 : -24000;
    graph->amplifier.gain(2.0f);
    graph->render(input);
    graph->render(input);
    EXPECT_EQ(graph->delay.samples, input);
    for (size_t i = 0; i < input.size(); ++i)
        EXPECT_EQ(graph->sidechain.samples[i], i % 2 == 0 ? INT16_MAX : INT16_MIN) << i;
}

TEST_F(AudioGraphTest, OverdriveStillRunsWithTheFilterDisabled)
{
    Samples input;
    input.fill(1000);
    graph->filter.setOverdriveAmount(16);  // Unity pre-gain, 3x in the quiet region.
    graph->render(input);
    EXPECT_EQ(graph->delay.samples.front(), 3000);
    EXPECT_EQ(graph->delay.samples.back(), 3000);
    EXPECT_EQ(graph->reverb.samples, graph->delay.samples);
}

TEST_F(AudioGraphTest, DisablingFilterRampsFromPreviousOutputThenBypasses)
{
    graph->filter.connect();
    graph->render(signal());
    const int32_t previous = graph->delay.samples.back();
    graph->filter.disconnect();
    const Samples input = signal();
    graph->render(input);
    for (size_t i = 0; i < input.size(); ++i)
    {
        const int32_t expected = (int32_t(input[i]) * int(i + 1) + previous * int(127 - i)) >> 7;
        EXPECT_EQ(graph->delay.samples[i], expected) << i;
    }
    graph->render(input);
    EXPECT_EQ(graph->delay.samples, input);
    EXPECT_EQ(graph->delay.blockIndex, graph->source.blockIndex);
}

TEST_F(AudioGraphTest, SilentActiveFilterStaysSilentAcrossTypeChanges)
{
    graph->filter.connect();
    for (uint8_t type = 0; type < 3; ++type)
    {
        graph->filter.setType(type);
        graph->render(Samples{});
        EXPECT_EQ(graph->delay.samples, Samples{});
        EXPECT_EQ(graph->left.samples, Samples{});
    }
}

// Measure the real graph's settled response, independently of the filter's
// implementation. These broad ratios detect a missing or swapped filter mode.
double responsePower(VoiceGraph& graph, double frequency)
{
    constexpr double pi = 3.14159265358979323846;
    double power = 0.0;
    for (int block = 0; block < 40; ++block)
    {
        Samples input;
        for (size_t i = 0; i < input.size(); ++i)
        {
            const int frame = block * AUDIO_BLOCK_SAMPLES + int(i);
            input[i] = int16_t(4000.0 * std::sin(2.0 * pi * frequency * frame / AUDIO_SAMPLE_RATE_EXACT));
        }
        graph.render(input);
        if (block >= 20)
            for (int16_t sample : graph.delay.samples)
                power += double(sample) * sample;
    }
    return power / (20 * AUDIO_BLOCK_SAMPLES);
}

TEST_F(AudioGraphTest, LowPassAttenuatesHighFrequencies)
{
    graph->filter.connect();  // Default cutoff is 1 kHz.
    const double low = responsePower(*graph, 200.0);
    const double high = responsePower(*graph, 8000.0);
    EXPECT_GT(low, 1000000.0);
    EXPECT_LT(high, low * 0.01);
}

TEST_F(AudioGraphTest, HighPassAttenuatesLowFrequencies)
{
    graph->filter.setType(1);
    graph->filter.connect();
    const double low = responsePower(*graph, 100.0);
    const double high = responsePower(*graph, 8000.0);
    EXPECT_GT(high, 1000000.0);
    EXPECT_LT(low, high * 0.01);
}

TEST_F(AudioGraphTest, BandPassAttenuatesBothSidesOfItsPassband)
{
    graph->filter.setType(2);
    graph->filter.connect();
    const double low = responsePower(*graph, 100.0);
    const double middle = responsePower(*graph, 1000.0);
    const double high = responsePower(*graph, 8000.0);
    EXPECT_GT(middle, 1000000.0);
    EXPECT_LT(low, middle * 0.05);
    EXPECT_LT(high, middle * 0.05);
}

TEST_F(AudioGraphTest, RepeatedFilterChangesReleaseEveryAudioBlock)
{
    for (int block = 0; block < 200; ++block)
    {
        if (block % 2 == 0)
            graph->filter.connect();
        else
            graph->filter.disconnect();
        graph->filter.setType(uint8_t(block % 3));
        graph->filter.setCutoff(float(block % 10) / 10.0f);
        graph->filter.resonance(0.7f + float(block % 5));
        graph->render(signal());
        ASSERT_EQ(AudioMemoryUsage(), 0) << block;
        ASSERT_TRUE(graph->left.received) << block;
    }
}
}  // namespace
