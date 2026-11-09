#ifndef FILEMANAGER_SAMPLEMEMORYSAVE_H_
#define FILEMANAGER_SAMPLEMEMORYSAVE_H_

#include "FileManagerOperation.h"

#include <stdint.h>

struct strInstrument;

class SampleMemorySave : public FileManagerOperation
{
  public:
    virtual void update() override;

    bool saveSampleFromSampleEditor(int16_t* memoryAddressL,
                                    int16_t* memoryAddressR,
                                    uint32_t samplesCount,
                                    uint8_t instrumentIdx,
                                    const strInstrument& instrument);
    bool canUndo(int slot) const;
    bool undo(int slot);
    void invalidateUndo(int first, int last);

  private:
    int currentInstrument;
    uint8_t* memoryAddressR;
    uint8_t* memoryAddressL;
    uint32_t memorySizeBytes;
    int undoInstrument = -1;
    bool undoHadSample = false;
    bool undoCopy = false;
    bool undoing = false;
    uint32_t undoDataOffset = 0;
    void updateUndo();
};

#endif /* FILEMANAGER_SAMPLEMEMORYSAVE_H_ */
