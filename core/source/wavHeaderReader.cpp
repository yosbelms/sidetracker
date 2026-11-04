#include "wavHeaderReader.h"
#include <climits>

bool readHeader(strWavFileHeader* header, FileSystem::FileHandle file, FileSystem& fs)
{
    *header = {};
    strWavFileHeader parsed{};
    struct Chunk
    {
        uint32_t id;
        uint32_t size;
    } chunk{};

    if (fs.readFile(&parsed, 12, file) != 12 || parsed.chunkId != 0x46464952 ||
        parsed.waveId != WAVEID)
        return false;

    auto findChunk = [&](uint32_t id)
    {
        for (int count = 0; count < 10; ++count)
        {
            if (fs.readFile(&chunk, sizeof(chunk), file) != sizeof(chunk) ||
                chunk.size > fs.getFileAvailable(file))
                return false;
            if (chunk.id == id)
                return true;
            const uint64_t padded = uint64_t(chunk.size) + (chunk.size & 1);
            if (padded > INT_MAX || !fs.seekFileCur(int(padded), file))
                return false;
        }
        return false;
    };

    if (!findChunk(0x20746d66) || chunk.size < 16 ||
        fs.readFile(&parsed.AudioFormat, 16, file) != 16)
        return false;
    parsed.subchunk1Id = chunk.id;
    parsed.subchunk1Size = chunk.size;
    const uint64_t extra = uint64_t(chunk.size) - 16 + (chunk.size & 1);
    if (extra > INT_MAX || !fs.seekFileCur(int(extra), file) || !findChunk(0x61746164))
        return false;
    parsed.subchunk2Id = chunk.id;
    parsed.subchunk2Size = chunk.size;
    *header = parsed;
    return true;
}
