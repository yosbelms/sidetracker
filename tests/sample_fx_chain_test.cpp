#include "SampleFxPreset.h"
#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

using namespace samplefx;
namespace
{
struct BackendFixture : Backend
{
    Audio input;
    Audio* output = nullptr;
    Node node;
    std::vector<float> begun;
    bool reject = false;
    bool failStep = false;
    bool begin(const Node& n, const Audio& in, Audio& out, uint32_t cap, uint32_t gen) override
    {
        if (reject) return false;
        node = n; input = in; output = &out;
        begun.push_back(n.params[0]);
        out.frames = (n.flags & 1) ? gen : in.frames;
        out.channels = (n.flags & 8) ? 1 : in.channels;
        return out.frames <= cap;
    }
    bool step(bool& complete) override
    {
        if (failStep) return false;
        for (unsigned c = 0; c < output->channels; ++c)
            for (unsigned i = 0; i < output->frames; ++i)
            {
                int value = (node.flags & 1) ? 3 : input.channel[c][i];
                output->channel[c][i] = node.effect == 3 ? value * node.params[0] : value + node.params[0];
            }
        complete = true;
        return true;
    }
    const char* error() const override { return "injected failure"; }
};
struct ChainTest : testing::Test
{
    BackendFixture backend;
    Runner runner{backend};
    int16_t storage[2][2][16] = {};
    int16_t* work[2][2] = { { storage[0][0], storage[0][1] }, { storage[1][0], storage[1][1] } };
    ChainDefinition definition;
    Audio source{ { storage[0][0], storage[0][1] }, 8, 1 };
    Node stage(uint8_t kind, float value)
    { Node n; n.effect = kind; n.count = 1; n.params[0] = value; return n; }
    void finish()
    {
        for (unsigned i = 0; i < 64 && runner.state() == Runner::State::Preparing; ++i) runner.update();
    }
};
}

