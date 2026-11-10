#include <string.h>
#include <stddef.h>
#include <algorithm>

#include "ConfigManager.h"
#include "Platform.h"
#include "core/interface.h"
#include "display.h"
#include "ThemeManager.h"

#include "mtStructs.h"


#include "convertOldFileVersionVolume.h"
#include "mtConfig.h"
#include "mtAudioEngine.h"
#include "mtSequencer.h"

void firmwareVersionChangeMini();
void firmwareVersionChange();
void memoryStructureChange();
void eepromStructureChange();

extern void checkFuncitonKey(int key);

static ConfigManager* configManager;
static FileSystem* configFileSystem;

static constexpr char kPerfFxMixPath[] = "/.tracker-ce-perf-mix";
static constexpr uint8_t kDefaultPerfFxMix = 100;
static constexpr uint8_t kInlinePerfFxMixVersion = 11;
static uint8_t perfFxMix = kDefaultPerfFxMix;
static bool perfFxMixLoaded = false;
static bool perfFxMixDirty = false;
static bool legacyMixPending = false;
static elapsedMillis perfFxMixSaveDelay;
static constexpr unsigned kConfigSaveIntervalMs = 5000;

uint8_t getPerfFxMix()
{
    return perfFxMix;
}

void setPerfFxMix(uint8_t mix)
{
    if (mix == perfFxMix) return;
    perfFxMix = mix;
    perfFxMixDirty = true;
    perfFxMixSaveDelay = 0;
    engine.getAudioOutput()->getPerfFx().setMix(mix);
}

static bool readPerfFxMix(uint8_t& mix)
{
    auto [opened, file] = configFileSystem->openFile(kPerfFxMixPath);
    if (!opened) return false;
    const bool valid = configFileSystem->readFile(&mix, sizeof(mix), file) == sizeof(mix) &&
                       mix <= kDefaultPerfFxMix;
    configFileSystem->closeFile(file);
    return valid;
}

void loadPerfFxMix()
{
    if (perfFxMixLoaded || !configFileSystem->isSdCardInserted()) return;
    perfFxMixLoaded = true;

    uint8_t mix;
    if (readPerfFxMix(mix))
    {
        perfFxMix = mix;
        perfFxMixDirty = false;
        legacyMixPending = false;
    }
    engine.getAudioOutput()->getPerfFx().setMix(perfFxMix);
}

void flushPerfFxMix()
{
    if (!perfFxMixDirty || !configFileSystem->isSdCardInserted()) return;
    auto [opened, file] = configFileSystem->openFile(kPerfFxMixPath, FileSystem::kWriteOpen);
    if (!opened) return;

    const bool written = configFileSystem->writeFile(&perfFxMix, sizeof(perfFxMix), file) ==
                         sizeof(perfFxMix);
    configFileSystem->closeFile(file);
    if (!written) return;

    uint8_t saved;
    if (legacyMixPending && (!readPerfFxMix(saved) || saved != perfFxMix)) return;
    perfFxMixDirty = false;
    legacyMixPending = false;
}

elapsedMillis save_delay;

static uint8_t saveFlag = 0;
static uint8_t saveAsapFlag = 0;

static void writeSharedConfig();

static const uint8_t eepromStructsCount = 9;
static uint8_t* eepromStructsPtr[eepromStructsCount]{
    (uint8_t*)&mtConfig.audioCodecConfig,
    (uint8_t*)&mtConfig.firmware,
    (uint8_t*)&mtConfig.midi,
    (uint8_t*)&mtConfig.values,
    (uint8_t*)&mtConfig.general,
    (uint8_t*)&mtConfig.interface,
    (uint8_t*)&mtConfig.metronome,
    (uint8_t*)&mtConfig.debug,
    (uint8_t*)&mtConfig.common,
};

static uint16_t eepromStructsSizes[]{
    sizeof(mtConfig.audioCodecConfig), sizeof(mtConfig.firmware), sizeof(mtConfig.midi),
    sizeof(mtConfig.values),           sizeof(mtConfig.general),  sizeof(mtConfig.interface),
    sizeof(mtConfig.metronome),        sizeof(mtConfig.debug),    sizeof(mtConfig.common),
};

void initConfig(Platform* platform)
{
    configManager = platform->getConfigManager();
    configFileSystem = platform->getFileSystem();
}

