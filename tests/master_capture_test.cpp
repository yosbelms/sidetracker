#include "MasterCapture.h"
#include "fake_devices.h"
#include <array>
#include <gtest/gtest.h>

namespace
{
constexpr char directory[] = "Export/Project/Project_RecordOutput_0001";
constexpr char temporary[] = "Export/Project/Project_RecordOutput_0001/master.tmp";
constexpr char output[] = "Export/Project/Project_RecordOutput_0001/master.wav";

class MasterCaptureTest : public testing::Test
{
  protected:
    ce::test::FakeFileSystem fs;
    MasterCapture capture;
    std::array<int16_t, 4096 * 2> ring{};
    std::array<int16_t, 2048 * 2> signal{};

    void open()
    {
        ASSERT_TRUE(capture.open(fs, directory, ring.data(), ring.size() / 2, 44118));
        for (unsigned i = 0; i < signal.size() / 2; ++i)
        {
            signal[2 * i] = i;
            signal[2 * i + 1] = -int(i);
        }
        capture.start();
    }
};

TEST_F(MasterCaptureTest, WritesOnlyCompleteTakesAndPreservesStereoAcrossWrap)
{
    open();
    for (int block = 0; block < 3; ++block)
    {
        capture.receive(signal.data(), signal.size() / 2);
        capture.drain();
    }
    capture.finish();
    EXPECT_EQ(capture.result(), MasterCapture::Result::Complete);
    EXPECT_FALSE(fs.exists(temporary));
    ASSERT_TRUE(fs.exists(output));
    EXPECT_EQ(fs.openHandles(), 0);

    const auto& wav = fs.files.at(output);
    strWavFileHeader header;
    ASSERT_EQ(wav.size(), sizeof(header) + 3 * sizeof(signal));
    memcpy(&header, wav.data(), sizeof(header));
    EXPECT_EQ(header.numChannels, 2);
    EXPECT_EQ(header.bitsPerSample, 16);
    EXPECT_EQ(header.sampleRate, 44118u);
    EXPECT_EQ(header.subchunk2Size, 3 * sizeof(signal));
    for (unsigned i = 0; i < 3; ++i)
        EXPECT_EQ(memcmp(wav.data() + sizeof(header) + i * sizeof(signal),
                         signal.data(), sizeof(signal)), 0);
}

TEST_F(MasterCaptureTest, ShortWritesAndCancelDiscardPartialFile)
{
    open();
    capture.receive(signal.data(), signal.size() / 2);
    fs.maxWriteSize = 512;
    capture.drain();
    EXPECT_EQ(capture.result(), MasterCapture::Result::FileError);
    EXPECT_FALSE(fs.exists(temporary));
    EXPECT_FALSE(fs.exists(output));
    EXPECT_EQ(fs.openHandles(), 0);

    fs.maxWriteSize = SIZE_MAX;
    open();
    capture.receive(signal.data(), 12);
    capture.cancel();
    EXPECT_EQ(capture.result(), MasterCapture::Result::Cancelled);
    EXPECT_FALSE(fs.exists(temporary));
    EXPECT_FALSE(fs.exists(output));
    EXPECT_EQ(fs.openHandles(), 0);
}

TEST_F(MasterCaptureTest, FailedDiskCommitKeepsTheWavUnpublished)
{
    open();
    capture.receive(signal.data(), 12);
    fs.failCloseWrite = true;
    capture.finish();
    EXPECT_EQ(capture.result(), MasterCapture::Result::FileError);
    EXPECT_FALSE(fs.exists(temporary));
    EXPECT_FALSE(fs.exists(output));
    EXPECT_EQ(fs.openHandles(), 0);
}

TEST_F(MasterCaptureTest, OverrunDiscardsInsteadOfPublishingGaps)
{
    open();
    for (int block = 0; block < 3; ++block)
        capture.receive(signal.data(), signal.size() / 2);
    capture.finish();
    EXPECT_EQ(capture.result(), MasterCapture::Result::Overrun);
    EXPECT_FALSE(fs.exists(temporary));
    EXPECT_FALSE(fs.exists(output));
    EXPECT_EQ(fs.openHandles(), 0);
}
}
