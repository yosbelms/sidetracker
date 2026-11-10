#ifndef INTERFACE_PERFORMANCEMODE_H_
#define INTERFACE_PERFORMANCEMODE_H_

#include "core/modulesBase.h"
#include "PerfFx/PerfFx.h"
#include "mtStructs.h"

constexpr uint8_t trackMasterModeCount = 2;

class cPerformanceMode : public ModuleBase
{
  public:
    void update() override;
    void start(uint32_t options) override;
    void stop() override;
    void initDisplayControls() override;
    void destroyDisplayControls() override {}
    uint8_t getCurrentModeIdx() override { return tracksPage; }
    void hideControlsUnderMiniMenu() override;

    void switchTracksPage();
    void toggleMute(uint8_t button);
    void showPerfFx();

  private:
    void showTracks();
    void showColumn(unsigned column, int row);

    static constexpr unsigned kFxColumnsPerGroup = 4;
    static constexpr unsigned kFxGroupCount = perffx::kColumns / kFxColumnsPerGroup;
    strLabelData groupDividerData{};
    hControl groupHeaders[kFxGroupCount];
    hControl groupDividers[kFxGroupCount - 1];
    uint8_t tracksPage = 0;
    hControl bgLabel;
    hControl label[8];
    hControl textLabel[perffx::kColumns];
    hControl valueLabel[perffx::kColumns];
    strMultiLabelData values[perffx::kColumns];
    int8_t displayedRow[perffx::kColumns];
};

extern cPerformanceMode performanceMode;

#endif
