#include "wavHeaderReader.h"
#include "TrackerImpl.h"

void readHeader(strWavFileHeader* header, FileSystem::FileHandle file)
{
    readHeader(header, file, *TrackerImpl::getInstance()->getPlatform()->getFileSystem());
}
