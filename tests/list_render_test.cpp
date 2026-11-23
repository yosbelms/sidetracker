#include "display.h"
#include "FT812.h"
#include "Tracker.h"
#include "fake_devices.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>

// Run the real list control with a text sink in place of the display chip.
// ASan checks the row-pointer reads; copying each label also checks its lifetime.
namespace {
std::vector<std::string> drawnText;
class ListPlatform final : public Platform {
public:
    void setPowerMode(bool) override {}
    void resetMCU() override {}
    void runBootloader(char*) override {}
    void setPowerButtonCallback(PowerButtonCallback) override {}
    bool getPowerButtonState() override { return false; }
    bool readForceStartUp() override { return false; }
    Timer* getTimer() override { return &ce::test::testClock(); }
#define UNUSED_DEVICE(T, name) T* name() override { return nullptr; }
    UNUSED_DEVICE(Sgtl5000, getCodecAudio)
    UNUSED_DEVICE(Radio, getRadio)
    UNUSED_DEVICE(SwitchGroup, getButtons)
    UNUSED_DEVICE(SwitchGroup, getPads)
    UNUSED_DEVICE(Leds, getLeds)
    UNUSED_DEVICE(Battery, getBattery)
    UNUSED_DEVICE(Encoder, getEncoder)
    UNUSED_DEVICE(DisplayDriver, getDisplayDriver)
    UNUSED_DEVICE(UsbAudio, getUsbAudio)
    UNUSED_DEVICE(UartMidi, getUartMidi)
    UNUSED_DEVICE(UsbMidi, getUsbMidi)
    UNUSED_DEVICE(ConfigManager, getConfigManager)
    UNUSED_DEVICE(FileSystem, getFileSystem)
    UNUSED_DEVICE(CrcCalculator, getCrcCalculator)
    UNUSED_DEVICE(SystemInfo, getSystemInfo)
    UNUSED_DEVICE(Usb, getUsb)
#undef UNUSED_DEVICE
};
class ListTracker final : public Tracker {
public:
    void init(Platform*) override {}
    void update() override {}
    void updateAudioBuffers(void*, void*, int) override {}
    void updateInputAudioBuffers(void*, void*, int) override {}
    void (*getStartDisplayFunc())(Platform*) override { return nullptr; }
    Platform* getPlatform() override { static ListPlatform platform; return &platform; }
};
strControlProperties properties(strList& data) {
    strControlProperties p{};
    p.data = &data;
    p.style = controlStyleFont1;
    p.w = 197;
    p.h = 394;
    return p;
}
void animate(cList& control, int target) {
    control.setValue(target);
    for (int frame = 0; frame < 128; ++frame) control.update();
}
}
Tracker* Tracker::getInstance() { static ListTracker tracker; return &tracker; }
const Theme themeDefault{};
strFont fonts[5]{};
const uint8_t displayFontCount = 5;
const strBitmap bitmaps[displayBitmapsCount]{};
cDisplay::cDisplay() { theme = &themeDefault; }
cDisplay display;
uint16_t getTextWidth(const strFont*, const char*, uint16_t length) { return length * 8; }
void API_CMD_TEXT(int16_t, int16_t, int16_t, uint16_t, const char* text, uint32_t) {
    drawnText.emplace_back(text ? text : "");
}
void API_COLOR(uint32_t) {}
void API_VERTEX2F(int16_t, int16_t) {}
void API_LINE_WIDTH(uint16_t) {}
void API_BEGIN(uint8_t) {}
void API_END() {}
void API_SAVE_CONTEXT() {}
void API_RESTORE_CONTEXT() {}
void API_BLEND_FUNC(uint8_t, uint8_t) {}
void API_SCISSOR_XY(uint16_t, uint16_t) {}
void API_SCISSOR_SIZE(uint16_t, uint16_t) {}
void API_CMD_GRADIENT(int16_t, int16_t, uint32_t, int16_t, int16_t, uint32_t) {}

TEST(ListRenderTest, ExpiredFontIndexDoesNotReadBeyondRescannedNames) {
    char original[] = "Original", win98[] = "Win98";
    char* names[] = {original, win98};
    strList data{};
    data.start = 3; // The previously selected font was fourth before the rescan.
    data.length = 2;
    data.linesCount = 14;
    data.data = names;
    auto p = properties(data);
    cList control(&p);
    drawnText.clear();
    animate(control, 1);
    EXPECT_EQ(data.start, 1);
    ASSERT_FALSE(drawnText.empty());
    for (const auto& text : drawnText) EXPECT_TRUE(text == "Original" || text == "Win98");
}
TEST(ListRenderTest, ExtraScrollRowNeverReadsPastShortenedTable) {
    char first[] = "First", second[] = "Second";
    char* names[] = {first, second};
    strList data{};
    data.length = 2;
    data.linesCount = 14;
    data.data = names;
    auto p = properties(data);
    cList control(&p);
    // A list shrinks while the old upward scroll state still has an extra row.
    data.start = 2;
    control.barPos = 1;
    control.textListPos = 0;
    animate(control, 1);
    EXPECT_EQ(data.start, 1);
}
TEST(ListRenderTest, EmptyAndUnboundListsDrawNoLabels) {
    strList data{};
    auto p = properties(data);
    cList control(&p);
    drawnText.clear();
    control.update();
    EXPECT_TRUE(drawnText.empty());
    EXPECT_EQ(control.listPosition, 0);
    EXPECT_EQ(control.textListPos, 0);
    cList unbound;
    unbound.update();
}
TEST(ListRenderTest, ScrolledListAndRapidDirectionChangesStayInBounds) {
    char name[] = "Choice";
    std::vector<char*> names(32, name);
    strList data{};
    data.length = names.size();
    data.linesCount = 14;
    data.data = names.data();
    auto p = properties(data);
    cList control(&p);
    for (int target : {31, 0, 20, 1, 30, 15, 0}) {
        animate(control, target);
        EXPECT_EQ(data.start, target);
    }
    for (int i = 0; i < 500; ++i) {
        control.setValue((i * 13) % 32);
        control.update();
    }
}

void API_BITMAP_SIZE(uint8_t, uint8_t, uint8_t, uint16_t, uint16_t) {}
void API_BITMAP_LAYOUT(uint8_t, uint16_t, uint16_t) {}
void API_BITMAP_SOURCE(uint32_t) {}