void updateConfig(bool force)
{
    // The sidecar shares the SD card with streamed samples. Save after
    // playback and coalesce rapid changes into one write.
    if (perfFxMixDirty && !sequencer.isPlay() &&
        (force || perfFxMixSaveDelay > kConfigSaveIntervalMs))
    {
        flushPerfFxMix();
        perfFxMixSaveDelay = 0;
    }

    if (saveFlag && (save_delay > kConfigSaveIntervalMs || force || saveAsapFlag))
    {
        saveAsapFlag = 0;
        writeSharedConfig();
    }

    if (save_delay > kConfigSaveIntervalMs)
    {
        save_delay = 0;
    }
}

void saveConfig()
{
    saveFlag = 1;
}
void saveConfigAsap()
{
    saveFlag = 1;
    saveAsapFlag = 1;
}

void forceSaveConfig()
{
    flushPerfFxMix();
    writeSharedConfig();
}

static void writeSharedConfig()
{
    // Keep the v11 copy until its mix has been saved separately.
    if (legacyMixPending)
    {
        saveFlag = 1;
        return;
    }

    strcpy(mtConfig.header.id, "EEPROM");
    mtConfig.header.id[6] = 0;
    mtConfig.header.version = CONFIG_STRUCT_VER;
    mtConfig.header.totalSize = sizeof(mtConfig);

    mtConfig.audioCodecConfig.size = sizeof(mtConfig.audioCodecConfig);
    mtConfig.firmware.size = sizeof(mtConfig.firmware);
    mtConfig.firmware.eepromStructVer = CONFIG_STRUCT_VER;
    mtConfig.midi.size = sizeof(mtConfig.midi);
    mtConfig.values.size = sizeof(mtConfig.values);
    mtConfig.general.size = sizeof(mtConfig.general);
    mtConfig.interface.size = sizeof(mtConfig.interface);
    mtConfig.metronome.size = sizeof(mtConfig.metronome);
    mtConfig.debug.size = sizeof(mtConfig.debug);
    mtConfig.common.size = sizeof(mtConfig.common);

    // Older writers stop after the last sized group, before struct tail padding.
    constexpr int bytes = offsetof(strMtConfig, common) + sizeof(mtConfig.common);
    if (configManager->writeConfig(&mtConfig, bytes)) saveFlag = 0;
}

//###############################################################
//
//###############################################################
void readConfig()
{
    uint8_t inlineMix = kDefaultPerfFxMix;
    bool isCorrect = configManager->readData(0, &mtConfig.header, sizeof(mtConfig.header));

    if (!isCorrect)
    {
        resetConfig();
        checkConfig();
        loadPerfFxMix();
        forceSaveConfig();
        return;
    }

    bool saveConfigAtEnd = false;

    if (memcmp(mtConfig.header.id, "EEPROM", sizeof(mtConfig.header.id)) != 0) // nie rozpoznana zawartosc eepromu
    {
        // mozliwe ze w eepromie jest stary config wiec dokonaj konwersji
        recoverConfigFromOldVerison();

        // teraz napraw zawartosc jesli bylo inaczej i byl tam po prostu syf
        checkConfig();

        saveConfigAtEnd = true;
    }
    else // normalny odczyt configu w nowej wersji
    {
        const size_t limit = std::min<size_t>(mtConfig.header.totalSize, sizeof(mtConfig));
        size_t offset = offsetof(strMtConfig, audioCodecConfig);
        for (uint8_t i = 0; i < eepromStructsCount && offset + sizeof(uint16_t) <= limit; ++i)
        {
            uint16_t size = 0;
            if (!configManager->readData(offset, &size, sizeof(size)) ||
                size < sizeof(size) || size > limit - offset)
                break;

            const auto bytes = std::min(size, eepromStructsSizes[i]);
            if (!configManager->readData(offset, eepromStructsPtr[i], bytes))
                break;

            if (i == eepromStructsCount - 1 &&
                mtConfig.header.version == kInlinePerfFxMixVersion &&
                mtConfig.firmware.eepromStructVer == kInlinePerfFxMixVersion &&
                size == sizeof(mtConfig.common) + 2)
            {
                uint8_t savedMix;
                if (configManager->readData(offset + sizeof(mtConfig.common), &savedMix, sizeof(savedMix)) &&
                    savedMix <= kDefaultPerfFxMix)
                    inlineMix = savedMix;
            }
            offset += size;
        }

        // napraw zawartosc
        checkConfig();
    }
    if ((mtConfig.firmware.ver_1 != PRODUCT_VER_1) || (mtConfig.firmware.ver_2 != PRODUCT_VER_2) ||
        (mtConfig.firmware.ver_3 != PRODUCT_VER_3) || (mtConfig.firmware.beta != PRODUCT_BETA))
    {

#if defined(TRACKER_MINI)
        firmwareVersionChangeMini();
#elif defined(PLATFORM_IMX) // TRACKER_PLUS
// nothing todo yet
#else                       // TRACKER_OG
        firmwareVersionChange();
#endif
        mtConfig.firmware.ver_1 = PRODUCT_VER_1;
        mtConfig.firmware.ver_2 = PRODUCT_VER_2;
        mtConfig.firmware.ver_3 = PRODUCT_VER_3;
        mtConfig.firmware.beta = PRODUCT_BETA;

        saveConfigAtEnd = true;
    }

    if (mtConfig.firmware.eepromStructVer != CONFIG_STRUCT_VER)
    {
        eepromStructureChange();

        mtConfig.firmware.eepromStructVer = CONFIG_STRUCT_VER;

        saveConfigAtEnd = true;
    }

    if (inlineMix != kDefaultPerfFxMix)
    {
        perfFxMix = inlineMix;
        perfFxMixDirty = true;
        legacyMixPending = true;
    }
    loadPerfFxMix();

    if (saveConfigAtEnd || mtConfig.header.version == kInlinePerfFxMixVersion)
    {
        forceSaveConfig();
    }
}

