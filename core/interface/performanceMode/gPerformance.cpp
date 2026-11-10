#include "mtSequencer.h"
#include "mtAudioEngine.h"
#include "mtConfig.h"
#include "mtPadsBacklight.h"
#include "performanceMode.h"

namespace
{
constexpr unsigned kFirstFxCenter = 42;
constexpr unsigned kFxColumnPitch = 65;
constexpr unsigned kFxGridTop = 34;
constexpr unsigned kFxGridHeight = 389;
constexpr auto kTrackNameFont = controlStyleFont3;
}

static char* getPerfTrackName(uint8_t track)
{
    if (isSampleTrack(track))
    {
        return &mtProject.values.TrackNames[track][0];
    }
    else
    {
        uint8_t midiIdx = track - kSampleTracks;
        return &mtProject.values.midiTrackNames[midiIdx][0];
    }
}

static bool isPerfTrackMuted(uint8_t track)
{
    if (isSampleTrack(track))
    {
        return mtProject.values.trackMute[track] > 0;
    }
    else
    {
        return mtProject.values.midiTrackMute[track - kSampleTracks] > 0;
    }
}

void cPerformanceMode::initDisplayControls()
{
    strControlProperties prop;
    strControlProperties prop2;
    for (uint8_t i = 0; i < 8; i++)
    {
        prop2.text = (char*)"";
        prop2.colors = display.getTheme().activeLabelsColors;
        prop2.style = (controlStyleCenterX | controlStyleCenterY | kTrackNameFont);
        prop2.x = (800 / 8) * i + (800 / 16);
        prop2.w = 800 / 8 - 6;
        prop2.y = 424;
        prop2.h = 55;
        prop2.value = 1;

        label[i] = display.createControl<cLabel>(&prop2);
    }

    prop2.text = nullptr;
    prop2.colors = nullptr;
    prop2.value = 255;
    prop2.style = controlStyleNoTransparency;
    prop2.x = 0;
    prop2.w = 800;
    prop2.y = 425;
    prop2.h = 54;
    bgLabel = display.createControl<cBgLabel>(&prop2);

    for (unsigned i = 0; i < punch::kColumns; ++i)
    {
        prop.data = nullptr;
        prop.style = controlStyleCenterX | controlStyleCenterY | controlStyleFont2 |
                     controlStyleVerticalText;
        prop.x = kFirstFxCenter + kFxColumnPitch * i;
        prop.y = 244;
        prop.colors = display.getTheme().perfFxNameTextLabelColors;
        textLabel[i] = display.createControl<cLabel>(&prop);

        values[i].yForFxTextHole = 210;
        values[i].textLinesCount = 5;
        values[i].yOffset = 16;
        values[i].text[4] = const_cast<char*>("");
        for (unsigned row = 0; row < punch::kLevels; ++row)
            values[i].text[row] = const_cast<char*>(punch::columns[i].labels[row]);

        prop.style = (controlStyleCenterX | kTrackNameFont | controlStyleBackground |
                      (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0));
        prop.y = kFxGridTop;
        prop.colors = display.getTheme().perfValueNormalMLabelColors;
        prop.w = 62;
        prop.h = kFxGridHeight;
        prop.data = &values[i];
        valueLabel[i] = display.createControl<cMultiLabel>(&prop);
    }

    constexpr const char* groupNames[kFxGroupCount] = {"Rhythm", "Turntable", "Tone"};
    strControlProperties headerProps;
    headerProps.style = controlStyleCenterX | controlStyleCenterY | controlStyleFont4;
    headerProps.colors = display.getTheme().perfFxNameTextLabelColors;
    headerProps.y = kFxGridTop + 4;
    headerProps.h = 24;
    for (unsigned group = 0; group < kFxGroupCount; ++group)
    {
        headerProps.x = kFirstFxCenter + kFxColumnPitch * (group * kFxColumnsPerGroup) +
                        kFxColumnPitch * (kFxColumnsPerGroup - 1) / 2;
        headerProps.text = groupNames[group];
        groupHeaders[group] = display.createControl<cLabel>(&headerProps);
    }

    groupDividerData.line = {0, 0, 0, kFxGridHeight, display.getTheme().inactiveLabelsColors[3], 2};
    strControlProperties dividerProps;
    dividerProps.style = controlStyleSpecialFx;
    dividerProps.y = kFxGridTop;
    dividerProps.data = &groupDividerData;
    unsigned boundary = kFxColumnsPerGroup;
    for (auto& divider : groupDividers)
    {
        dividerProps.x = kFirstFxCenter + kFxColumnPitch * boundary - (kFxColumnPitch + 1) / 2;
        divider = display.createControl<cLabel>(&dividerProps);
        boundary += kFxColumnsPerGroup;
    }
}

void cPerformanceMode::showPerformanceFxes()
{
    titleBar->show();
    titleBar->setTitle("Performance FX");
    display.setControlShow(bgLabel);
    display.refreshControl(bgLabel);
    showTracks();
    auto& rack = engine.getAudioOutput()->getPunchFx().rack();
    for (unsigned i = 0; i < punch::kColumns; ++i)
    {
        display.setControlText(textLabel[i], punch::columns[i].name);
        display.setControlShow(textLabel[i]);
        display.refreshControl(textLabel[i]);
        showColumn(i, rack.soundingLevel(i));
    }
    for (auto header : groupHeaders)
    {
        display.setControlShow(header);
        display.refreshControl(header);
    }
    for (auto divider : groupDividers)
    {
        display.setControlShow(divider);
        display.refreshControl(divider);
    }
    display.synchronizeRefresh();
}

void cPerformanceMode::showTracks()
{
    for (uint8_t i = 0; i < 8; ++i)
    {
        const auto track = i + (tracksPage ? kSampleTracks : 0);
        const bool muted = isPerfTrackMuted(track);
        display.setControlText(label[i], getPerfTrackName(track));
        display.setControlText2(label[i], muted ? "Unmute" : "Mute");
        display.setControlColors(label[i], muted ? display.getTheme().inactiveLabelsColors
                                                : display.getTheme().activeLabelsColors);
        display.setControlStyle2(label[i], controlStyleCenterX | controlStyleCenterY | controlStyleFont2);
        display.setControlShow(label[i]);
        display.refreshControl(label[i]);
    }
}

void cPerformanceMode::showColumn(unsigned column, int row)
{
    displayedRow[column] = row;
    values[column].selected = row + 1;
    display.setControlShow(valueLabel[column]);
    display.refreshControl(valueLabel[column]);
    const bool inMiddleGroup = column / kFxColumnsPerGroup == 1;
    for (unsigned i = 0; i < punch::kLevels; ++i)
    {
        uint8_t brightness = inMiddleGroup ? mtConfig.values.padsLightBackDim : 0;
        if (static_cast<int>(i) == row) brightness = mtConfig.values.padsLightBack;
        padsBacklight.setBackLayer(brightness != 0, brightness, column + punch::kColumns * i);
    }
}

void cPerformanceMode::hideControlsUnderMiniMenu()
{
    for (auto handle : textLabel)
    {
        display.setControlHide(handle);
        display.refreshControl(handle);
    }
    for (auto header : groupHeaders)
    {
        display.setControlHide(header);
        display.refreshControl(header);
    }
    for (auto divider : groupDividers)
    {
        display.setControlHide(divider);
        display.refreshControl(divider);
    }
}
