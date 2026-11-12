#include "DesktopStorage.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <strings.h>
#include <stdexcept>
#include <vector>
#ifdef TRACKER_DESKTOP_PLUS
#include <SDL.h>
#include <memory>
#endif

namespace fs = std::filesystem;

DesktopStorage::DesktopStorage(fs::path path) : root(fs::absolute(path).lexically_normal())
{
    fs::create_directories(root);
    root = fs::canonical(root);
#ifdef TRACKER_DESKTOP_PLUS
    const std::unique_ptr<char, decltype(&SDL_free)> basePath(SDL_GetBasePath(), SDL_free);
    if (basePath)
        packagedPatches = fs::path(basePath.get()) / "../SD/Patches";
#endif
}

DesktopStorage::~DesktopStorage()
{
    while (!handles.empty())
        closeFile(*handles.begin(), true);
}

fs::path DesktopStorage::resolve(const char* path) const
{
    auto candidate = root;
    std::error_code error;
    for (const auto& part : fs::path(path).relative_path())
    {
        if (part == "..")
            return {};
        candidate /= part;
        if (fs::is_symlink(candidate, error))
            return {};
    }
    return candidate;
}

bool DesktopStorage::exists(const char* path)
{
    std::error_code ec;
    return fs::exists(resolveForRead(path), ec);
}

fs::path DesktopStorage::resolveForRead(const char* path) const
{
    auto local = resolve(path);
    if (local.empty() || local.extension() != ".patch")
        return local;

    const auto bundledFolder = resolvePackagedPatchFolder(local.parent_path());
    std::error_code ec;
    if (bundledFolder.empty() || fs::exists(local, ec) || ec)
        return local;
    const char* legacyFolder = local.parent_path().filename() == "MINI" ? "9" : "10";
    const auto legacy = resolve((fs::path("Patches") / legacyFolder / local.filename()).c_str());
    if (fs::is_regular_file(legacy, ec))
        return legacy;
    return bundledFolder / local.filename();
}

fs::path DesktopStorage::resolvePackagedPatchFolder(const fs::path& local) const
{
    if (packagedPatches.empty() || local.parent_path() != root / "Patches")
        return {};
    const auto name = local.filename();
    return name == "MINI" || name == "OBER" ? packagedPatches / name : fs::path{};
}

bool DesktopStorage::rename(const char* oldPath, const char* newPath)
{
    std::error_code ec;
    fs::rename(resolve(oldPath), resolve(newPath), ec);
    return !ec;
}

bool DesktopStorage::mkdir(uint8_t, const char* path, bool parents)
{
    auto dir = resolve(path);
    std::error_code ec;
    if (fs::is_directory(dir, ec))
        return true;
    return parents ? fs::create_directories(dir, ec) : fs::create_directory(dir, ec);
}

bool DesktopStorage::remove(const char* path)
{
    auto file = resolve(path);
    std::error_code ec;
    return file != root && fs::remove(file, ec);
}

bool DesktopStorage::removeDirWithFiles(const char* path)
{
    auto dir = resolve(path);
    std::error_code ec;
    return !dir.empty() && dir != root && fs::remove_all(dir, ec) != uintmax_t(-1);
}

bool DesktopStorage::removeOnlyDirFiles(const char* path)
{
    std::error_code ec;
    auto dir = resolve(path);
    if (dir == root)
        return false;
    for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        if (!it->is_directory(ec) && !ec)
            fs::remove(it->path(), ec);
    return !ec;
}

size_t DesktopStorage::getFileSize(const char* path)
{
    std::error_code ec;
    const auto size = fs::file_size(resolveForRead(path), ec);
    return ec ? 0 : size;
}

size_t DesktopStorage::getFileSize(FileHandle file)
{
    if (!isFileOpen(file))
        return 0;
    auto* stream = static_cast<OpenFile*>(file)->stream;
    const auto pos = ftell(stream);
    if (pos < 0 || fseek(stream, 0, SEEK_END))
        return 0;
    const auto size = ftell(stream);
    fseek(stream, pos, SEEK_SET);
    return size < 0 ? 0 : size;
}

size_t DesktopStorage::getFileAvailable(FileHandle file)
{
    if (!isFileOpen(file))
        return 0;
    const auto pos = ftell(static_cast<OpenFile*>(file)->stream);
    const auto size = getFileSize(file);
    return pos >= 0 && size > size_t(pos) ? size - pos : 0;
}