void checkConfig()
{
    if (mtConfig.audioCodecConfig.headphoneVolume < MASTER_VOLUME_MIN)
        mtConfig.audioCodecConfig.headphoneVolume = MASTER_VOLUME_MIN;
    else if (mtConfig.audioCodecConfig.headphoneVolume > MASTER_VOLUME_MAX)
        mtConfig.audioCodecConfig.headphoneVolume = MASTER_VOLUME_MAX;

    if (mtConfig.audioCodecConfig.inSelect >= inputSelectCount)
        mtConfig.audioCodecConfig.inSelect = inputSelectMic;
    if (mtConfig.audioCodecConfig.outSelect >= outputSelectCount)
        mtConfig.audioCodecConfig.outSelect = outputSelectHeadphones;

    if (mtConfig.audioCodecConfig.inputGain > 63)
        mtConfig.audioCodecConfig.inputGain = INPUT_MIC_GAIN_DEFAULT;

    if (mtConfig.audioCodecConfig.mutedHeadphone > 1)
        mtConfig.audioCodecConfig.mutedHeadphone = 0;
    if (mtConfig.audioCodecConfig.mutedLineOut > 1)
        mtConfig.audioCodecConfig.mutedLineOut = 0;

    if (mtConfig.audioCodecConfig.lineInLeft > 15)
        mtConfig.audioCodecConfig.lineInLeft = LINE_IN_SENS_LEVEL_DEFAULT;
    if (mtConfig.audioCodecConfig.lineInRight > 15)
        mtConfig.audioCodecConfig.lineInRight = LINE_IN_SENS_LEVEL_DEFAULT;

    if (mtConfig.audioCodecConfig.lineOutLeft > 31)
        mtConfig.audioCodecConfig.lineOutLeft = LINE_OUT_LEVEL_DEFAULT;
    else if (mtConfig.audioCodecConfig.lineOutLeft < 13)
        mtConfig.audioCodecConfig.lineOutLeft = LINE_OUT_LEVEL_DEFAULT;

    if (mtConfig.audioCodecConfig.lineOutRight > 31)
        mtConfig.audioCodecConfig.lineOutRight = LINE_OUT_LEVEL_DEFAULT;
    else if (mtConfig.audioCodecConfig.lineOutRight < 13)
        mtConfig.audioCodecConfig.lineOutRight = LINE_OUT_LEVEL_DEFAULT;

    if (mtConfig.audioCodecConfig.volume > MASTER_VOLUME_MAX)
        mtConfig.audioCodecConfig.volume = MASTER_VOLUME_MAX;

    //	mtConfig.startup.lastProjectName[PROJECT_NAME_SIZE-1] = 0;
    //	if(mtConfig.startup.startMode >= interfaceCommandsCount) mtConfig.startup.startMode =
    // interfaceOpenLastProject;
    //
    //	if(mtConfig.startup.powerState > powerStateRun)
    //	{
    //		mtConfig.startup.powerState = powerStateSleep;
    //	}

    // midi ----------------------------------------
    if (mtConfig.midi.clkIn > 2)
    {
        mtConfig.midi.clkIn = 0;
    }

    if (mtConfig.midi.clkOut > 3)
    {
        mtConfig.midi.clkOut = 0;
    }

    if (mtConfig.midi.transportIn > 2)
    {
        mtConfig.midi.transportIn = 0;
    }

    if (mtConfig.midi.transportOut > 3)
    {
        mtConfig.midi.transportOut = 0;
    }

    if (mtConfig.midi.notesOutMode > 3)
    {
        mtConfig.midi.notesOutMode = 0;
    }

    if (mtConfig.midi.notesInMode > 3)
    {
        mtConfig.midi.notesInMode = 0;
    }

    if (mtConfig.midi.notesInChannel > 16)
    {
        mtConfig.midi.notesInChannel = 0;
    }

    if (mtConfig.midi.ccInMode > 3)
    {
        mtConfig.midi.ccInMode = 0;
    }

    if (mtConfig.midi.ccInChannel > 16)
    {
        mtConfig.midi.ccInChannel = 0;
    }

    if (mtConfig.midi.middleC > 3)
    {
        mtConfig.midi.middleC = 2;
    }

    mtConfig.midi.slaveClockComp =
      clamp(mtConfig.midi.slaveClockComp, (uint8_t)0, (uint8_t)kExternalClockCompMaxVal);

    // VALUES CHECK  --------------------
    if (mtConfig.values.padsLightBack > 31)
        mtConfig.values.padsLightBack = PADS_LIGHT_BACK_DEFAULT;
    if (mtConfig.values.padsLightFront > 31)
        mtConfig.values.padsLightFront = PADS_LIGHT_FRONT_DEFAULT;
    if (mtConfig.values.padsLightBackDim > 31)
        mtConfig.values.padsLightBackDim = PADS_LIGHT_BACK_DEFAULT / 2;

    // general ----------------------------------------
    if (mtConfig.general.dispBrightness > 2)
    {
        mtConfig.general.dispBrightness = 2;
    }

    if (mtConfig.general.padsBrightness > 2)
    {
        mtConfig.general.padsBrightness = 2;
    }

    if (mtConfig.general.radioRegion > 3)
    {
        mtConfig.general.radioRegion = 0;
    }

    if (mtConfig.general.mtpState > 0)
    {
        mtConfig.general.mtpState = 0;
    }

    if (mtConfig.general.recOptions > recOptions_max)
        mtConfig.general.recOptions = 0;
    if (mtConfig.general.recThreshold > recThreshold_max)
        mtConfig.general.recThreshold = recThreshold_default;

    if (mtConfig.general.numberingMode >= NumberingMode::kNumberingModeCount)
        mtConfig.general.numberingMode = NumberingMode::kNumberingModeDefault;

    if (mtConfig.general.padPreviewInPatternEdit > padPreviewModePattern)
        mtConfig.general.padPreviewInPatternEdit = padPreviewModePattern;
    if (mtConfig.general.patternArrangement > 2)
        mtConfig.general.patternArrangement = 0;
    if (mtConfig.general.patternInfo > 2)
        mtConfig.general.patternInfo = 0;

    if (mtConfig.general.lineInOutType > 2)
    {
        mtConfig.general.lineInOutType = 0;
    }

    if (mtConfig.general.theme >= ThemeManager::kMaxCustomThemes)
    {
        mtConfig.general.theme = DisplayThemes::kOriginal;
    }
    if (mtConfig.general.font >= ThemeManager::kMaxCustomFonts)
    {
        mtConfig.general.font = 0;
    }

    if (mtConfig.general.previewVolume > PREVIEW_VOLUME_MAX)
    {
        mtConfig.general.previewVolume = PREVIEW_VOLUME_MAX;
    }

    // interface ----------------------------------------
    if (mtConfig.interface.fxPopupDescription > 1)
    {
        mtConfig.interface.fxPopupDescription = 0;
    }

    for (uint8_t i = 0; i < 8; i++)
    {
        if (mtConfig.interface.functionKeysBinding[i] >= kMenuModulesCount)
        {
            mtConfig.interface.functionKeysBinding[i] = 0;
        }
        checkFuncitonKey(i);
    }

    // metornome ----------------------------------------
    if (mtConfig.metronome.state > 1)
        mtConfig.metronome.state = 0;
    if (mtConfig.metronome.preRoll > 1)
        mtConfig.metronome.preRoll = 0;
    if (mtConfig.metronome.timeSignatureNumerator > 11)
        mtConfig.metronome.timeSignatureNumerator = 3;
    if (mtConfig.metronome.timeSignatureDenominator > 11)
        mtConfig.metronome.timeSignatureDenominator = 3;
    if (mtConfig.metronome.volume > 100)
        mtConfig.metronome.volume = 50;

    // debug ----------------------------------------
    if (mtConfig.debug.debugLogState > 0)
    {
        mtConfig.debug.debugLogState = 0;
    }

    // common ----------------------------------------
    if (mtConfig.common.equalSliceNumber < 1)
        mtConfig.common.equalSliceNumber = 8;
    else if (mtConfig.common.equalSliceNumber > 48)
        mtConfig.common.equalSliceNumber = 8;

    if (mtConfig.common.usbAudioMode >= 4)
        mtConfig.common.usbAudioMode = 0;

    // TODO dodac zapis spowrotem do configu jesli np przy sprawdzaniu wazne dane sie nie zgadzały
    // TODO
    // TODO
}

