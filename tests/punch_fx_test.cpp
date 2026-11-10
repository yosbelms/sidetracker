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
            // Fill enough history for the longest Dub Echo division at 120 BPM.
            advance(320);
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
    rack.press(pad(0), punch::Effect::BeatRepeat, 0);
    rack.press(pad(1), punch::Effect::Stutter, 1);
    rack.press(pad(2), punch::Effect::Scatter, 2);
    rack.press(pad(3), punch::Effect::DubEcho, 3);
    advance(8);
    EXPECT_EQ(rack.soundingLevel(0), 0);
    EXPECT_EQ(rack.soundingLevel(1), 1);
    EXPECT_EQ(rack.soundingLevel(2), 2);
    EXPECT_EQ(rack.soundingLevel(3), -1);

    rack.press(pad(12), punch::Effect::BeatRepeat, 1);
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
    EXPECT_EQ(rack.soundingLevel(5), 3);

    rack.reset();
    rack.beginBlock(120, 44100, 100, true);
    EXPECT_EQ(rack.soundingLevel(5), -1);
    EXPECT_FALSE(rack.wet());
    rack.press(pad(0), punch::Effect::Reverse, 3);
    rack.beginBlock(120, 44100, 100, true);
    EXPECT_EQ(rack.soundingLevel(5), -1);
    rack.process(left.data(), right.data(), nullptr, nullptr, 64);
    block();
    EXPECT_EQ(rack.soundingLevel(5), 3);
}

TEST_F(PunchFxTest, OwnerGroupsAndOverflowFailDry)
{
    rack.press(pad(1), punch::Effect::Stutter, 0);
    rack.press(punch::owner(punch::Source::Sequencer, 1), punch::Effect::PitchUp, 3);
    advance(8);
    rack.releaseSource(punch::Source::Pads);
    advance(16);
    EXPECT_EQ(rack.soundingLevel(1), -1);
    EXPECT_EQ(rack.soundingLevel(4), 3);
    for (unsigned i = 0; i < 128; ++i) rack.press(pad(0), punch::Effect::Loop, 0);
    block();
    EXPECT_FALSE(rack.wet());
    EXPECT_EQ(rack.soundingLevel(4), -1);
    EXPECT_EQ(outL, left);
}

TEST_F(PunchFxTest, MixChangesWithNoPunchesKeepThePlaybackDry)
{
    advance(8);
    for (uint8_t percent : {99, 62, 8, 0, 11, 68, 100})
    {
        block(120, percent);
        EXPECT_FALSE(rack.wet());
        EXPECT_EQ(outL, left);
        EXPECT_EQ(outR, right);
    }
}

TEST_F(PunchFxTest, HistoryWrapAndExtremeTempoRemainBounded)
{
    advance(punch::kHistoryFrames / kBlock + 2, 5);
    rack.press(pad(0), punch::Effect::Scratch, 3);
    rack.press(pad(1), punch::Effect::Scatter, 3);
    rack.press(pad(2), punch::Effect::DubEcho, 3);
    advance(punch::kHistoryFrames / kBlock + 2, 800);
    EXPECT_EQ(rack.soundingLevel(6), 3);
    EXPECT_EQ(rack.soundingLevel(2), 3);
    EXPECT_EQ(rack.soundingLevel(3), 3);
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
    EXPECT_EQ(rack.soundingLevel(7), 0);
    EXPECT_EQ(outL, (std::array<int16_t, kBlock>{}));
    EXPECT_EQ(outR, (std::array<int16_t, kBlock>{}));
    rack.release(pad(0));
    advance(16);
    EXPECT_EQ(outL, left);
    EXPECT_EQ(outR, right);
}

TEST_F(PunchFxTest, LowPassClosesOverABarToItsLabeledCutoff)
{
    rack.press(pad(0), punch::Effect::LowPass, 3);
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
    EXPECT_EQ(rack.soundingLevel(8), 3);
    EXPECT_NEAR(cutoffEnergy / dryEnergy, 0.5f, 0.03f);
    EXPECT_LT(highEnergy / dryEnergy, 0.0001f);
    rack.release(pad(0));
    advance(16);
    EXPECT_EQ(outL, left);
    EXPECT_EQ(outR, right);
}

