#ifndef INTERFACE_PERFORMANCEMODE_H_
#define INTERFACE_PERFORMANCEMODE_H_

#include "core/modulesBase.h"
#include "PerformanceFx/PunchFx.h"
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
    void showPerformanceFxes();

  private:
    void showTracks();
    void showColumn(unsigned column, int row);

    static constexpr unsigned kFxColumnsPerGroup = 4;
    strLabelData groupDividerData{};
    hControl groupDividers[punch::kColumns / kFxColumnsPerGroup - 1];
    uint8_t tracksPage = 0;
    hControl bgLabel;
    hControl label[8];
    hControl textLabel[punch::kColumns];
    hControl valueLabel[punch::kColumns];
    strMultiLabelData values[punch::kColumns];
    int8_t displayedRow[punch::kColumns];
};

extern cPerformanceMode performanceMode;

#endif
