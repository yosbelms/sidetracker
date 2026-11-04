#include "SdStream.h"
#include "fake_devices.h"
#include "wav_fixtures.h"
#include "wavHeaderReader.h"
#include <gtest/gtest.h>
#include <array>

using namespace ce::test;

class StreamTest : public testing::Test
{
  protected:
    void SetUp() override { stream.init(&fs, buffers.data(), int(buffers.size())); }
    void TearDown() override
    {
        stream.endCurrentStream();
        EXPECT_EQ(fs.openHandles(), 0);
    }
    bool start(const char* path = "sample.wav")
    {
        return stream.startStream(path, &header, 0, 65535, &position, &end, &type);
    }
    FakeFileSystem fs;
    SdStream stream;
    alignas(4) std::array<uint8_t, 5 * 64> buffers{};
    strWavFileHeader header{};
    uint32_t position = 0, end = 0;
    uint8_t type = 255;
};

TEST_F(StreamTest, MissingFileDoesNotStartOrLeakAHandle)
{
    EXPECT_FALSE(start());
    EXPECT_FALSE(stream.isStreamActive());
    EXPECT_EQ(fs.openHandles(), 0);
}

TEST_F(StreamTest, ShortPathIsRejectedWithoutReadingBeforeItsStart)
{
    fs.files["x"] = {1, 2, 3};
    EXPECT_FALSE(start("x"));
    EXPECT_EQ(fs.openHandles(), 0);
}

TEST_F(StreamTest, InvalidWaveReleasesItsHandle)
{
    fs.files["sample.wav"] = {1, 2, 3, 4};
    EXPECT_FALSE(start());
    EXPECT_EQ(fs.openHandles(), 0);
}

TEST_F(StreamTest, InvalidChannelOrSampleSizeDoesNotDivideByZero)
{
    auto wav = makeWav({1, 2});
    wav[22] = 0;
    fs.files["sample.wav"] = wav;
    EXPECT_FALSE(start());
    wav = makeWav({1, 2});
    wav[34] = 0;
    fs.files["sample.wav"] = wav;
    EXPECT_FALSE(start());
}

TEST_F(StreamTest, StreamsEverySampleAcrossBufferRotationsAndPartialReads)
{
    std::vector<int16_t> samples(1000);
    for (size_t i = 0; i < samples.size(); ++i) samples[i] = int16_t(i * 29 - 14000);
    const auto wav = makeWav(samples);
    fs.files["sample.wav"] = wav;
    ASSERT_TRUE(start());
    EXPECT_EQ(position, 0u);
    EXPECT_EQ(end, samples.size() * 2);
    EXPECT_EQ(type, 0);
    std::vector<uint8_t> received;
    for (int iteration = 0; iteration < 500 && stream.isStreamActive(); ++iteration)
    {
        stream.update();
        uint8_t bytes[37];
        const int count = stream.readFromStream(sizeof(bytes), bytes);
        ASSERT_GE(count, 0);
        ASSERT_LE(count, int(sizeof(bytes)));
        received.insert(received.end(), bytes, bytes + count);
    }
    EXPECT_FALSE(stream.isStreamActive());
    EXPECT_EQ(received, (std::vector<uint8_t>{wav.begin() + 44, wav.end()}));
}

TEST_F(StreamTest, StopAndRestartReusesTheHandleAndBeginsAtTheNewFile)
{
    fs.files["sample.wav"] = makeWav({111, 222});
    ASSERT_TRUE(start());
    stream.endCurrentStream();
    EXPECT_FALSE(stream.isStreamActive());
    fs.files["second.wav"] = makeWav({-333, 444});
    ASSERT_TRUE(start("second.wav"));
    int16_t samples[2];
    ASSERT_EQ(stream.readFromStream(sizeof(samples), samples), int(sizeof(samples)));
    EXPECT_EQ(samples[0], -333);
    EXPECT_EQ(samples[1], 444);
}

TEST_F(StreamTest, SimulatedSdLatencyOccursDuringRefill)
{
    fs.timer = &testClock();
    fs.readDelayUs = 2000;
    fs.files["sample.wav"] = makeWav(std::vector<int16_t>(1000, 123));
    ASSERT_TRUE(start());
    const uint64_t before = testClock().timeUs;
    stream.update();
    EXPECT_GT(testClock().timeUs, before);
    EXPECT_GT(fs.readCalls, 0);
}