TEST_F(PunchFxTest, HighPassPresetsReachTheirCutoffsWithoutHighFrequencyBoost)
{
    constexpr float cutoffs[] = {160, 400, 900, 1800};
    constexpr float radiansPerSample = 6.28318531f / 44100;
    for (uint8_t row = 0; row < punch::kLevels; ++row)
    {
        SCOPED_TRACE(cutoffs[row]);
        rack.reset();
        rack.beginBlock(120, 44100, 100, true);
        rack.press(pad(9), punch::Effect::HighPass, row);
        float cutoffEnergy = 0, highEnergy = 0, dryCutoffEnergy = 0, dryHighEnergy = 0;
        for (unsigned b = 0; b < 128; ++b)
        {
            for (unsigned i = 0; i < kBlock; ++i, ++cursor)
            {
                left[i] = static_cast<int16_t>(8000 * std::sin(cursor * cutoffs[row] * radiansPerSample));
                right[i] = static_cast<int16_t>(8000 * std::sin(cursor * 12000 * radiansPerSample));
            }
            rack.beginBlock(120, 44100, 100, true);
            rack.process(left.data(), right.data(), outL.data(), outR.data(), kBlock);
            if (b < 32) continue;
            for (unsigned i = 0; i < kBlock; ++i)
            {
                cutoffEnergy += outL[i] * outL[i];
                highEnergy += outR[i] * outR[i];
                dryCutoffEnergy += left[i] * left[i];
                dryHighEnergy += right[i] * right[i];
            }
        }
        EXPECT_EQ(rack.soundingLevel(9), row);
        EXPECT_NEAR(cutoffEnergy / dryCutoffEnergy, 0.5f, 0.01f);
        EXPECT_GT(highEnergy / dryHighEnergy, 0.98f);
        EXPECT_LE(highEnergy / dryHighEnergy, 1.001f);
    }

    rack.press(pad(8), punch::Effect::LowPass, 1);
    block();
    EXPECT_EQ(rack.soundingLevel(8), 1);
    EXPECT_EQ(rack.soundingLevel(9), 3);
    rack.releaseSource(punch::Source::Pads);
    advance(16);
    EXPECT_EQ(outL, left);
    EXPECT_EQ(outR, right);
}

TEST_F(PunchFxTest, CrushPresetsHoldAndQuantizeBothPolaritiesSymmetrically)
{
    constexpr unsigned periods[] = {4, 8, 16, 32};
    constexpr int steps[] = {64, 256, 1024, 4096};
    for (uint8_t row = 0; row < punch::kLevels; ++row)
    {
        SCOPED_TRACE(static_cast<unsigned>(row));
        rack.reset();
        block();
        rack.press(pad(10), punch::Effect::Crush, row);
        advance(16);
        for (unsigned i = 0; i < kBlock; ++i)
        {
            left[i] = static_cast<int16_t>(i * 231 + 31);
            right[i] = -left[i];
        }
        rack.beginBlock(120, 44100, 100, true);
        rack.process(left.data(), right.data(), outL.data(), outR.data(), kBlock);
        for (unsigned i = 0; i < kBlock; ++i)
        {
            const auto held = left[i - i % periods[row]];
            const auto expected = held / steps[row] * steps[row];
            EXPECT_EQ(outL[i], expected);
            EXPECT_EQ(outR[i], -expected);
        }
    }
}

TEST_F(PunchFxTest, RetiredEffectIdsDoNotTriggerReplacementColumns)
{
    advance(8);
    rack.press(pad(0), static_cast<punch::Effect>(4), 0);
    rack.press(pad(1), static_cast<punch::Effect>(8), 0);
    rack.press(pad(2), static_cast<punch::Effect>(10), 0);
    rack.press(pad(3), static_cast<punch::Effect>(18), 0);
    block();
    EXPECT_FALSE(rack.wet());
    EXPECT_EQ(rack.soundingLevel(3), -1);
    EXPECT_EQ(rack.soundingLevel(7), -1);
    EXPECT_EQ(rack.soundingLevel(9), -1);
}

TEST_F(PunchFxTest, DubEchoReadsThreeDecayingTapsFromTheStereoHistory)
{
    // At 120 BPM, 1/8 is 11025 frames. Place the input impulse one beat into
    // the recording so the rack has reached full wet before the first tap.
    constexpr unsigned tapSpacing = 11025;
    constexpr unsigned impulse = 2 * tapSpacing;
    rack.press(pad(3), punch::Effect::DubEcho, 0);
    std::array<int, 4> peaks{};
    while (cursor < impulse + 5 * tapSpacing)
    {
        left.fill(0);
        right.fill(0);
        if (cursor <= impulse && impulse < cursor + kBlock)
        {
            left[impulse - cursor] = 16000;
            right[impulse - cursor] = -8000;
        }
        rack.beginBlock(120, 44100, 100, true);
        rack.process(left.data(), right.data(), outL.data(), outR.data(), kBlock);
        for (unsigned i = 0; i < kBlock; ++i)
        {
            if (cursor + i <= impulse) continue;
            const unsigned elapsed = cursor + i - impulse;
            const unsigned tap = elapsed / tapSpacing;
            if (tap >= 1 && tap <= peaks.size())
                peaks[tap - 1] = std::max(peaks[tap - 1], int(outL[i]));
            else
                EXPECT_EQ(outL[i], 0);
            EXPECT_NEAR(outL[i], -2 * outR[i], 2);
        }
        cursor += kBlock;
    }
    EXPECT_GT(peaks[0], 300);
    EXPECT_NEAR(peaks[0], 2 * peaks[1], 3);
    EXPECT_NEAR(peaks[1], 2 * peaks[2], 3);
    EXPECT_EQ(peaks[3], 0);
}
}
