#include "ConfigManagerImpl.h"
#include "FileSystemImpl.h"
#include "config.h"
#include "polyassert.h"

void ConfigManagerImpl::init(FileSystem* fileSystem)
{
    this->fileSystem = fileSystem;
}

bool ConfigManagerImpl::readData(int address, void* data, int size)
{
    assert(fileSystem);

    if (!fileSystem->exists(kConfigFilePath))
    {
        return false;
    }

    auto openFileInfo = fileSystem->openFile(kConfigFilePath);
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

    auto openFileInfo = fileSystem->openFile(kConfigFilePath, FileSystem::FileOpenMode::kWriteOpen);
    auto isOpen = std::get<0>(openFileInfo);
    auto file = std::get<1>(openFileInfo);

    if (!isOpen) return false;
    const bool written = fileSystem->writeFile(data, size, file) == size;
    if (written) fileSystem->endFile(file);
    fileSystem->closeFile(file);
    return written;
}