std::tuple<bool, FileSystem::FileHandle> DesktopStorage::openFile(const char* path, FileOpenMode mode,
                                                                 FileHandle* reuse)
{
    const auto filename = mode == kRead ? resolveForRead(path) : resolve(path);
    if (filename.empty())
        return {false, nullptr};
    auto* file = reuse ? static_cast<OpenFile*>(*reuse) : nullptr;
    if (handles.contains(file))
        closeFile(file, false);
    else
        file = nullptr;
    const char* modes[] = {"rb", "r+b", "a+b", "w+b"};
    auto* stream = fopen(filename.c_str(), modes[mode]);
    if (!stream && mode == kWriteOpen && !exists(path))
        stream = fopen(filename.c_str(), "w+b");
    if (!stream)
        return {false, file};
    if (!file)
    {
        file = new OpenFile;
        handles.insert(file);
    }
    file->stream = stream;
    if (reuse)
        *reuse = file;
    return {true, file};
}

bool DesktopStorage::isFileOpen(FileHandle file)
{
    auto* entry = static_cast<OpenFile*>(file);
    return handles.contains(entry) && entry->stream;
}

void DesktopStorage::closeFile(FileHandle file, bool destroy)
{
    auto* entry = static_cast<OpenFile*>(file);
    if (!handles.contains(entry))
        return;
    if (entry->stream)
        fclose(entry->stream);
    entry->stream = nullptr;
    if (destroy)
    {
        handles.erase(entry);
        delete entry;
    }
}

void DesktopStorage::endFile(FileHandle file)
{
    if (isFileOpen(file))
        fflush(static_cast<OpenFile*>(file)->stream);
}

int32_t DesktopStorage::readFile(void* data, size_t length, FileHandle file)
{
    if (!isFileOpen(file)) return -1;
    FILE* stream = static_cast<OpenFile*>(file)->stream;
    const size_t count = fread(data, 1, std::min(length, size_t(INT32_MAX)), stream);
    return ferror(stream) ? -1 : int32_t(count);
}

int32_t DesktopStorage::writeFile(const void* data, size_t length, FileHandle file)
{
    if (!isFileOpen(file)) return -1;
    FILE* stream = static_cast<OpenFile*>(file)->stream;
    const size_t count = fwrite(data, 1, std::min(length, size_t(INT32_MAX)), stream);
    return ferror(stream) ? -1 : int32_t(count);
}

bool DesktopStorage::seekFileCur(int offset, FileHandle file)
{
    return isFileOpen(file) && fseek(static_cast<OpenFile*>(file)->stream, offset, SEEK_CUR) == 0;
}

bool DesktopStorage::seekFile(uint32_t pos, FileHandle file)
{
    return isFileOpen(file) && fseek(static_cast<OpenFile*>(file)->stream, pos, SEEK_SET) == 0;
}

