#include "fake_devices.h"
#include "wav_fixtures.h"
#include "wavHeaderReader.h"
#include <gtest/gtest.h>
#include <cstdlib>
#include <fstream>
#include <iterator>

using namespace ce::test;

class WavTest : public testing::Test
{
  protected:
    bool parse(const std::vector<uint8_t>& bytes)
    {
        fs.files["sample.wav"] = bytes;
        auto [opened, handle] = fs.openFile("sample.wav");
        EXPECT_TRUE(opened);
        file = handle;
        return readHeader(&header, file, fs);
    }
    FakeFileSystem fs;
    FileSystem::FileHandle file = nullptr;
    strWavFileHeader header{};
};

TEST_F(WavTest, GoldenFixtureHasExpectedFormatAndPositionsAtAudio)
{
    const char* directory = std::getenv("CE_FIXTURES");
    ASSERT_NE(directory, nullptr);
    std::ifstream fixture(std::string(directory) + "/mono.hex");
    ASSERT_TRUE(fixture.good());
    std::vector<uint8_t> bytes;
    unsigned byte;
    while (fixture >> std::hex >> byte) bytes.push_back(uint8_t(byte));
    ASSERT_TRUE(parse(bytes));
    EXPECT_EQ(header.numChannels, 1);
    EXPECT_EQ(header.sampleRate, 44100u);
    EXPECT_EQ(header.bitsPerSample, 16);
    EXPECT_EQ(header.subchunk2Size, 8u);
    int16_t samples[4];
    ASSERT_EQ(fs.readFile(samples, sizeof(samples), file), sizeof(samples));
    EXPECT_EQ(samples[0], 0);
    EXPECT_EQ(samples[1], 32767);
    EXPECT_EQ(samples[2], -32768);
    EXPECT_EQ(samples[3], -1);
}

TEST_F(WavTest, StereoHeaderReportsInterleavedFrameSize)
{
    ASSERT_TRUE(parse(makeWav({100, -100, 200, -200}, 2)));
    EXPECT_EQ(header.numChannels, 2);
    EXPECT_EQ(header.blockAlign, 4);
    EXPECT_EQ(header.byteRate, 176400u);
    EXPECT_EQ(fs.getFileAvailable(file), 8u);
}

TEST_F(WavTest, SkipsOddUnknownChunkAndItsPadding)
{
    auto bytes = makeWav({123});
    const std::vector<uint8_t> junk = {'J', 'U', 'N', 'K', 1, 0, 0, 0, 99, 0};
    bytes.insert(bytes.begin() + 12, junk.begin(), junk.end());
    bytes[4] += uint8_t(junk.size());
    ASSERT_TRUE(parse(bytes));
    EXPECT_EQ(fs.getFileAvailable(file), 2u);
    int16_t sample = 0;
    fs.readFile(&sample, sizeof(sample), file);
    EXPECT_EQ(sample, 123);
}

TEST_F(WavTest, AcceptsExtendedFormatChunk)
{
    auto bytes = makeWav({123});
    bytes[16] = 18;
    bytes.insert(bytes.begin() + 36, {0, 0});
    bytes[4] += 2;
    ASSERT_TRUE(parse(bytes));
    EXPECT_EQ(header.subchunk1Size, 18u);
    EXPECT_EQ(fs.getFileAvailable(file), 2u);
}

TEST_F(WavTest, EveryTruncatedPrefixIsRejectedAndClearsTheHeader)
{
    const auto bytes = makeWav({1, 2});
    for (size_t length = 0; length < bytes.size(); ++length)
    {
        SCOPED_TRACE(length);
        header.chunkId = 0xdeadbeef;
        EXPECT_FALSE(parse({bytes.begin(), bytes.begin() + length}));
        EXPECT_EQ(header.chunkId, 0u);
        EXPECT_EQ(header.subchunk2Size, 0u);
    }
}

TEST_F(WavTest, RejectsInvalidRiffAndWaveIdentifiers)
{
    auto bytes = makeWav({1});
    bytes[0] = 'X';
    EXPECT_FALSE(parse(bytes));
    bytes = makeWav({1});
    bytes[8] = 'X';
    EXPECT_FALSE(parse(bytes));
}

TEST_F(WavTest, RejectsFormatShorterThanSixteenBytes)
{
    auto bytes = makeWav({1});
    bytes[16] = 12;
    EXPECT_FALSE(parse(bytes));
}

TEST_F(WavTest, RejectsShortDeviceReads)
{
    fs.maxReadSize = 4;
    EXPECT_FALSE(parse(makeWav({1})));
    EXPECT_EQ(header.chunkId, 0u);
}

TEST_F(WavTest, BoundedScanAcceptsFormatAsTheTenthChunk)
{
    auto bytes = makeWav({1});
    const std::vector<uint8_t> junk = {'J', 'U', 'N', 'K', 0, 0, 0, 0};
    for (int i = 0; i < 9; ++i) bytes.insert(bytes.begin() + 12, junk.begin(), junk.end());
    bytes[4] += 72;
    EXPECT_TRUE(parse(bytes));
    bytes.insert(bytes.begin() + 12, junk.begin(), junk.end());
    bytes[4] += 8;
    EXPECT_FALSE(parse(bytes));
}
