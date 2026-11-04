#include "wavHeaderReader.h"
#include "TrackerImpl.h"

bool readHeader(strWavFileHeader* header, FileSystem::FileHandle file)
{
    return readHeader(header, TrackerImpl::getInstance()->getPlatform()->getFileSystem(), file);
}
