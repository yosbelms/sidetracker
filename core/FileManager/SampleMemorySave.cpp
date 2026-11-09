#include "SampleMemorySave.h"

#include "fileManager.h"
#include "fileManagerDefs.h"
#include "fileTransfer.h"
#include "mtAudioEngine.h"
#include "mtEffect.h"
#include "VoiceStreamManager.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace
{
constexpr size_t kEditPathSize = WORKSPACE_INSTRUMENTS_FORMAT_MAX_LENGTH + sizeof(".undo0");

void instrumentPath(char* path, int slot)
{
    snprintf(path, kEditPathSize, cWorkspaceInstrumentFileFormatNew, slot + 1);
}

void backupPath(char* path, int slot, bool copy)
{
    instrumentPath(path, slot);
    const size_t length = strlen(path);
    snprintf(path + length, kEditPathSize - length, ".undo%d", int(copy));
}

constexpr char kEditTemp[] = "Workspace/instruments/edit.tmp";
constexpr char kUndoTemp[] = "Workspace/instruments/undo.tmp";

bool shouldStream(const strInstrument& instrument, bool stereo)
{
    return instrument.sample.length * (stereo ? 4 : 2) >= kStreamThresholdBytes &&
           isPlayModeStreamable(instrument.playMode);
}

bool fits(int slot, uint32_t bytes)
{
    const uint32_t current = newFileManager.getSampleMemorySize(slot, slot);
    return mtProject.used_memory - current + bytes <=
           std::min<uint32_t>(mtProject.max_memory, EDIT_BUFF_OFFSET);
}

bool readInstrument(FileSystem* fs, const char* path, strInstrument& instrument,
                    uint32_t& offset, bool& stereo)
{
    auto [opened, file] = fs->openFile(path, FileSystem::kRead);
    if (!opened) return false;
    strInstrumentFile header = {};
    const uint32_t size = fs->getFileSize(file);
    const auto headerBytes = std::min<uint32_t>(size, sizeof(header));
    const bool read = fs->readFile(&header, headerBytes, file) == int32_t(headerBytes);
    fs->closeFile(file);
    if (!read || !newFileManager.loadInstrumentFormFileStruct(&instrument, &header, size, offset) ||
        !instrument.sample.length || instrument.sample.length > EDIT_BUFF_SIZE / 2)
        return false;
    const uint32_t monoBytes = instrument.sample.length * 2;
    stereo = size - offset == monoBytes * 2;
    return stereo || size - offset == monoBytes;
}

// Install only the edited slot. The finished audio already lives in the editor's
// work area, so stream setup can build its cache without rereading sample data.
bool install(int slot, const char* path, strInstrument& instrument, uint32_t offset,
             const int16_t* left, const int16_t* right)
{
    const uint32_t frames = instrument.sample.length;
    const bool stream = shouldStream(instrument, right != nullptr);
    const uint32_t ramFrames = stream ? std::min<uint32_t>(frames, kStreamPreCacheFrames) : frames;
    const uint32_t bytes = ramFrames * (right ? 4 : 2);
    if (!fits(slot, bytes)) return false;
    const int32_t delta = int32_t(bytes) - int32_t(newFileManager.getSampleMemorySize(slot, slot));
    engine.setBlockedInstrument(slot);
    const int16_t* channels[] = {left, right};
    if (stream && !voiceStreamManager.setupInstrumentStream(slot, path, offset, frames, right != nullptr, channels))
    {
        engine.clearBlockedInstrument(slot);
        return false;
    }
    newFileManager.moveSampleMemory(newFileManager.calcFirstSlotToMoveInMemory(slot + 1), delta);
    mtProject.instrument[slot] = instrument;
    if (frames)
    {
        newFileManager.setInstrumentSampleAddress(slot, right ? 2 : 1, ramFrames, stream);
        auto& memory = mtProject.sampleMemory[slot];
        memcpy(memory.address[0], left, ramFrames * 2);
        if (right) memcpy(memory.address[1], right, ramFrames * 2);
        if (stream && (instrument.playMode == playModeSlice || instrument.playMode == playModeBeatSlice))
            voiceStreamManager.refreshInstrumentSliceCache(slot);
    }
    newFileManager.completeLoadedInstrumentStruct(slot, frames != 0, frames);
    mtProject.used_memory += delta;
    engine.clearBlockedInstrument(slot);
    return true;
}

void finish(int slot)
{
    newFileManager.clearInstrumentChanged(slot);
    newFileManager.clearSampleChanged(slot);
    newFileManager.setProjectStructChanged();
    newFileManager.setStatus(fmImportSampleFromSampleEditorEnd);
    newFileManager.setCurrentOperation(fmNoOperation);
}
}

