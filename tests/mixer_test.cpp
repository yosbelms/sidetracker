#include "audio_test_support.h"
#include "effect_bitDepth.h"
#include "mixer.h"
#include <gtest/gtest.h>
#include <memory>
#include <vector>

using namespace ce::test;

namespace
{
struct MixerGraph
{
    BlockSource a, b;
    AudioBitDepth crusher;
    AudioMixer<2> mixer;
    BlockSink sourceTap, output;
    AudioConnection aToCrusher{&a, &crusher};
    AudioConnection crusherToMixer{&crusher, 0, &mixer, 0};
    AudioConnection bToMixer{&b, 0, &mixer, 1};
    AudioConnection aToTap{&a, &sourceTap};
    AudioConnection mixerToOutput{&mixer, &output};
    void render(const Samples& first, const Samples& second = {})
    {
        ASSERT_TRUE(a.push(first));
        ASSERT_TRUE(b.push(second));
        AudioStream::update_all();
    }
};
std::vector<std::unique_ptr<MixerGraph>> graphs;

class MixerTest : public testing::Test
{
  protected:
    void SetUp() override
    {
        graphs.emplace_back(new MixerGraph);
        graph = graphs.back().get();
    }
    void TearDown() override { EXPECT_EQ(AudioMemoryUsage(), 0); }
    MixerGraph* graph = nullptr;
};

TEST_F(MixerTest, AddsChannelsWithoutChangingSharedSourceSamples)
{
    Samples a, b;
    a.fill(3000);
    b.fill(-500);
    graph->render(a, b);
    EXPECT_EQ(graph->sourceTap.samples, a);
    for (int16_t sample : graph->output.samples) EXPECT_EQ(sample, 2500);
}

TEST_F(MixerTest, HardClippingSaturatesBothSigns)
{
    Samples a;
    for (size_t i = 0; i < a.size(); ++i) a[i] = i % 2 ? -30000 : 30000;
    graph->render(a, a);
    for (size_t i = 0; i < a.size(); ++i)
        EXPECT_EQ(graph->output.samples[i], i % 2 ? INT16_MIN : INT16_MAX);
}

TEST_F(MixerTest, SettledGainsScaleOneChannelAndMuteTheOther)
{
    Samples a, b;
    a.fill(2000);
    b.fill(999);
    graph->mixer.gain(0, 0.5f);
    graph->mixer.gain(1, 0.0f);
    graph->render(a, b);
    graph->render(a, b);
    for (int16_t sample : graph->output.samples) EXPECT_EQ(sample, 1000);
}

TEST_F(MixerTest, EightBitCrusherClearsLowBitsWithoutChangingTheSharedSource)
{
    Samples a{};
    a[0] = 511;
    a[1] = -1;
    a[2] = -257;
    graph->crusher.setBitDepth(8);
    graph->render(a);
    EXPECT_EQ(graph->sourceTap.samples, a);
    EXPECT_EQ(graph->output.samples[0], 256);
    EXPECT_EQ(graph->output.samples[1], -256);
    EXPECT_EQ(graph->output.samples[2], -512);
}

TEST_F(MixerTest, CrusherClampsOutOfRangeDepthsBeforeBuildingTheMask)
{
    Samples a;
    a.fill(5000);
    graph->crusher.setBitDepth(0);
    graph->render(a);
    EXPECT_EQ(graph->output.samples.front(), 4096);
    graph->crusher.setBitDepth(17);
    graph->render(a);
    EXPECT_EQ(graph->output.samples, a);
    graph->crusher.setBitDepth(255);
    graph->render(a);
    EXPECT_EQ(graph->output.samples, a);
}
}  // namespace
