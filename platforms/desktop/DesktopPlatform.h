#pragma once

#include "ConfigManager.h"
#include "DesktopDevices.h"
#include "DesktopStorage.h"
#include "SoftwareDisplay.h"

#include <array>
#include <filesystem>

class DesktopTimer final : public Timer
{
public:
    explicit DesktopTimer(bool virtualTime) : virtualTime(virtualTime), start(SDL_GetPerformanceCounter()) {}
    uint32_t millis() override { return timeUs() / 1000; }
    uint32_t micros() override { return timeUs(); }
    uint64_t timeUs() const
    {
        if (virtualTime)
            return elapsed;
        return (SDL_GetPerformanceCounter() - start) * 1000000 / SDL_GetPerformanceFrequency();
    }
    void delay(int ms) override
    {
        if (virtualTime)
            advance(ms);
        else
            SDL_Delay(ms);
    }
    uint32_t clockCycles() override { return micros(); }
    uint32_t clockCyclesPerUs() override { return 1; }
    void advance(uint32_t ms) { elapsed += ms * 1000; }

private:
    bool virtualTime;
    uint64_t start;
    uint64_t elapsed = 0;
};

class DesktopSwitches final : public SwitchGroup
{
public:
    void setHoldTime(int timeMs) override { holdTime = timeMs; }
    bool isButtonPressed(int id) override { return id >= 0 && id < int(keys.size()) && keys[id].down; }
    void setActionCallback(ActionType type, void (*callback)(uint8_t)) override { callbacks[type] = callback; }
    void set(uint8_t id, bool down, uint32_t now);
    void update(uint32_t now);

private:
    struct State { bool down = false, held = false; uint32_t pressed = 0; };
    std::array<State, 64> keys{};
    void (*callbacks[3])(uint8_t){};
    uint32_t holdTime = 500;
};

class DesktopLeds final : public Leds
{
public:
    void setLed(int id, bool on, int brightness) override;
    std::array<uint8_t, 48> levels{};
};

class DesktopConfig final : public ConfigManager
{
public:
    explicit DesktopConfig(std::filesystem::path path) : path(std::move(path)) {}
    bool readData(int address, void* data, int size) override;
    void writeData(int address, void* data, int size) override;

private:
    std::filesystem::path path;
};

class DesktopCrc final : public CrcCalculator
{
public:
    uint32_t calculate32bitCrc(const uint8_t* data, uint16_t size) override;
};

class DesktopPlatform final : public Platform
{
public:
    DesktopPlatform(SoftwareDisplay& display, const std::filesystem::path& card, bool headless);
    void setPowerMode(bool on) override { running = on; }
    void resetMCU() override { running = false; }
    void runBootloader(char*) override {}
    void setPowerButtonCallback(PowerButtonCallback callback) override { powerButton = callback; }
    bool getPowerButtonState() override { return true; }
    bool readForceStartUp() override { return true; }
    Sgtl5000* getCodecAudio() override { return &codec; }
    Radio* getRadio() override { return &radio; }
    SwitchGroup* getButtons() override { return &buttons; }
    SwitchGroup* getPads() override { return &pads; }
    Leds* getLeds() override { return &leds; }
    Battery* getBattery() override { return &battery; }
    Timer* getTimer() override { return &timer; }
    Encoder* getEncoder() override { return &encoder; }
    DisplayDriver* getDisplayDriver() override { return &display; }
    UsbAudio* getUsbAudio() override { return &usbAudio; }
    UartMidi* getUartMidi() override { return &uartMidi; }
    UsbMidi* getUsbMidi() override { return &usbMidi; }
    ConfigManager* getConfigManager() override { return &config; }
    FileSystem* getFileSystem() override { return &storage; }
    CrcCalculator* getCrcCalculator() override { return &crc; }
    float volume() const { return codec.muted ? 0.0f : codec.volume; }
    SystemInfo* getSystemInfo() override { return &info; }
    Usb* getUsb() override { return &usb; }

    bool running = true;
    PowerButtonCallback powerButton = nullptr;
    DesktopTimer timer;
    DesktopSwitches buttons, pads;
    DesktopLeds leds;

private:
    SoftwareDisplay& display;
    DesktopStorage storage;
    DesktopConfig config;
    DesktopCrc crc;
    DesktopSgtl5000 codec;
    DesktopRadio radio;
    DesktopBattery battery;
    DesktopEncoder encoder;
    DesktopUsbAudio usbAudio;
    DesktopUartMidi uartMidi;
    DesktopUsbMidi usbMidi;
    DesktopUsb usb;
    SystemInfo info{"Desktop"};
};