void SampleMemorySave::update()
{
    if (undoing)
    {
        updateUndo();
        return;
    }
    const auto status = fileTransfer.saveSampleRawDataToFile(
      memoryAddressL, memorySizeBytes, kEditTemp, sizeof(strInstrumentFile), memoryAddressR);
    if (status == fileTransferInProgress) return;
    fileTransfer.clearBytesComplited();

    char path[kEditPathSize], backup[kEditPathSize];
    instrumentPath(path, currentInstrument);
    const bool nextCopy = undoInstrument == currentInstrument && !undoCopy;
    backupPath(backup, currentInstrument, nextCopy);
    const bool previous = mtProject.instrument[currentInstrument].isActive;
    const bool stereo = memoryAddressR != nullptr;
    strInstrument instrument;
    uint32_t offset;
    bool fileStereo;
    if (status != fileTransferEnd || !readInstrument(fileSystem, kEditTemp, instrument, offset, fileStereo) ||
        instrument.sample.length * 2 != memorySizeBytes || fileStereo != stereo)
    {
        fileSystem->remove(kEditTemp);
        stopOperationWithError(fmImportSampleFromSampleEditorError);
        return;
    }

    // Recover the previous file if an interrupted rollback left it in the
    // backup position. Keep the current Undo entry until the new save succeeds.
    if (previous && !fileSystem->exists(path) && fileSystem->exists(backup))
        fileSystem->rename(backup, path);
    const bool hadFile = fileSystem->exists(path);
    if (hadFile) fileSystem->remove(backup);
    if ((previous && !hadFile) || (hadFile && !fileSystem->rename(path, backup)) ||
        !fileSystem->rename(kEditTemp, path))
    {
        if (hadFile && !fileSystem->exists(path)) fileSystem->rename(backup, path);
        fileSystem->remove(kEditTemp);
        stopOperationWithError(fmImportSampleFromSampleEditorError);
        return;
    }

    if (!install(currentInstrument, path, instrument, offset,
          reinterpret_cast<const int16_t*>(memoryAddressL), reinterpret_cast<const int16_t*>(memoryAddressR)))
    {
        fileSystem->remove(path);
        if (hadFile) fileSystem->rename(backup, path);
        stopOperationWithError(fmImportSampleFromSampleEditorError);
        return;
    }
    invalidateUndo(0, kSampleInstrumentsCount - 1);
    if (!previous && hadFile) fileSystem->remove(backup);
    undoInstrument = currentInstrument;
    undoHadSample = previous;
    undoCopy = nextCopy;
    finish(currentInstrument);
}

bool SampleMemorySave::saveSampleFromSampleEditor(int16_t* left, int16_t* right,
                                                  uint32_t frames, uint8_t slot,
                                                  const strInstrument& instrument)
{
    if (newFileManager.getStatus() != fmIdle || newFileManager.getCurrentOperation() != fmNoOperation ||
        slot >= kSampleInstrumentsCount || !frames || !left)
        return false;

    strInstrument output = instrument;
    if (!output.isActive)
    {
        newFileManager.setDefaultActiveInstrument(&output);
        snprintf(output.sample.file_name, SAMPLE_NAME_SIZE, "Sample %02u", slot + 1);
    }
    output.sample.length = frames;
    output.sample.address = nullptr;
    right = right == left ? nullptr : right;
    const uint32_t ramFrames = shouldStream(output, right != nullptr) ?
      std::min<uint32_t>(frames, kStreamPreCacheFrames) : frames;
    if (!fits(slot, ramFrames * (right ? 4 : 2))) return false;

    strInstrumentFile header;
    newFileManager.writeInstrumentToFileStruct(&output, &header);
    auto [opened, handle] = fileSystem->openFile(kEditTemp, FileSystem::kWriteCreateNew);
    if (!opened) return false;
    const bool written = fileSystem->writeFile(&header, sizeof(header), handle) == int32_t(sizeof(header));
    fileSystem->closeFile(handle);
    if (!written)
    {
        fileSystem->remove(kEditTemp);
        return false;
    }

    currentInstrument = slot;
    undoing = false;
    memoryAddressL = reinterpret_cast<uint8_t*>(left);
    memoryAddressR = reinterpret_cast<uint8_t*>(right);
    memorySizeBytes = frames * 2;
    newFileManager.setStatus(fmImportingSampleFromSampleEditor);
    newFileManager.setCurrentOperation(fmImportSampleFromSampleEditor);
    return true;
}

