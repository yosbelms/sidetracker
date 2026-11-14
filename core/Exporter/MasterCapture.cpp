#include "MasterCapture.h"
#include "mtStructs.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

bool MasterCapture::open(FileSystem& fs, const char* folder, int16_t* buffer,
                         uint32_t capacityFrames, uint32_t sampleRate)
{
    if (file || !buffer || !capacityFrames || (capacityFrames & (capacityFrames - 1)) ||
        !fs.isSdCardInserted())
        return false;
    char path[PATCH_SIZE];
    const int length = snprintf(path, sizeof(path), "%s/master.tmp", folder);
    if (length < 0 || size_t(length) >= sizeof(path) || fs.exists(path))
        return false;

    const auto [opened, handle] = fs.openFile(path, FileSystem::kWriteCreateNew);
    if (!opened) return false;
    file = handle;
    fileSystem = &fs;
    directory = folder;
    pcm = buffer;
    capacity = capacityFrames;
    rate = sampleRate;
    bytes = 0;
    written.store(0, std::memory_order_relaxed);
    read.store(0, std::memory_order_relaxed);
    overrun.store(false, std::memory_order_relaxed);
    state = Result::Recording;

    const auto wav = header();
    if (fs.writeFile(&wav, sizeof(wav), file) != sizeof(wav))
    {
        close(Result::FileError);
        return false;
    }
    return true;
}

void MasterCapture::receive(const int16_t* stereo, uint32_t frames)
{
    if (!accepting.load(std::memory_order_acquire)) return;

    const auto head = written.load(std::memory_order_relaxed);
    const auto tail = read.load(std::memory_order_acquire);
    if (!stereo || frames > capacity - (head - tail))
    {
        overrun.store(true, std::memory_order_release);
        stop();
        return;
    }

    const auto offset = head & (capacity - 1);
    const auto first = std::min(frames, capacity - offset);
    memcpy(pcm + 2 * offset, stereo, first * kFrameBytes);
    if (first != frames)
        memcpy(pcm, stereo + 2 * first, (frames - first) * kFrameBytes);
    written.store(head + frames, std::memory_order_release);
}

void MasterCapture::drain()
{
    if (!file) return;
    if (overrun.load(std::memory_order_acquire))
    {
        close(Result::Overrun);
        return;
    }

    const auto tail = read.load(std::memory_order_relaxed);
    const auto queued = written.load(std::memory_order_acquire) - tail;
    if (accepting.load(std::memory_order_relaxed) && queued < kWriteFrames) return;
    const auto offset = tail & (capacity - 1);
    const auto frames = std::min({queued, capacity - offset, kWriteFrames});
    if (!frames) return;

    const uint32_t size = frames * kFrameBytes;
    if (size > UINT32_MAX - 36 - bytes ||
        fileSystem->writeFile(pcm + 2 * offset, size, file) != static_cast<int32_t>(size))
    {
        close(Result::FileError);
        return;
    }
    bytes += size;
    read.store(tail + frames, std::memory_order_release);
}

void MasterCapture::finish()
{
    stop();
    while (file && written.load(std::memory_order_acquire) != read.load(std::memory_order_relaxed))
        drain();
    if (!file) return;
    if (overrun.load(std::memory_order_acquire))
    {
        close(Result::Overrun);
        return;
    }

    const auto wav = header();
    const bool ok = bytes && fileSystem->seekFile(0, file) &&
                    fileSystem->writeFile(&wav, sizeof(wav), file) == sizeof(wav);
    close(ok ? Result::Complete : Result::FileError);
}

void MasterCapture::cancel()
{
    if (file) close(Result::Cancelled);
}

void MasterCapture::close(Result outcome)
{
    stop();
    const bool closed = fileSystem->closeWrittenFile(file);
    if (!closed && outcome == Result::Complete) outcome = Result::FileError;
    file = nullptr;
    char temporary[PATCH_SIZE];
    snprintf(temporary, sizeof(temporary), "%s/master.tmp", directory);
    if (outcome == Result::Complete)
    {
        char output[PATCH_SIZE];
        snprintf(output, sizeof(output), "%s/master.wav", directory);
        if (!fileSystem->rename(temporary, output)) outcome = Result::FileError;
    }
    if (outcome != Result::Complete && fileSystem->isSdCardInserted())
        fileSystem->remove(temporary);
    state = outcome;
}

strWavFileHeader MasterCapture::header() const
{
    static_assert(sizeof(strWavFileHeader) == 44);
    return {0x46464952, bytes + 36, WAVEID, 0x20746d66, 16, wavFormatPCM, 2,
            rate, rate * kFrameBytes, kFrameBytes, 16, 0x61746164, bytes};
}
