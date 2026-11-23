#include "../core/interface/configEditor/configMenu.h"
#include <gtest/gtest.h>

TEST(ConfigMenuTest, LoadedTrackCountOutsideTableDoesNotReadPastChoices)
{
    cMenuGroup root(root, "Config");
    uint8_t value = 3;
    const char* choices[] = {"4", "8", "12", "16"};
    strItemTypeListTextWithAction setup{&value, 4, choices};
    cMenuItem item(root, "Max tracks", "maxTracks", menuTypeItemListTextWithAction, &setup);
    EXPECT_STREQ(item.getValue(), "16");
    value = 255; // A persisted value arrives after the menu constructor.
    EXPECT_STREQ(item.getValue(), "");
    EXPECT_EQ(value, 255); // Opening Config must not change active project tracks.
}

TEST(ConfigMenuTest, RemovedFontAfterRescanDoesNotReadAnExpiredChoice)
{
    cMenuGroup root(root, "Config");
    uint8_t value = 3;
    const char* original[] = {"Original", "New", "Snes", "Win98"};
    strItemTypeListTextWithAction setup{&value, 4, original};
    cMenuItem item(root, "Display font", "displFont", menuTypeItemListTextWithAction, &setup);
    EXPECT_STREQ(item.getValue(), "Win98");
    const char* rescanned[] = {"Original", "Snes", "Win98"};
    setup.count = 3;
    setup.ptrText = rescanned;
    EXPECT_STREQ(item.getValue(), "");
    value = 2;
    EXPECT_STREQ(item.getValue(), "Win98");
}

TEST(ConfigMenuTest, PlainListsRejectLoadedInvalidValuesAndMissingChoices)
{
    cMenuGroup root(root, "Config");
    uint8_t value = 0;
    const char* choices[] = {"Off", nullptr};
    strItemTypeListText setup{&value, 2, choices};
    cMenuItem item(root, "Option", "", menuTypeItemListText, &setup);
    EXPECT_STREQ(item.getValue(), "Off");
    value = 1;
    EXPECT_STREQ(item.getValue(), "");
    value = 2;
    EXPECT_STREQ(item.getValue(), "");
    value = 0;
    setup.count = 0;
    EXPECT_STREQ(item.getValue(), "");
    setup.count = 1;
    setup.ptrText = nullptr;
    EXPECT_STREQ(item.getValue(), "");
    setup.value = nullptr;
    EXPECT_STREQ(item.getValue(), "");
}
