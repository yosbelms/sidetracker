#include <gtest/gtest.h>
#include "PerformanceFx/PunchFx.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <set>

namespace
{
constexpr unsigned kBlock = 128;
constexpr auto pad(unsigned key) { return punch::owner(punch::Source::Pads, key); }

class PunchFxTest : public testing::Test
{
  protected:
    std::unique_ptr<punch::Storage> storage = std::make_unique<punch::Storage>();
    punch::Rack rack{*storage};
    std::array<int16_t, kBlock> left{}, right{}, outL{}, outR{};
    unsigned cursor = 0;

    void block(float tempo = 120, uint8_t mix = 100)
    {
        for (unsigned i = 0; i < kBlock; ++i, ++cursor)
        {
            left[i] = static_cast<int16_t>(9000 * std::sin(cursor * 0.036f) + int(cursor % 127) - 63);
            right[i] = static_cast<int16_t>(6000 * std::sin(cursor * 0.057f));
        }
        rack.beginBlock(tempo, 44100, mix, true);
        if (rack.wet())
            rack.process(left.data(), right.data(), outL.data(), outR.data(), kBlock);
        else
        {
            rack.process(left.data(), right.data(), nullptr, nullptr, kBlock);
            outL = left;
            outR = right;
        }
    }

    void advance(unsigned blocks, float tempo = 120, uint8_t mix = 100)
    {
        for (unsigned i = 0; i < blocks; ++i) block(tempo, mix);
    }
};

TEST_F(PunchFxTest, EachPresetProcessesStereoAndHasADistinctResult)
{
    for (unsigned col = 0; col < punch::kColumns; ++col)
    {
        SCOPED_TRACE(punch::columns[col].name);
        std::set<uint32_t> signatures;
        for (uint8_t row = 0; row < punch::kLevels; ++row)
        {
            rack.reset();
            cursor = 0;
            advance(96);
            rack.press(pad(0), punch::columns[col].effect, row);
            uint32_t checksum = 2166136261u;
            bool changedL = false, changedR = false;
            for (unsigned i = 0; i < 144; ++i)
            {
                block();
                changedL |= outL != left;
                changedR |= outR != right;
                for (auto sample : outL) checksum = (checksum ^ static_cast<uint16_t>(sample)) * 16777619u;
            }
            EXPECT_EQ(rack.soundingLevel(col), row);
            EXPECT_TRUE(changedL);
            EXPECT_TRUE(changedR);
            EXPECT_TRUE(signatures.insert(checksum).second);
        }
    }
}

TEST_F(PunchFxTest, LatestRowFallsBackAndFourthColumnWaitsForRelease)
{
    advance(8);
    rack.press(pad(0), punch::Effect::Loop, 0);
    rack.press(pad(1), punch::Effect::TripletLoop, 1);
    rack.press(pad(2), punch::Effect::Unison, 2);
    rack.press(pad(3), punch::Effect::TapeStop, 3);
    advance(8);
    EXPECT_EQ(rack.soundingLevel(0), 0);
    EXPECT_EQ(rack.soundingLevel(1), 1);
    EXPECT_EQ(rack.soundingLevel(2), 2);
    EXPECT_EQ(rack.soundingLevel(3), -1);

    rack.press(pad(12), punch::Effect::Loop, 1);
    advance(18);
    EXPECT_EQ(rack.soundingLevel(0), 1);
    EXPECT_EQ(rack.soundingLevel(3), -1);
    rack.release(pad(12));
    advance(18);
    EXPECT_EQ(rack.soundingLevel(0), 0);

    rack.release(pad(1));
    block();
    EXPECT_EQ(rack.soundingLevel(3), -1);
    advance(16);
    EXPECT_EQ(rack.soundingLevel(1), -1);
    EXPECT_EQ(rack.soundingLevel(3), 3);
}

TEST_F(PunchFxTest, ZeroMixAndIdlePreservePcmAndResetClearsHistory)
{
    advance(8);
    EXPECT_EQ(outL, left);
    EXPECT_EQ(outR, right);
    rack.press(pad(0), punch::Effect::Reverse, 3);
    advance(8);
    EXPECT_NE(outL, left);
    block(120, 0);
    EXPECT_TRUE(rack.wet());
    advance(5, 120, 0);
    EXPECT_FALSE(rack.wet());
    EXPECT_EQ(outL, left);
    EXPECT_EQ(outR, right);
    EXPECT_EQ(rack.soundingLevel(11), 3);

    rack.reset();
    rack.beginBlock(120, 44100, 100, true);
    EXPECT_EQ(rack.soundingLevel(11), -1);
    EXPECT_FALSE(rack.wet());
    rack.press(pad(0), punch::Effect::Reverse, 3);
    rack.beginBlock(120, 44100, 100, true);
    EXPECT_EQ(rack.soundingLevel(11), -1);
    rack.process(left.data(), right.data(), nullptr, nullptr, 64);
    block();
    EXPECT_EQ(rack.soundingLevel(11), 3);
}

TEST_F(PunchFxTest, OwnerGroupsAndOverflowFailDry)
{
    rack.press(pad(1), punch::Effect::Stutter, 0);
    rack.press(punch::owner(punch::Source::Sequencer, 1), punch::Effect::PitchUp, 3);
    advance(8);
    rack.releaseSource(punch::Source::Pads);
    advance(16);
    EXPECT_EQ(rack.soundingLevel(6), -1);
    EXPECT_EQ(rack.soundingLevel(4), 3);
    for (unsigned i = 0; i < 128; ++i) rack.press(pad(0), punch::Effect::Loop, 0);
    block();
    EXPECT_FALSE(rack.wet());
    EXPECT_EQ(rack.soundingLevel(4), -1);
    EXPECT_EQ(outL, left);
}

TEST_F(PunchFxTest, HistoryWrapAndExtremeTempoRemainBounded)
{
    advance(punch::kHistoryFrames / kBlock + 2, 5);
    rack.press(pad(0), punch::Effect::Scratch, 3);
    rack.press(pad(1), punch::Effect::Retrigger, 3);
    rack.press(pad(2), punch::Effect::Quantize, 3);
    advance(punch::kHistoryFrames / kBlock + 2, 800);
    EXPECT_EQ(rack.soundingLevel(8), 3);
    EXPECT_EQ(rack.soundingLevel(9), 3);
    EXPECT_EQ(rack.soundingLevel(10), 3);
    rack.releaseSource(punch::Source::Pads);
    advance(16, 5);
    EXPECT_FALSE(rack.wet());
}

TEST_F(PunchFxTest, TapeStopSlowsAcrossHistoryWrapThenStaysSilentUntilRelease)
{
    advance(punch::kHistoryFrames / kBlock);
    rack.press(pad(0), punch::Effect::TapeStop, 0);
    advance(8);
    const auto crossings = [&] {
        unsigned count = 0;
        for (unsigned i = 0; i < 16; ++i)
        {
            block();
            for (unsigned sample = 1; sample < kBlock; ++sample)
                if (outL[sample - 1] < 0 && outL[sample] >= 0) ++count;
        }
        return count;
    };
    const auto early = crossings();
    advance(96);
    const auto late = crossings();
    EXPECT_GT(late, 0u);
    EXPECT_LT(2 * late, early);

    advance(38);
    EXPECT_EQ(rack.soundingLevel(3), 0);
    EXPECT_EQ(outL, (std::array<int16_t, kBlock>{}));
    EXPECT_EQ(outR, (std::array<int16_t, kBlock>{}));
    rack.release(pad(0));
    advance(16);
    EXPECT_EQ(outL, left);
    EXPECT_EQ(outR, right);
}

TEST_F(PunchFxTest, FilterSweepClosesOverABarToItsLabeledCutoff)
{
    rack.press(pad(0), punch::Effect::FilterSweep, 3);
    float cutoffEnergy = 0, highEnergy = 0, dryEnergy = 0;
    for (unsigned b = 0; b < 720; ++b)
    {
        for (unsigned i = 0; i < kBlock; ++i, ++cursor)
        {
            constexpr float radiansPerSample = 6.28318531f / 44100;
            left[i] = static_cast<int16_t>(8000 * std::sin(cursor * 250 * radiansPerSample));
            right[i] = static_cast<int16_t>(8000 * std::sin(cursor * 4000 * radiansPerSample));
        }
        rack.beginBlock(120, 44100, 100, true);
        rack.process(left.data(), right.data(), outL.data(), outR.data(), kBlock);
        if (b == 8)
            EXPECT_GT(*std::max_element(outR.begin(), outR.end()), 7800);
        if (b < 704) continue;
        for (unsigned i = 0; i < kBlock; ++i)
        {
            dryEnergy += left[i] * left[i];
            cutoffEnergy += outL[i] * outL[i];
            highEnergy += outR[i] * outR[i];
        }
    }
    EXPECT_EQ(rack.soundingLevel(7), 3);
    EXPECT_NEAR(cutoffEnergy / dryEnergy, 0.5f, 0.03f);
    EXPECT_LT(highEnergy / dryEnergy, 0.0001f);
    rack.release(pad(0));
    advance(16);
    EXPECT_EQ(outL, left);
    EXPECT_EQ(outR, right);
}

TEST_F(PunchFxTest, RetiredEffectIdsDoNotTriggerReplacementColumns)
{
    advance(8);
    rack.press(pad(0), static_cast<punch::Effect>(4), 0);
    rack.press(pad(1), static_cast<punch::Effect>(8), 0);
    block();
    EXPECT_FALSE(rack.wet());
    EXPECT_EQ(rack.soundingLevel(3), -1);
    EXPECT_EQ(rack.soundingLevel(7), -1);
}
}
