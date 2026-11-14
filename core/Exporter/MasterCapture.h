#pragma once

#include "wavHeaderReader.h"
#include <atomic>

class MasterCapture
{
  public:
    enum class Result { Recording, Complete, Cancelled, FileError, Overrun };

    // The caller owns the storage, the SD device and the path for the whole session.
    bool open(FileSystem& fs, const char* folder, int16_t* buffer, uint32_t capacityFrames,
              uint32_t rate);
    void start() { accepting.store(true, std::memory_order_release); }
    void stop() { accepting.store(false, std::memory_order_release); }
    void receive(const int16_t* stereo, uint32_t frames);
    void drain();
    void finish();
    void cancel();
    Result result() const { return state; }

  private:
    static constexpr uint32_t kFrameBytes = 2 * sizeof(int16_t);
    static constexpr uint32_t kWriteFrames = 2048;

    void close(Result outcome);
    strWavFileHeader header() const;

    FileSystem* fileSystem = nullptr;
    FileSystem::FileHandle file = nullptr;
    const char* directory = nullptr;
    int16_t* pcm = nullptr;
    uint32_t capacity = 0;
    uint32_t rate = 0;
    uint32_t bytes = 0;
    std::atomic<uint32_t> written{0};
    std::atomic<uint32_t> read{0};
    std::atomic<bool> accepting{false};
    std::atomic<bool> overrun{false};
    Result state = Result::Cancelled;
};
