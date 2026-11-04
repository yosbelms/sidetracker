#include "mtEffectModular.h"
#include "fileManager.h"
#include <cstdio>

void mtEffectModular::onApplyCommitted(int instrumentIdx)
{
    if (instrumentIdx >= 0)
    {
        if ((m_flags & PTFX_FLAG_GENERATOR))
        {
            if (mtProject.instrument[instrumentIdx].isActive == 0)
            {
                newFileManager.setDefaultActiveInstrument(&mtProject.instrument[instrumentIdx]);
                snprintf(mtProject.instrument[instrumentIdx].sample.file_name, SAMPLE_NAME_SIZE, "Gen %02d", instrumentIdx + 1);
            }
            mtProject.instrument[instrumentIdx].isActive = 1;
            mtProject.instrument[instrumentIdx].sample.length = getLengthToPolyphonicPlay();
        }
        if ((m_flags & PTFX_FLAG_WAVETABLE))
        {
            mtProject.instrument[instrumentIdx].playMode = playModeWavetable;
        }
    }
}

