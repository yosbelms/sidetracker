#include "ExportQueues.h"
#include "MasterFaderPosition.h"
#include "MasterSaturation.h"
#include "audio_test_support.h"
#include <gtest/gtest.h>

namespace
{
using namespace ce::test;

BlockSource dryLeft, dryRight, finalLeft, finalRight;
MasterSaturation saturation;
BlockSink left, right, upstream;
ExportQueues exports;
AudioConnection toLeft(&dryLeft, 0, &saturation, 0);
AudioConnection toRight(&dryRight, 0, &saturation, 1);
AudioConnection toUpstream(&dryLeft, &upstream);
AudioConnection fromLeft(&saturation, 0, &left, 0);
AudioConnection fromRight(&saturation, 1, &right, 0);
auto* const masterL = exports.getQueue(ExportQueues::kLeft, mtAudioExport::stem_master);
auto* const masterR = exports.getQueue(ExportQueues::kRight, mtAudioExport::stem_master);
AudioConnection dryMasterL(&dryLeft, 0, masterL, ExportQueues::kPerfFxInput);
AudioConnection dryMasterR(&dryRight, 0, masterR, ExportQueues::kPerfFxInput);
AudioConnection finalMasterL(&finalLeft, 0, masterL, ExportQueues::kFinalMixInput);
AudioConnection finalMasterR(&finalRight, 0, masterR, ExportQueues::kFinalMixInput);

Samples constant(int16_t sample)
{
    Samples data;
    data.fill(sample);
    return data;
}

void feed(const Samples& l, const Samples& r)
{
    ASSERT_TRUE(dryLeft.push(l));
    ASSERT_TRUE(dryRight.push(r));
    AudioStream::update_all();
    ASSERT_TRUE(left.received && right.received);
    EXPECT_EQ(AudioStream::memory_used, 0);
}

TEST(MasterSaturationTest, CurvesApplyOnceAndDoNotAlterSharedInputs)
{
    const auto a = constant(17000);
    const auto b = constant(-9000);
    saturation.setDrive(0);
    feed(a, b); // Settle Drive before the comparison.
    for (const auto mode : {kHardClip, kSoftClip, kSaturate, kOverdrive, kOverdrive2})
    {
        saturation.setMode(mode);
        Samples expectedLeft, expectedRight;
        int32_t input[AUDIO_BLOCK_SAMPLES];
        std::copy(a.begin(), a.end(), input);
        saturate(expectedLeft.data(), input, mode);
        std::copy(b.begin(), b.end(), input);
        saturate(expectedRight.data(), input, mode);
        feed(a, b);
        EXPECT_EQ(left.samples, expectedLeft) << mode;
        EXPECT_EQ(right.samples, expectedRight) << mode;
        EXPECT_EQ(upstream.samples, a);
    }
}

TEST(MasterSaturationTest, DriveRampsInBothChannelsAndClampsAtTwelveDb)
{
    const auto data = constant(6000);
    saturation.setMode(kHardClip);
    saturation.setDrive(0);
    feed(data, data);
    saturation.setDrive(100);
    feed(data, data);
    EXPECT_EQ(left.samples.front(), 6000);
    EXPECT_GT(left.samples.back(), left.samples.front());
    EXPECT_EQ(left.samples, right.samples);
    feed(data, data);
    EXPECT_NEAR(left.samples.back(), 23886, 1);
    saturation.setDrive(-100);
    feed(data, data);
    EXPECT_NEAR(left.samples.front(), 23886, 1);
    feed(data, data);
    EXPECT_NEAR(left.samples.back(), 1507, 1);
}

TEST(MasterSaturationTest, MaximumSoftClipDriveDoesNotFoldPeaks)
{
    saturation.setMode(kSoftClip);
    saturation.setDrive(24);
    feed(constant(INT16_MAX), constant(INT16_MIN));
    feed(constant(INT16_MAX), constant(INT16_MIN));
    EXPECT_GT(left.samples.front(), 32000);
    EXPECT_LT(right.samples.front(), -32000);
}

TEST(MasterExportTest, SelectsExactlyOneStereoTapAndDrainsTheOther)
{
    const auto lp = constant(3000), rp = constant(-5000);
    const auto lf = constant(700), rf = constant(-800);
    for (const auto mode : {exportPattern, exportPatternSelection, exportSong,
                            exportPatternStems, exportSongStems})
    {
        const bool expectPerf = mode == exportPattern || mode == exportPatternSelection;
        EXPECT_EQ(exportUsesPerfFxTap(mode), expectPerf);
        exports.selectPerfFxTap(exportUsesPerfFxTap(mode));
        masterL->begin();
        masterR->begin();
        ASSERT_TRUE(dryLeft.push(lp));
        ASSERT_TRUE(dryRight.push(rp));
        ASSERT_TRUE(finalLeft.push(lf));
        ASSERT_TRUE(finalRight.push(rf));
        AudioStream::update_all();
        ASSERT_EQ(masterL->available(), 1);
        ASSERT_EQ(masterR->available(), 1);
        EXPECT_EQ(masterL->readBuffer()[0], expectPerf ? lp[0] : lf[0]);
        EXPECT_EQ(masterR->readBuffer()[0], expectPerf ? rp[0] : rf[0]);
        masterL->freeBuffer();
        masterR->freeBuffer();
        masterL->end();
        masterR->end();
        EXPECT_EQ(AudioStream::memory_used, 0);
    }
}

TEST(MasterSettingsTest, DriveRoundTripsAndLegacyOrInvalidDataDefaultsToUnity)
{
    uint8_t reserved[7] = {0, 0, 1, 2, 3, 4, 5};
    EXPECT_EQ(masterSaturationSettings::readDrive(reserved), 0);
    for (int drive = -24; drive <= 24; ++drive)
    {
        masterSaturationSettings::writeDrive(reserved, drive);
        EXPECT_EQ(masterSaturationSettings::readDrive(reserved), drive);
        EXPECT_EQ(reserved[2], 1);
        EXPECT_EQ(reserved[6], 5);
    }
    reserved[1] = 255;
    EXPECT_EQ(masterSaturationSettings::readDrive(reserved), 0);
}

TEST(MasterSettingsTest, PerfFxAndSynthUseTheSameUnityPositionAsReturnFaders)
{
    using namespace masterFaderPosition;
    EXPECT_EQ(mix(0), 0);
    EXPECT_EQ(mix(100), DEFAULT_DELAY_VOLUME);
    EXPECT_EQ(synth(-96, -96, 12), 0);
    EXPECT_EQ(synth(0, -96, 12), mix(100));
    EXPECT_EQ(synth(12, -96, 12), 100);
}

}
