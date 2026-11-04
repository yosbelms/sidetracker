#include "DesktopPlatform.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

void onButtonPush(uint8_t);
void onButtonRelease(uint8_t);
void onButtonHold(uint8_t);
void onPadPush(uint8_t);
void onPadRelease(uint8_t);
void onPadHold(uint8_t);

DesktopPlatform::DesktopPlatform(SoftwareDisplay& display, const std::filesystem::path& card, bool headless)
    : timer(headless), display(display), storage(card), config(card / ".desktop-config")
{
    buttons.setActionCallback(SwitchGroup::kPress, onButtonPush);
    buttons.setActionCallback(SwitchGroup::kRelease, onButtonRelease);
    buttons.setActionCallback(SwitchGroup::kHold, onButtonHold);
    pads.setActionCallback(SwitchGroup::kPress, onPadPush);
    pads.setActionCallback(SwitchGroup::kRelease, onPadRelease);
    pads.setActionCallback(SwitchGroup::kHold, onPadHold);
}

void DesktopSwitches::set(uint8_t id, bool down, uint32_t now)
{
    auto& key = keys.at(id);
    if (key.down == down)
        return;
    key = {down, false, now};
    if (auto callback = callbacks[down ? kPress : kRelease])
        callback(id);
}

void DesktopSwitches::update(uint32_t now)
{
    if (!callbacks[kHold])
        return;
    for (size_t id = 0; id < keys.size(); ++id)
    {
        auto& key = keys[id];
        if (key.down && !key.held && now - key.pressed >= holdTime)
        {
            key.held = true;
            callbacks[kHold](id);
        }
    }
}

void DesktopLeds::setLed(int id, bool on, int brightness)
{
    if (id >= 0 && id < int(levels.size()))
        levels[id] = on ? std::clamp(brightness, 0, 255) : 0;
}

bool DesktopConfig::readData(int address, void* data, int size)
{
    auto* file = fopen(path.c_str(), "rb");
    if (!file)
        return false;
    const bool ok = fseek(file, address, SEEK_SET) == 0 && fread(data, 1, size, file) == size_t(size);
    fclose(file);
    return ok;
}

void DesktopConfig::writeData(int address, void* data, int size)
{
    auto* file = fopen(path.c_str(), "r+b");
    if (!file)
        file = fopen(path.c_str(), "w+b");
    if (!file)
        return;
    if (fseek(file, address, SEEK_SET) == 0)
        fwrite(data, 1, size, file);
    fclose(file);
}

uint32_t DesktopCrc::calculate32bitCrc(const uint8_t* data, uint16_t size)
{
    uint32_t crc = 0xffffffff;
    while (size--)
    {
        crc ^= *data++;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
    }
    return ~crc;
}

// The desktop loop runs application updates and audio blocks on one thread.
void enableAudioInterrupt() {}
void disableAudioInterrupt() {}

int16_t sin_q15(int16_t value)
{
    return std::clamp(std::lround(32768 * std::sin(value * (6.283185307179586 / 32768))), -32768l, 32767l);
}

extern "C" void restartMCU()
{
    std::exit(0);
}

namespace
{
bool audioMuted = false;
}

uint8_t isAudioOutMuted() { return audioMuted; }
void muteAudioOutByKey() { audioMuted = true; }
void unmuteAudioOutByKey() { audioMuted = false; }