void resetConfig()
{
    setPerfFxMix(kDefaultPerfFxMix);
    // header ----------------------------------------
    strcpy(mtConfig.header.id, "EEPROM");
    mtConfig.header.id[6] = 0;
    mtConfig.header.version = CONFIG_STRUCT_VER;
    mtConfig.header.totalSize = sizeof(mtConfig);

    // firmware ----------------------------------------

    mtConfig.firmware.ver_1 = PRODUCT_VER_1;
    mtConfig.firmware.ver_2 = PRODUCT_VER_2;
    mtConfig.firmware.ver_3 = PRODUCT_VER_3;
    mtConfig.firmware.beta = PRODUCT_BETA;

    mtConfig.firmware.eepromStructVer = CONFIG_STRUCT_VER;

    // interface ----------------------------------------
    mtConfig.audioCodecConfig.headphoneVolume = MASTER_VOLUME_DEFAULT;
    mtConfig.audioCodecConfig.inSelect = inputSelectMic;
    mtConfig.audioCodecConfig.outSelect = outputSelectHeadphones;
    mtConfig.audioCodecConfig.inputGain = INPUT_MIC_GAIN_DEFAULT;
    mtConfig.audioCodecConfig.mutedHeadphone = 0;
    mtConfig.audioCodecConfig.mutedLineOut = 0;
    mtConfig.audioCodecConfig.lineInLeft = LINE_IN_SENS_LEVEL_DEFAULT;
    mtConfig.audioCodecConfig.lineInRight = LINE_IN_SENS_LEVEL_DEFAULT;
    mtConfig.audioCodecConfig.lineOutLeft = LINE_OUT_LEVEL_DEFAULT;
    mtConfig.audioCodecConfig.lineOutRight = LINE_OUT_LEVEL_DEFAULT;
    mtConfig.audioCodecConfig.volume = MASTER_VOLUME_DEFAULT;

    // midi ----------------------------------------
    mtConfig.midi.clkIn = 0;
    mtConfig.midi.clkOut = 0;
    mtConfig.midi.transportIn = 0;
    mtConfig.midi.transportOut = 0;
    mtConfig.midi.notesInMode = 0;
    mtConfig.midi.notesInChannel = 0;
    mtConfig.midi.notesOutMode = 0;
    mtConfig.midi.ccInMode = 0;
    mtConfig.midi.ccInChannel = 0;
    mtConfig.midi.middleC = 2;
    mtConfig.midi.slaveClockComp = kExternalClockCompDefaultVal;

    // values ----------------------------------------
    mtConfig.values.padsLightBack = PADS_LIGHT_BACK_DEFAULT;
    mtConfig.values.padsLightFront = PADS_LIGHT_FRONT_DEFAULT;
    mtConfig.values.padsLightBackDim = PADS_LIGHT_BACK_DEFAULT / 2;

    // general ----------------------------------------
    mtConfig.general.dispBrightness = 2;
    mtConfig.general.padsBrightness = 2;
    mtConfig.general.radioRegion = 0;
    mtConfig.general.mtpState = 0;

    mtConfig.general.recOptions = 0;
    mtConfig.general.numberingMode = NumberingMode::kNumberingModeDefault;
    mtConfig.general.padPreviewInPatternEdit = padPreviewModePattern;
    mtConfig.general.patternArrangement = 0;
    mtConfig.general.patternInfo = 0;
    mtConfig.general.lineInOutType = 0;
    mtConfig.general.previewVolume = PREVIEW_VOLUME_DEFAULT;
    mtConfig.general.recThreshold = recThreshold_default;

    // interface ----------------------------------------
    mtConfig.interface.fxPopupDescription = 0;

    mtConfig.interface.functionKeysBinding[0] = 0;
    mtConfig.interface.functionKeysBinding[1] = 1;
    mtConfig.interface.functionKeysBinding[2] = 2;
    mtConfig.interface.functionKeysBinding[3] = 3;
    mtConfig.interface.functionKeysBinding[4] = 4;
    mtConfig.interface.functionKeysBinding[5] = 5;
    mtConfig.interface.functionKeysBinding[6] = 6;
    mtConfig.interface.functionKeysBinding[7] = 7;

    // metronome ----------------------------------------
    mtConfig.metronome.state = 0;
    mtConfig.metronome.preRoll = 0;
    mtConfig.metronome.timeSignatureNumerator = 3;
    mtConfig.metronome.timeSignatureDenominator = 3;
    mtConfig.metronome.volume = 50;

    // debug ----------------------------------------
    mtConfig.debug.debugLogState = 0;

    // common ----------------------------------------
    mtConfig.common.equalSliceNumber = 8;
    mtConfig.common.usbAudioMode = 0;
}

