#include "mtEffectModular.h"
#include "fileManager.h"
#include <cstdio>

void mtEffectModular::prepareOutputInstrument(strInstrument& instrument, int instrumentIdx)
{
    if (instrumentIdx >= 0)
    {
        if ((m_flags & PTFX_FLAG_GENERATOR))
        {
            if (instrument.isActive == 0)
            {
                newFileManager.setDefaultActiveInstrument(&instrument);
                snprintf(instrument.sample.file_name, SAMPLE_NAME_SIZE, "Gen %02d", instrumentIdx + 1);
            }
            instrument.isActive = 1;
            instrument.sample.length = getLengthToPolyphonicPlay();
        }
        if ((m_flags & PTFX_FLAG_WAVETABLE))
        {
            instrument.playMode = playModeWavetable;
        }
    }
}

