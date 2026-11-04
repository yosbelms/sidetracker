#pragma once
#include "Platform.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace ce::test
{
class FakeTimer final : public Timer
{
  public:
    uint32_t millis() override { return timeUs / 1000; }
    uint32_t micros() override { return uint32_t(timeUs); }
    void delay(int ms) override { advanceUs(uint64_t(ms) * 1000); }
    uint32_t clockCycles() override { return uint32_t(timeUs * 180); }
    uint32_t clockCyclesPerUs() override { return 180; }
    void advanceUs(uint64_t us) { timeUs += us; }
    uint64_t timeUs = 0;
};

inline FakeTimer& testClock()
{
    static FakeTimer timer;
    return timer;
}

// File contents, positions and handle reuse follow FileSystem's byte-based API.
// Fault injection lets consumers exercise missing cards, short reads and delays.
class FakeFileSystem final : public FileSystem
{
    struct Handle
    {
        std::vector<uint8_t>* bytes;
        size_t position = 0;
        bool open = true;
        bool writable = false;
    };
    std::vector<std::unique_ptr<Handle>> handles;

  public:
    std::map<std::string, std::vector<uint8_t>> files;
    std::set<std::string> directories;
    bool inserted = true;
    bool failOpen = false;
    bool failRead = false;
    size_t maxReadSize = std::numeric_limits<size_t>::max();
    FakeTimer* timer = nullptr;
    uint32_t readDelayUs = 0;
    int readCalls = 0;
    int openHandles() const
    {
        return int(std::count_if(handles.begin(), handles.end(), [](const auto& h) { return h->open; }));
    }

    bool isSdCardInserted() override { return inserted; }
    void setMountEventCallback(CardStateChangeCallback) override {}
    bool exists(const char* path) override { return files.count(path) || directories.count(path); }
    bool rename(const char* from, const char* to) override
    {
        if (exists(to)) return false;
        auto node = files.extract(from);
        if (node.empty()) return false;
        node.key() = to;
        files.insert(std::move(node));
        return true;
    }
    bool mkdir(uint8_t, const char* path, bool = true) override { directories.insert(path); return true; }
    bool remove(const char* path) override { return files.erase(path) != 0; }
    bool removeDirWithFiles(const char*) override { return false; }
    bool removeOnlyDirFiles(const char*) override { return false; }
    size_t getFileSize(const char* path) override
    {
        const auto it = files.find(path);
        return it == files.end() ? 0 : it->second.size();
    }
    size_t getFileSize(FileHandle file) override { return static_cast<Handle*>(file)->bytes->size(); }
    size_t getFileAvailable(FileHandle file) override
    {
        auto* h = static_cast<Handle*>(file);
        return h->position < h->bytes->size() ? h->bytes->size() - h->position : 0;
    }
    std::tuple<bool, FileHandle> openFile(const char* path, FileOpenMode mode = kRead,
                                         FileHandle* reuse = nullptr) override
    {
        if (!inserted || failOpen || (mode == kRead && !files.count(path))) return {false, nullptr};
        auto& bytes = files[path];
        if (mode == kWriteCreateNew) bytes.clear();
        Handle* h;
        if (reuse && *reuse)
            h = static_cast<Handle*>(*reuse);
        else
        {
            handles.emplace_back(new Handle);
            h = handles.back().get();
        }
        *h = {&bytes, mode == kWriteAppend ? bytes.size() : 0, true, mode != kRead};
        if (reuse) *reuse = h;
        return {true, h};
    }
    int32_t readFile(void* destination, size_t length, FileHandle file) override
    {
        ++readCalls;
        if (timer) timer->advanceUs(readDelayUs);
        if (!inserted || failRead || !isFileOpen(file)) return -1;
        auto* h = static_cast<Handle*>(file);
        const size_t count = std::min({length, getFileAvailable(file), maxReadSize, size_t(INT32_MAX)});
        if (count) std::memcpy(destination, h->bytes->data() + h->position, count);
        h->position += count;
        return int32_t(count);
    }
    int32_t writeFile(const void* source, size_t length, FileHandle file) override
    {
        if (!inserted || !isFileOpen(file)) return -1;
        auto* h = static_cast<Handle*>(file);
        if (!h->writable) return -1;
        length = std::min(length, size_t(INT32_MAX));
        h->bytes->resize(std::max(h->bytes->size(), h->position + length));
        if (length) std::memcpy(h->bytes->data() + h->position, source, length);
        h->position += length;
        return int32_t(length);
    }
    bool seekFileCur(int offset, FileHandle file) override
    {
        if (!isFileOpen(file)) return false;
        auto* h = static_cast<Handle*>(file);
        const int64_t position = int64_t(h->position) + offset;
        if (position < 0 || uint64_t(position) > h->bytes->size()) return false;
        h->position = size_t(position);
        return true;
    }
    bool seekFile(uint32_t position, FileHandle file) override
    {
        if (!isFileOpen(file) || position > getFileSize(file)) return false;
        static_cast<Handle*>(file)->position = position;
        return true;
    }
    void closeFile(FileHandle file, bool = true) override
    {
        if (file) static_cast<Handle*>(file)->open = false;
    }
    void endFile(FileHandle file) override { closeFile(file); }
    bool isFileOpen(FileHandle file) override { return file && static_cast<Handle*>(file)->open; }
    bool enableFastSeek(FileHandle, uint32_t*, uint32_t, uint32_t*, uint32_t*) override { return false; }
    bool readSectorsAsync(uint8_t*, uint32_t, uint32_t) override { return false; }
    bool pollAsyncRead(bool*) override { return false; }
    bool waitAsyncRead() override { return false; }
    bool isAsyncReadBusy() override { return false; }
    int createInstrumentsList(const char*, char**, uint8_t, uint32_t*) override { return kListError; }
    int countContainedFiles(const char*) override { return kListError; }
    int createFilesList(const char*, uint8_t, char**, uint8_t, uint16_t,
                        enListFileFilter, uint16_t, uint16_t, enListFileFilter) override { return kListError; }
    void listDirContents(const char*, DirContentType, std::function<bool(const char*)>,
                         char*, int, int, int& count, char**) override { count = kListError; }
    int readNbFiles(const char*) override { return kListError; }
    bool nesDir_open(const char*) override { return false; }
    bool nesDir_readItem(char*, uint8_t*, uint8_t*, uint32_t*) override { return false; }
    void nesDir_close() override {}
};
}  // namespace ce::test