uint16_t sizes[10];

// odczytanie eepromu w starej konwencji i wstawienie do nowej
void recoverConfigFromOldVerison()
{
    configManager->readData(36, ((uint8_t*)&mtConfig.audioCodecConfig) + 2, 13);
    configManager->readData(52, ((uint8_t*)&mtConfig.firmware) + 2, 5);
    configManager->readData(57, ((uint8_t*)&mtConfig.midi) + 2, 18);
    configManager->readData(76, ((uint8_t*)&mtConfig.values) + 2, 151);
    configManager->readData(228, ((uint8_t*)&mtConfig.general) + 2, 10);
    configManager->readData(238, ((uint8_t*)&mtConfig.interface) + 2, 1);
    configManager->readData(239, ((uint8_t*)&mtConfig.metronome) + 2, 5);
    configManager->readData(244, ((uint8_t*)&mtConfig.debug) + 2, 1);
    configManager->readData(245, ((uint8_t*)&mtConfig.common) + 2, 1);

    sizes[0] = sizeof(mtConfig.audioCodecConfig);
    sizes[1] = sizeof(mtConfig.firmware);
    sizes[2] = sizeof(mtConfig.midi);
    sizes[3] = sizeof(mtConfig.values);
    sizes[4] = sizeof(mtConfig.general);
    sizes[5] = sizeof(mtConfig.interface);
    sizes[6] = sizeof(mtConfig.metronome);
    sizes[7] = sizeof(mtConfig.debug);
    sizes[8] = sizeof(mtConfig.common);

    strcpy(mtConfig.header.id, "EEPROM");
    mtConfig.header.id[6] = 0;
    mtConfig.header.version = CONFIG_STRUCT_VER;
    mtConfig.header.totalSize = sizeof(mtConfig);

    // ustaw nowe parametry
    mtConfig.audioCodecConfig.volume = MASTER_VOLUME_DEFAULT;
    mtConfig.general.numberingMode = NumberingMode::kNumberingModeDefault;
    mtConfig.general.padPreviewInPatternEdit = padPreviewModePattern;
    mtConfig.general.previewVolume = PREVIEW_VOLUME_DEFAULT;
}

void firmwareVersionChange()
{
    // Community Edition (CE) resets version numbers to 1.0.0.
    // Legacy pre-1.9 EEPROM migrations are disabled so CE 1.0.x
    // upgrades do not overwrite existing user settings.
}

#ifdef TRACKER_MINI
void firmwareVersionChangeMini()
{
    // Community Edition (CE) resets version numbers to 1.0.0.
}
#endif

void eepromStructureChange()
{
    // Serial.println("Eeprom structure changed!");
    // resetConfig();
}