bool SampleMemorySave::canUndo(int slot) const
{
    return slot >= 0 && undoInstrument == slot;
}

void SampleMemorySave::invalidateUndo(int first, int last)
{
    if (undoInstrument < 0 || undoInstrument < first || undoInstrument > last) return;
    if (undoHadSample)
    {
        char backup[kEditPathSize];
        backupPath(backup, undoInstrument, undoCopy);
        fileSystem->remove(backup);
    }
    undoInstrument = -1;
}

bool SampleMemorySave::undo(int slot)
{
    if (!canUndo(slot) || newFileManager.getStatus() != fmIdle ||
        newFileManager.getCurrentOperation() != fmNoOperation)
        return false;

    if (undoHadSample)
    {
        char backup[kEditPathSize];
        backupPath(backup, slot, undoCopy);
        strInstrument instrument;
        bool stereo;
        if (!readInstrument(fileSystem, backup, instrument, undoDataOffset, stereo))
            return false;
        memorySizeBytes = instrument.sample.length * (stereo ? 4 : 2);
    }
    undoing = true;
    newFileManager.setStatus(fmImportingSampleFromSampleEditor);
    newFileManager.setCurrentOperation(fmImportSampleFromSampleEditor);
    return true;
}

void SampleMemorySave::updateUndo()
{
    const int slot = undoInstrument;
    char path[kEditPathSize], backup[kEditPathSize];
    instrumentPath(path, slot);
    backupPath(backup, slot, undoCopy);
    strInstrument instrument = {};
    uint32_t offset = 0;
    bool stereo = false;
    int16_t* scratch = externalRamEffectsBuff;
    if (undoHadSample)
    {
        uint32_t read = 0;
        const auto status = fileTransfer.loadInstrumentRawSampleDataToMemory(
          backup, undoDataOffset, reinterpret_cast<uint8_t*>(scratch), memorySizeBytes, read);
        if (status == fileTransferInProgress) return;
        fileTransfer.clearBytesComplited();
        if (status != fileTransferEnd || read * 2 != memorySizeBytes ||
            !readInstrument(fileSystem, backup, instrument, offset, stereo) ||
            offset != undoDataOffset || instrument.sample.length * (stereo ? 4 : 2) != memorySizeBytes)
        {
            stopOperationWithError(fmImportSampleFromSampleEditorError);
            return;
        }
    }

    if (!fileSystem->exists(path) && fileSystem->exists(kUndoTemp))
        fileSystem->rename(kUndoTemp, path);
    if (!fileSystem->rename(path, kUndoTemp))
    {
        stopOperationWithError(fmImportSampleFromSampleEditorError);
        return;
    }
    if (undoHadSample && !fileSystem->rename(backup, path))
    {
        fileSystem->rename(kUndoTemp, path);
        stopOperationWithError(fmImportSampleFromSampleEditorError);
        return;
    }
    if (!install(slot, path, instrument, offset, scratch, stereo ? scratch + instrument.sample.length : nullptr))
    {
        if (undoHadSample) fileSystem->rename(path, backup);
        fileSystem->rename(kUndoTemp, path);
        stopOperationWithError(fmImportSampleFromSampleEditorError);
        return;
    }
    undoInstrument = -1;
    fileSystem->remove(kUndoTemp);
    finish(slot);
}