namespace
{
std::vector<std::string> entries(const fs::path& path, const std::function<bool(const fs::directory_entry&)>& filter)
{
    std::vector<std::string> names;
    std::error_code ec;
    for (auto it = fs::directory_iterator(path, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        if (!it->is_symlink(ec) && !ec && it->path().filename().string().front() != '.' && filter(*it))
            names.push_back(it->path().filename().string());
    std::sort(names.begin(), names.end());
    return names;
}

bool matches(const fs::directory_entry& item, FileSystem::enListFileFilter filter)
{
    std::error_code ec;
    if (item.is_directory(ec))
        return filter == FileSystem::enListFileFilter_folder || filter == FileSystem::enListFileFilter_all;
    auto extension = item.path().extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    switch (filter)
    {
        case FileSystem::enListFileFilter_all: return true;
        case FileSystem::enListFileFilter_wave: return extension == ".wav";
        case FileSystem::enListFileFilter_wave_inst: return extension == ".wav" || extension == ".pti";
        case FileSystem::enListFileFilter_firmware: return extension == ".ptf";
        case FileSystem::enListFileFilter_mod: return extension == ".mod" || extension == ".it";
        case FileSystem::enListFileFilter_game: return extension == ".nes";
        default: return false;
    }
}
}

int DesktopStorage::countContainedFiles(const char* path)
{
    return entries(resolve(path), [](const auto&) { return true; }).size();
}

namespace
{
char* copyName(std::string_view name)
{
    auto* copy = new char[name.size() + 1];
    std::memcpy(copy, name.data(), name.size());
    copy[name.size()] = 0;
    return copy;
}
}

int DesktopStorage::createInstrumentsList(const char* path, char** list, uint8_t count, uint32_t* size)
{
    const auto dir = resolve(path);
    std::error_code ec;
    if (!fs::is_directory(dir, ec))
        return kListError;
    int found = 0;
    for (const auto& name : entries(dir, [](const auto& entry)
         {
             return matches(entry, enListFileFilter_wave_inst);
         }))
    {
        if (name.size() < 7 || strcasecmp(name.c_str() + name.size() - 4, ".pti") != 0)
            continue;
        const auto separator = name.find(' ');
        if (separator != 1 && separator != 2)
            continue;
        int slot = 0;
        for (size_t i = 0; i < separator; ++i)
        {
            if (name[i] < '0' || name[i] > '9')
            {
                slot = 0;
                break;
            }
            slot = slot * 10 + name[i] - '0';
        }
        if (slot < 1 || slot > count || list[slot - 1])
            continue;
        list[slot - 1] = copyName(std::string_view(name).substr(separator + 1, name.size() - separator - 5));
        if (size)
            *size += getFileSize((fs::path(path) / name).c_str());
        ++found;
    }
    return found;
}

int DesktopStorage::createFilesList(const char* path, uint8_t start, char** list, uint8_t count,
                                    uint16_t budget, enListFileFilter filter, uint16_t skip,
                                    uint16_t maxFiles, enListFileFilter folderFilter)
{
    const auto dir = resolve(path);
    std::error_code ec;
    if (!fs::is_directory(dir, ec))
        return kListError;
    for (int i = 0; i < count; ++i)
    {
        delete[] list[i];
        list[i] = nullptr;
    }
    auto names = entries(dir, [filter](const auto& entry) { return matches(entry, filter); });
    int out = start;
    const auto append = [&](const std::string& name)
    {
        const auto bytes = name.size() + 1;
        if (out >= count || bytes > budget)
            return false;
        list[out++] = copyName(name);
        budget -= bytes;
        return true;
    };
    for (size_t i = skip; i < names.size() && out < count; ++i)
    {
        const auto& name = names[i];
        if (!fs::is_directory(dir / name, ec))
        {
            if (!append(name))
                break;
            continue;
        }
        const auto files = maxFiles ? entries(dir / name, [folderFilter](const auto& entry)
                                            { return matches(entry, folderFilter); }).size() : 0;
        if (!maxFiles || files <= maxFiles)
        {
            if (!append("/" + name))
                break;
            continue;
        }
        for (size_t offset = 0; offset < files; offset += maxFiles)
            if (!append("/" + name + " (" + std::to_string(offset + 1) + "-" +
                        std::to_string(offset + maxFiles) + ")"))
                return out;
    }
    return out;
}

void DesktopStorage::listDirContents(const char* path, DirContentType type,
                                     std::function<bool(const char*)> filter, char* buffer,
                                     int capacity, int maxName, int& count, char** list)
{
    const int maxEntries = count;
    count = 0;
    const auto local = resolve(path);
    const auto matchesType = [&](const auto& entry) {
        std::error_code ec;
        return entry.is_directory(ec) == (type == kFolders) && !ec &&
               (!filter || filter(entry.path().filename().c_str()));
    };
    auto names = entries(local, matchesType);
    if (type == kFiles)
    {
        const auto bundled = resolvePackagedPatchFolder(local);
        if (!bundled.empty())
        {
            const auto localCount = names.size();
            for (auto& name : entries(bundled, matchesType))
                if (!std::binary_search(names.begin(), names.begin() + localCount, name))
                    names.push_back(std::move(name));
        }
    }
    for (const auto& name : names)
    {
        const int size = name.size() + 1;
        if (size > maxName)
            continue;
        if (count >= maxEntries || size > capacity)
            break;
        std::memcpy(buffer, name.c_str(), size);
        list[count++] = buffer;
        buffer += size;
        capacity -= size;
    }
}
