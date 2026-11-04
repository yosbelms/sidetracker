#pragma once

#include "Platform.h"

class DesktopSgtl5000 final : public Sgtl5000
{
public:
    void init() override {}
    void setInputType(InputType input) override {}
    void setHeadphoneSource(HeadphoneSource source) override {}
    void setLineInLevel(int level) override {}
    void setMicGain(int gainDb) override {}
    void setHeadphoneMuted(bool muted) override { this->muted = muted; }
    void setMasterVolume(float volume) override { this->volume = volume; }
    bool muted = false;
    float volume = 1.0f;
    void setAutoVolumeEnabled(bool enabled) override {}
    void configureAutoVolume(AutoVolumeParams* params) override {}
    void setEnhanceBassEnabled(bool enabled) override {}
    void configureEnhanceBass(EnhanceBassParams* params) override {}
    void setEqualizerType(EqualizerType type) override {}
    void configureFiveBandEqualizer(float* values) override {}
    void enableSurroundSound(bool enabled) override {}
    void configureSurroundSound(SurroundSoundParams* params) override {}
    void enableHardwareProcessor(bool enabled) override {}
};

class DesktopRadio final : public Radio
{
public:
    void enableLineInput(bool enabled) override {}
    void enable(bool enabled) override {}
    bool setFrequency(float freq) override { return {}; }
    void setSeekCallback(void (*seekCallback)(void)) override {}
    void seek(SeekDirection direction) override {}
    void updateSeek() override {}
    bool updateRDS() override { return {}; }
    float getFrequency() override { return {}; }
    float getBaseFrequency() override { return {}; }
    float getSpacing() override { return {}; }
    bool isinitialized() override { return {}; }
    void clearRDS() override {}
    void setRegion(Region region) override {}
    bool isRegionChanged() override { return {}; }
    char* getStationName() override { static char empty[] = ""; return empty; }
};

class DesktopBattery final : public Battery
{
public:
    void enableMonitoring() override {}
    int getLevel() override { return 100; }
    float getVoltage() override { return {}; }
    bool getCharging() override { return {}; }
    int getTimeTo() override { return {}; }
    bool getState() override { return {}; }
    bool isEmpty() override { return {}; }
};

class DesktopEncoder final : public Encoder
{
public:
    void setAcceleration(int acceleration) override {}
    void setResolution(int resolution) override {}
};

class DesktopUsbAudio final : public UsbAudio
{
public:
    void beginFillData() override {}
    void fillChannelData(void* samples, int count, int channel) override {}
    void finishFillData() override {}
};

class DesktopUartMidi final : public UartMidi
{
public:
    bool isReadAvailable() override { return {}; }
    void read() override {}
    void sendRealTime(MessageType messageType) override {}
    void sendControlChange(uint8_t index, uint8_t value, uint8_t channel) override {}
    void sendProgramChange(uint8_t programNumber, uint8_t channel) override {}
    void sendAfterTouch(uint8_t velocity, uint8_t channel) override {}
    void sendPolyPressure(uint8_t note, uint8_t value, uint8_t channel) override {}
    void sendPitchBend(int value, uint8_t channel) override {}
    void sendNoteOn(uint8_t note, uint8_t velocity, uint8_t channel) override {}
    void sendNoteOff(uint8_t note, uint8_t velocity, uint8_t channel) override {}
    void init() override {}
    void setReceiveActions(MidiReceiveActions* receiveActions) override {}
    void setReceiveNoteOnAction(void (*action)(uint8_t, uint8_t, uint8_t)) override {}
};

class DesktopUsbMidi final : public UsbMidi
{
public:
    bool isReadAvailable() override { return {}; }
    void read() override {}
    void sendRealTime(MessageType messageType) override {}
    void sendControlChange(uint8_t index, uint8_t value, uint8_t channel) override {}
    void sendProgramChange(uint8_t programNumber, uint8_t channel) override {}
    void sendAfterTouch(uint8_t velocity, uint8_t channel) override {}
    void sendPolyPressure(uint8_t note, uint8_t value, uint8_t channel) override {}
    void sendPitchBend(int value, uint8_t channel) override {}
    void sendNoteOn(uint8_t note, uint8_t velocity, uint8_t channel) override {}
    void sendNoteOff(uint8_t note, uint8_t velocity, uint8_t channel) override {}
    void setReceiveActions(MidiReceiveActions* receiveActions) override {}
};

class DesktopUsb final : public Usb
{
public:
    void init(void) override {}
    void reset(void) override {}
    void update(void) override {}
    void setClassState(ClassType type, bool enable) override {}
    bool getClassState(ClassType type) override { return {}; }
    bool isAttached() override { return {}; }
};
