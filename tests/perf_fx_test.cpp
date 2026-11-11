#include <gtest/gtest.h>
#include "PerfFx/PerfFx.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <set>

namespace
{
constexpr unsigned kBlock = 128;
constexpr auto pad(unsigned key) { return perffx::owner(perffx::Source::Pads, key); }

class PerfFxTest : public testing::Test
{
  protected:
    std::unique_ptr<perffx::Storage> storage = std::make_unique<perffx::Storage>();
    perffx::Rack rack{*storage};
    std::array<int16_t, kBlock> left{}, right{}, outL{}, outR{};
    unsigned cursor = 0;

    void block(float tempo = 120, uint8_t mix = 100, bool sequencedOnly = false)
    {
        for (unsigned i = 0; i < kBlock; ++i, ++cursor)
        {
            left[i] = static_cast<int16_t>(9000 * std::sin(cursor * 0.036f) + int(cursor % 127) - 63);
            right[i] = static_cast<int16_t>(6000 * std::sin(cursor * 0.057f));
        }
        rack.beginBlock(tempo, 44100, mix, true, sequencedOnly);
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

TEST_F(PerfFxTest, PatternValuesDecodeEveryPresetAndRelease)
{
    using Action = perffx::PatternCommand::Action;
    using Effect = perffx::Effect;
    constexpr Effect order[] = {
        Effect::BeatRepeat, Effect::Stutter, Effect::Scatter, Effect::DubEcho,
        Effect::Pitch, Effect::Reverse, Effect::Scratch, Effect::TapeStop,
        Effect::LowPass, Effect::HighPass, Effect::Crush, Effect::Unison,
    };
    for (unsigned effect = 0; effect < std::size(order); ++effect)
    {
        for (unsigned level = 0; level < perffx::kLevels; ++level)
        {
            const uint8_t value = 1 + effect * 4 + level;
            const auto command = perffx::decodePatternCommand(value);
            ASSERT_EQ(command.action, Action::Press);
            EXPECT_EQ(command.column->effect, order[effect]);
            EXPECT_EQ(command.level, level);
            const auto pad = perffx::patternPadForValue(value);
            EXPECT_EQ(perffx::patternValueForPad(pad), value);
            EXPECT_EQ(perffx::columns[pad % perffx::kColumns].effect, order[effect]);
            EXPECT_EQ(pad / perffx::kColumns, level);
        }
        const auto release = perffx::decodePatternCommand(49 + effect);
        ASSERT_EQ(release.action, Action::Release);
        EXPECT_EQ(release.column->effect, order[effect]);
    }
    EXPECT_EQ(perffx::decodePatternCommand(0).action, Action::AllOff);
    EXPECT_EQ(perffx::decodePatternCommand(61).action, Action::Invalid);
    EXPECT_EQ(perffx::decodePatternCommand(255).action, Action::Invalid);
}

TEST_F(PerfFxTest, PatternLatchesStackAndReleaseIndependentlyOfPads)
{
    rack.sequence(35); // LP3
    rack.sequence(14); // DE2
    rack.sequence(255);
    advance(16);
    EXPECT_EQ(rack.soundingLevel(8), 2);
    EXPECT_EQ(rack.soundingLevel(3), 1);

    rack.press(pad(8), perffx::Effect::LowPass, 0);
    advance(16);
    rack.sequence(35); // A repeated row must not take priority from the pad.
    advance(16);
    EXPECT_EQ(rack.soundingLevel(8), 0);
    rack.release(pad(8));
    advance(16);
    EXPECT_EQ(rack.soundingLevel(8), 2);

    rack.sequence(57); // LP-
    advance(16);
    EXPECT_EQ(rack.soundingLevel(8), -1);
    EXPECT_EQ(rack.soundingLevel(3), 1);
    rack.press(pad(8), perffx::Effect::LowPass, 0);
    rack.sequence(0);
    advance(16);
    EXPECT_EQ(rack.soundingLevel(8), 0);
    EXPECT_EQ(rack.soundingLevel(3), -1);
}

TEST_F(PerfFxTest, OrderedResetKeepsFirstSequencedCommandAndExportSkipsPads)
{
    rack.sequence(35);
    advance(4);
    rack.reset();
    rack.press(pad(8), perffx::Effect::LowPass, 0);
    rack.sequence(5); // CT1 on the first row after reset
    block(120, 100, true);
    EXPECT_EQ(rack.soundingLevel(8), -1);
    EXPECT_EQ(rack.soundingLevel(1), 0);
}

TEST_F(PerfFxTest, EachPresetProcessesStereoAndHasADistinctResult)
{
    for (unsigned col = 0; col < perffx::kColumns; ++col)
    {
        SCOPED_TRACE(perffx::columns[col].name);
        std::set<uint32_t> signatures;
        for (uint8_t row = 0; row < perffx::kLevels; ++row)
        {
            rack.reset();
            cursor = 0;
            // Fill enough history for the longest Dub Echo division at 120 BPM.
            advance(320);
            rack.press(pad(0), perffx::columns[col].effect, row);
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

TEST_F(PerfFxTest, LatestRowFallsBackAndFourthColumnWaitsForRelease)
{
    advance(8);
    rack.press(pad(0), perffx::Effect::BeatRepeat, 0);
    rack.press(pad(1), perffx::Effect::Stutter, 1);
    rack.press(pad(2), perffx::Effect::Scatter, 2);
    rack.press(pad(3), perffx::Effect::DubEcho, 3);
    advance(8);
    EXPECT_EQ(rack.soundingLevel(0), 0);
    EXPECT_EQ(rack.soundingLevel(1), 1);
    EXPECT_EQ(rack.soundingLevel(2), 2);
    EXPECT_EQ(rack.soundingLevel(3), -1);

    rack.press(pad(12), perffx::Effect::BeatRepeat, 1);
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

TEST_F(PerfFxTest, ZeroMixAndIdlePreservePcmAndResetClearsHistory)
{
    advance(8);
    EXPECT_EQ(outL, left);
    EXPECT_EQ(outR, right);
    rack.press(pad(0), perffx::Effect::Reverse, 3);
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
    rack.press(pad(0), perffx::Effect::Reverse, 3);
    rack.beginBlock(120, 44100, 100, true);
    EXPECT_EQ(rack.soundingLevel(5), -1);
    rack.process(left.data(), right.data(), nullptr, nullptr, 64);
    block();
    EXPECT_EQ(rack.soundingLevel(5), 3);
}

TEST_F(PerfFxTest, OwnerGroupsAndOverflowFailDry)
{
    rack.press(pad(1), perffx::Effect::Stutter, 0);
    rack.press(perffx::owner(perffx::Source::Sequencer, 1), perffx::Effect::PitchUp, 3);
    advance(8);
    rack.releaseSource(perffx::Source::Pads);
    advance(16);
    EXPECT_EQ(rack.soundingLevel(1), -1);
    EXPECT_EQ(rack.soundingLevel(4), 3);
    for (unsigned i = 0; i < 128; ++i) rack.press(pad(0), perffx::Effect::Loop, 0);
    block();
    EXPECT_FALSE(rack.wet());
    EXPECT_EQ(rack.soundingLevel(4), -1);
    EXPECT_EQ(outL, left);
}

TEST_F(PerfFxTest, MixChangesWithNoPunchesKeepThePlaybackDry)
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

TEST_F(PerfFxTest, HistoryWrapAndExtremeTempoRemainBounded)
{
    advance(perffx::kHistoryFrames / kBlock + 2, 5);
    rack.press(pad(0), perffx::Effect::Scratch, 3);
    rack.press(pad(1), perffx::Effect::Scatter, 3);
    rack.press(pad(2), perffx::Effect::DubEcho, 3);
    advance(perffx::kHistoryFrames / kBlock + 2, 800);
    EXPECT_EQ(rack.soundingLevel(6), 3);
    EXPECT_EQ(rack.soundingLevel(2), 3);
    EXPECT_EQ(rack.soundingLevel(3), 3);
    rack.releaseSource(perffx::Source::Pads);
    advance(16, 5);
    EXPECT_FALSE(rack.wet());
}

TEST_F(PerfFxTest, TapeStopSlowsAcrossHistoryWrapThenStaysSilentUntilRelease)
{
    advance(perffx::kHistoryFrames / kBlock);
    rack.press(pad(0), perffx::Effect::TapeStop, 0);
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

TEST_F(PerfFxTest, LowPassClosesOverABarToItsLabeledCutoff)
{
    rack.press(pad(0), perffx::Effect::LowPass, 3);
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

TEST_F(PerfFxTest, HighPassPresetsReachTheirCutoffsWithoutHighFrequencyBoost)
{
    constexpr float cutoffs[] = {160, 400, 900, 1800};
    constexpr float radiansPerSample = 6.28318531f / 44100;
    for (uint8_t row = 0; row < perffx::kLevels; ++row)
    {
        SCOPED_TRACE(cutoffs[row]);
        rack.reset();
        rack.beginBlock(120, 44100, 100, true);
        rack.press(pad(9), perffx::Effect::HighPass, row);
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

    rack.press(pad(8), perffx::Effect::LowPass, 1);
    block();
    EXPECT_EQ(rack.soundingLevel(8), 1);
    EXPECT_EQ(rack.soundingLevel(9), 3);
    rack.releaseSource(perffx::Source::Pads);
    advance(16);
    EXPECT_EQ(outL, left);
    EXPECT_EQ(outR, right);
}

TEST_F(PerfFxTest, CrushPresetsHoldAndQuantizeBothPolaritiesSymmetrically)
{
    constexpr unsigned periods[] = {4, 8, 16, 32};
    constexpr int steps[] = {64, 256, 1024, 4096};
    for (uint8_t row = 0; row < perffx::kLevels; ++row)
    {
        SCOPED_TRACE(static_cast<unsigned>(row));
        rack.reset();
        block();
        rack.press(pad(10), perffx::Effect::Crush, row);
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

TEST_F(PerfFxTest, RetiredEffectIdsDoNotTriggerReplacementColumns)
{
    advance(8);
    rack.press(pad(0), static_cast<perffx::Effect>(4), 0);
    rack.press(pad(1), static_cast<perffx::Effect>(8), 0);
    rack.press(pad(2), static_cast<perffx::Effect>(10), 0);
    rack.press(pad(3), static_cast<perffx::Effect>(18), 0);
    block();
    EXPECT_FALSE(rack.wet());
    EXPECT_EQ(rack.soundingLevel(3), -1);
    EXPECT_EQ(rack.soundingLevel(7), -1);
    EXPECT_EQ(rack.soundingLevel(9), -1);
}

TEST_F(PerfFxTest, DubEchoReadsThreeDecayingTapsFromTheStereoHistory)
{
    // At 120 BPM, 1/8 is 11025 frames. Place the input impulse one beat into
    // the recording so the rack has reached full wet before the first tap.
    constexpr unsigned tapSpacing = 11025;
    constexpr unsigned impulse = 2 * tapSpacing;
    rack.press(pad(3), perffx::Effect::DubEcho, 0);
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