TEST_F(ChainTest, OrderedPassesAndIndependentDuplicateSettings)
{
    storage[0][0][0] = 5;
    definition.add(stage(3, 2)); definition.add(stage(4, 1));
    ASSERT_TRUE(definition.duplicate(0));
    definition.stages[1].params[0] = 3;
    ASSERT_TRUE(runner.start(definition, source, work, 16)); finish();
    ASSERT_EQ(runner.state(), Runner::State::Ready);
    EXPECT_EQ(runner.result().channel[0][0], 31);
    EXPECT_EQ(backend.begun, (std::vector<float>{2, 3, 1}));
    EXPECT_EQ(definition.stages[0].params[0], 2);
}
TEST_F(ChainTest, BypassSkipsBackendAndPreservesAudio)
{
    storage[0][0][0] = 123;
    definition.add(stage(3, 10)); definition.stages[0].bypass = true;
    ASSERT_TRUE(runner.start(definition, source, work, 16)); finish();
    ASSERT_EQ(runner.state(), Runner::State::Ready);
    EXPECT_EQ(runner.result().channel[0][0], 123);
    EXPECT_TRUE(backend.begun.empty());
}
TEST_F(ChainTest, GeneratorWorksWithoutInputAndFeedsEffects)
{
    definition.source = stage(kPlugin, 0);
    definition.source.flags = 1;
    strcpy(definition.source.filename, "kick.ptfx");
    definition.generatorFrames = 12;
    source.frames = 0;
    definition.add(stage(3, 4));
    ASSERT_TRUE(runner.start(definition, source, work, 16)); finish();
    ASSERT_EQ(runner.state(), Runner::State::Ready);
    EXPECT_EQ(runner.result().frames, 12);
    EXPECT_EQ(runner.result().channel[0][11], 12);
    EXPECT_EQ(backend.begun.size(), 2);
    runner.update();
    EXPECT_EQ(backend.begun.size(), 2); // Ready audio is retained, not generated again.
}
TEST_F(ChainTest, CancelAndParameterChangesInvalidatePreparedAudio)
{
    definition.add(stage(3, 2));
    ASSERT_TRUE(runner.start(definition, source, work, 16));
    runner.update(); runner.cancel(); runner.update();
    EXPECT_EQ(runner.state(), Runner::State::Dirty);
    EXPECT_EQ(runner.result().frames, 0);
    ASSERT_TRUE(runner.start(definition, source, work, 16)); finish();
    runner.invalidate();
    EXPECT_EQ(runner.state(), Runner::State::Dirty);
    EXPECT_EQ(runner.result().frames, 0);
}
TEST_F(ChainTest, ErrorsNeverPublishIntermediateResults)
{
    definition.add(stage(3, 2));
    backend.failStep = true;
    ASSERT_TRUE(runner.start(definition, source, work, 16)); finish();
    EXPECT_EQ(runner.state(), Runner::State::Failed);
    EXPECT_EQ(runner.result().frames, 0);
    EXPECT_STREQ(runner.error(), "injected failure");
}
TEST_F(ChainTest, OversizedGenerationAndEmptySampleAreRejected)
{
    source.frames = 0;
    EXPECT_FALSE(runner.start(definition, source, work, 16));
    definition.source.effect = kPlugin; definition.source.flags = 1;
    strcpy(definition.source.filename, "generator.ptfx");
    definition.generatorFrames = 17;
    ASSERT_TRUE(runner.start(definition, source, work, 16)); finish();
    EXPECT_EQ(runner.state(), Runner::State::Failed);
    EXPECT_TRUE(backend.begun.size() <= 1);
}
TEST_F(ChainTest, MonoConversionReachesDownstreamStage)
{
    source.channels = 2;
    storage[0][0][0] = 5; storage[0][1][0] = 9;
    Node convert = stage(kPlugin, 0); convert.flags = 8;
    strcpy(convert.filename, "mono.ptfx");
    definition.add(convert); definition.add(stage(3, 2));
    ASSERT_TRUE(runner.start(definition, source, work, 16)); finish();
    ASSERT_EQ(runner.state(), Runner::State::Ready);
    EXPECT_EQ(runner.result().channels, 1);
    EXPECT_EQ(runner.result().channel[0][0], 10);
}
TEST_F(ChainTest, PresetRoundTripIsVersionedAndChecksummed)
{
    definition.add(stage(3, 2));
    definition.source.effect = kPlugin; definition.source.flags = 5;
    definition.source.fingerprint = 0xdeadbeef;
    strcpy(definition.source.filename, "wavetable.ptfx");
    std::array<uint8_t, kPresetFileBytes> bytes;
    ASSERT_TRUE(encodePreset(definition, bytes.data(), bytes.size()));
    ChainDefinition restored;
    ASSERT_TRUE(decodePreset(bytes.data(), bytes.size(), restored));
    EXPECT_EQ(restored.source.flags, 5);
    EXPECT_EQ(restored.source.fingerprint, 0xdeadbeef);
    EXPECT_EQ(restored.stages[0].params[0], 2);
    bytes.back() ^= 1;
    restored.count = 7;
    EXPECT_FALSE(decodePreset(bytes.data(), bytes.size(), restored));
    EXPECT_EQ(restored.count, 7); // Invalid files cannot replace a draft.
    EXPECT_FALSE(decodePreset(bytes.data(), bytes.size() - 1, restored));
}
TEST_F(ChainTest, ObsoletePresetFormatsAreRejected)
{
    std::array<uint8_t, kPresetFileBytes> bytes;
    ASSERT_TRUE(encodePreset(definition, bytes.data(), bytes.size()));
    EXPECT_EQ(memcmp(bytes.data(), "PTPR", 4), 0);
    EXPECT_EQ(bytes[4], 1);
    memcpy(bytes.data(), "PTCH", 4);
    ChainDefinition restored;
    restored.count = 1;
    for (uint8_t version : {1, 2})
    {
        bytes[4] = version;
        bytes[7] = 0; // The obsolete format stored the inverse mix.
        uint32_t hash = fingerprint(bytes.data() + 16, bytes.size() - 16, fingerprint(bytes.data(), 12));
        for (unsigned i = 0; i < 4; ++i) bytes[12 + i] = hash >> (i * 8);
        EXPECT_FALSE(decodePreset(bytes.data(), bytes.size(), restored));
        EXPECT_EQ(restored.count, 1);
    }
}
TEST_F(ChainTest, InvalidChainsAndStageBoundsAreRejected)
{
    for (unsigned i = 0; i < kMaxStages; ++i) ASSERT_TRUE(definition.add(stage(3, i)));
    EXPECT_FALSE(definition.add(stage(3, 1)));
    EXPECT_FALSE(definition.move(0, -1));
    EXPECT_FALSE(definition.move(7, 1));
    EXPECT_FALSE(definition.duplicate(0));
    ASSERT_TRUE(definition.remove(1));
    EXPECT_EQ(definition.stages[1].params[0], 2);
    std::array<uint8_t, kPresetFileBytes> bytes;
    definition.stages[0].params[0] = NAN;
    EXPECT_FALSE(encodePreset(definition, bytes.data(), bytes.size()));
    definition.stages[0].params[0] = 0;
    definition.source.effect = kPlugin; definition.source.flags = 1;
    strcpy(definition.source.filename, "../escape.ptfx");
    EXPECT_FALSE(encodePreset(definition, bytes.data(), bytes.size()));
}

TEST_F(ChainTest, PresetPreservesWholeChainMix)
{
    std::array<uint8_t, kPresetFileBytes> bytes;
    ChainDefinition restored;
    ASSERT_TRUE(encodePreset(definition, bytes.data(), bytes.size()));
    EXPECT_EQ(bytes[7], 100);
    ASSERT_TRUE(decodePreset(bytes.data(), bytes.size(), restored));
    EXPECT_EQ(restored.mix, 100);
    for (unsigned percent : {0u, 35u, 100u})
    {
        definition.mix = percent;
        ASSERT_TRUE(encodePreset(definition, bytes.data(), bytes.size()));
        ASSERT_TRUE(decodePreset(bytes.data(), bytes.size(), restored));
        EXPECT_EQ(restored.mix, percent);
    }
    definition.mix = 101;
    EXPECT_FALSE(encodePreset(definition, bytes.data(), bytes.size()));
    bytes[7] = 101;
    uint32_t hash = fingerprint(bytes.data() + 16, bytes.size() - 16, fingerprint(bytes.data(), 12));
    for (unsigned i = 0; i < 4; ++i) bytes[12 + i] = hash >> (i * 8);
    restored.mix = 35;
    EXPECT_FALSE(decodePreset(bytes.data(), bytes.size(), restored));
    EXPECT_EQ(restored.mix, 35);
}

TEST_F(ChainTest, MissingOrAliasedBuffersAreRejectedBeforeProcessing)
{
    source.channel[0] = nullptr;
    EXPECT_FALSE(runner.start(definition, source, work, 16));
    source.channel[0] = storage[0][0];
    work[1][0] = work[0][0];
    EXPECT_FALSE(runner.start(definition, source, work, 16));
    EXPECT_TRUE(backend.begun.empty());
}
