#include "ConfigManagerImpl.h"
#include "FileSystemImpl.h"
#include "SdPaths.h"
#include "config.h"
#include "polyassert.h"

void ConfigManagerImpl::init(FileSystem* fileSystem)
{
    this->fileSystem = fileSystem;
}

bool ConfigManagerImpl::readData(int address, void* data, int size)
{
    assert(fileSystem);

    std::string cfgPath = SdPaths::configFilePath(fileSystem);
    const char* readPath = cfgPath.c_str();
    if (!fileSystem->exists(readPath))
    {
        if (!fileSystem->exists(kConfigFilePath))
        {
            return false;
        }
        readPath = kConfigFilePath;
    }

    auto openFileInfo = fileSystem->openFile(readPath);
    auto isOpen = std::get<0>(openFileInfo);
    auto file = std::get<1>(openFileInfo);

    if (isOpen)
    {
        const bool read = fileSystem->seekFile(address, file) &&
                          fileSystem->readFile(data, size, file) == size;
        fileSystem->closeFile(file);
        return read;
    }

    return false;
}

bool ConfigManagerImpl::writeConfig(void* data, int size)
{
    assert(fileSystem);

    SdPaths::ensureDir(fileSystem, SdPaths::systemRoot(fileSystem));
    std::string cfgPath = SdPaths::configFilePath(fileSystem);
    auto openFileInfo =
      fileSystem->openFile(cfgPath.c_str(), FileSystem::FileOpenMode::kWriteOpen);
    auto isOpen = std::get<0>(openFileInfo);
    auto file = std::get<1>(openFileInfo);

    if (!isOpen) return false;
    const bool written = fileSystem->writeFile(data, size, file) == size;
    if (written) fileSystem->endFile(file);
    fileSystem->closeFile(file);
    return written;
}
