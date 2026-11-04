#include "mtStructs.h"
#include "padToBeatNoteConverter.h"
#include "scales.h"
#include "util_string.h"
#include "util_math.h"
#include <gtest/gtest.h>
#include <array>

TEST(Scales, ChromaticAcceptsEveryMidiNoteForEveryRoot)
{
    for (int root = 0; root < 12; ++root)
        for (int note = 0; note < 128; ++note)
            EXPECT_TRUE(isInScale(note, root, 0));
}

TEST(Scales, MajorAndMinorHaveTheExpectedIntervals)
{
    const std::array<int, 12> major{1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1};
    const std::array<int, 12> minor{1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0};
    for (int note = 0; note < 12; ++note)
    {
        EXPECT_EQ(isInScale(note + 60, 0, 2), major[note]);
        EXPECT_EQ(isInScale(note + 60, 0, 1), minor[note]);
    }
}

TEST(Scales, TransposingTheRootTransposesMembership)
{
    for (int scale = 0; scale <= MAX_SCALE; ++scale)
        for (int interval = 0; interval < 12; ++interval)
            EXPECT_EQ(isInScale(60 + interval, 0, scale), isInScale(65 + interval, 5, scale));
}

TEST(BeatSlices, AllPadsRoundTripThroughTheirNoteNumbers)
{
    for (int pad = 0; pad < 48; ++pad)
        EXPECT_EQ(padToBeatNoteConverter.getPadForNote(padToBeatNoteConverter.getNoteFromPad(pad)), pad);
}

TEST(BeatSlices, OutOfRangeNotesAndPadsClampToTheGrid)
{
    EXPECT_EQ(padToBeatNoteConverter.getNoteFromPad(255), FIRST_BEAT_SLICE_NOTE + 47);
    EXPECT_EQ(padToBeatNoteConverter.getPadForNote(0), 0);
    EXPECT_EQ(padToBeatNoteConverter.getPadForNote(255), 47);
}

TEST(Strings, NaturalSortingOrdersNumbersAndIgnoresCase)
{
    EXPECT_TRUE(compareStrings("sample2.wav", "sample10.wav"));
    EXPECT_FALSE(compareStrings("sample10.wav", "sample2.wav"));
    EXPECT_TRUE(compareStrings("BASS", "bass"));
    EXPECT_TRUE(compareStrings("", "bass"));
    EXPECT_FALSE(compareStrings("bass", ""));
}

TEST(Strings, BoundedCopyTerminatesWithoutOverwritingTheNextByte)
{
    char destination[5] = {'x', 'x', 'x', 'x', '!'};
    polystrncpy(destination, "long name", 4);
    EXPECT_STREQ(destination, "lon");
    EXPECT_EQ(destination[4], '!');
}

TEST(Random, SeededSequenceIsReproducibleAndWithinRequestedBounds)
{
    std::array<int32_t, 64> first;
    utilMathRandom::srandom(1234);
    for (auto& value : first) value = utilMathRandom::random(-20, 50);
    utilMathRandom::srandom(1234);
    for (auto value : first)
    {
        EXPECT_GE(value, -20);
        EXPECT_LT(value, 50);
        EXPECT_EQ(utilMathRandom::random(-20, 50), value);
    }
}
