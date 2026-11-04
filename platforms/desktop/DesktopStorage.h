#pragma once

#include "Platform.h"

#include <filesystem>
#include <unordered_set>

class DesktopStorage final : public FileSystem
{
public:
    explicit DesktopStorage(std::filesystem::path root);
    ~DesktopStorage();

    bool isSdCardInserted() override { return true; }
    void setMountEventCallback(CardStateChangeCallback) override {}
    bool exists(const char* path) override;
    bool rename(const char* oldPath, const char* newPath) override;
    bool mkdir(uint8_t, const char* path, bool parents) override;
    bool remove(const char* path) override;
    bool removeDirWithFiles(const char* path) override;
    bool removeOnlyDirFiles(const char* path) override;
    size_t getFileSize(const char* path) override;
    size_t getFileSize(FileHandle file) override;
    size_t getFileAvailable(FileHandle file) override;
    std::tuple<bool, FileHandle> openFile(const char* path, FileOpenMode mode, FileHandle* reuse) override;
    size_t readFile(void* data, size_t length, FileHandle file) override;
    size_t writeFile(const void* data, size_t length, FileHandle file) override;
    bool seekFileCur(int offset, FileHandle file) override;
    bool seekFile(uint32_t pos, FileHandle file) override;
    void closeFile(FileHandle file, bool destroy) override;
    void endFile(FileHandle file) override;
    bool isFileOpen(FileHandle file) override;
    bool enableFastSeek(FileHandle, uint32_t*, uint32_t, uint32_t*, uint32_t*) override { return false; }
    bool readSectorsAsync(uint8_t*, uint32_t, uint32_t) override { return false; }
    bool pollAsyncRead(bool*) override { return false; }
    bool waitAsyncRead() override { return false; }
    bool isAsyncReadBusy() override { return false; }
    int createInstrumentsList(const char*, char**, uint8_t, uint32_t*) override;
    int countContainedFiles(const char*) override;
    int createFilesList(const char*, uint8_t, char**, uint8_t, uint16_t,
                        enListFileFilter, uint16_t, uint16_t, enListFileFilter) override;
    void listDirContents(const char*, DirContentType, std::function<bool(const char*)>,
                         char*, int, int, int&, char**) override;
    int readNbFiles(const char* path) override { return countContainedFiles(path); }
    bool nesDir_open(const char*) override { return false; }
    bool nesDir_readItem(char*, uint8_t*, uint8_t*, uint32_t*) override { return false; }
    void nesDir_close() override {}

private:
    struct OpenFile { FILE* stream = nullptr; };
    std::filesystem::path resolve(const char*) const;
    std::filesystem::path root;
    std::unordered_set<OpenFile*> handles;
};
