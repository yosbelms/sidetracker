#pragma once

#include "DisplayDriver.h"

#include <SDL.h>
#include <array>
#include <vector>
#include <functional>
#include <string>

class SoftwareDisplay final : public DisplayDriver
{
public:
    static constexpr int width = 800, height = 480;
    explicit SoftwareDisplay(SDL_Renderer& renderer);
    struct FrameText { int x, y; std::string value; };
    // Optional headless UI audit: observe the text actually submitted at swap.
    std::function<void(const std::vector<FrameText>&)> frameObserver;

    void setPowerState(bool) override {}
    void setBrightness(int) override {}
    void setRotation(int) override {}
    void getImgProps(uint32_t& address, uint32_t& w, uint32_t& h) override { address = w = h = 0; }
    uint32_t getDisplayListOffset() override { return list.size() * sizeof(uint32_t); }
    void IncCMDOffset(uint16_t) override {}
    void WriteData32(uint32_t data) override { commands.push_back(data); }
    void beginCoProList() override { commands.clear(); }
    void beginCoProListNoCheck() override { beginCoProList(); }
    void endCoProList() override;
    void awaitCoProEmpty() override {}
    uint8_t isCoProEmpty() override { return 1; }
    void writeDataRAMG(const uint8_t* data, uint32_t size, uint32_t address) override;
    void writeFontRAMG(const uint8_t* data, uint32_t size, uint32_t address) override;
    void readDataRAMG(uint8_t* data, uint32_t size, uint32_t address) override;
    void readBackDataRAMG(uint8_t* data, uint32_t size, uint32_t address) override
    {
        readDataRAMG(data, size, address);
    }
    uint8_t sendString(const char* string, uint32_t length) override;
    void writeDataToCMD(const uint8_t*, uint32_t) override {}
    void initStreamDma(uint16_t, uint16_t, uint16_t, uint8_t*, uint32_t) override {}
    bool updateStreamDma(bool) override { return false; }
    void stopStreamDma() override {}

private:
    struct Bitmap
    {
        uint32_t source = 0;
        int format = 0, stride = 0, height = 0, drawWidth = 0, drawHeight = 0;
        int firstChar = 0;
        uint32_t metrics = 0;
        bool font = false;
    };
    struct BitmapTransform
    {
        double a = 1, b = 0, c = 0;
        double d = 0, e = 1, f = 0;

        void translate(double x, double y);
        void rotate(uint16_t angle);
    };

    struct Context
    {
        uint32_t color = 0xffffff, clear = 0;
        uint8_t alpha = 255;
        SDL_BlendMode blend = SDL_BLENDMODE_BLEND;
        int prim = 0, fraction = 4, handle = 0, cell = 0;
        int lineWidth = 16, pointSize = 16;
        BitmapTransform transform;
        SDL_Rect clip{0, 0, width, height};
    };

    void displayWord(uint32_t word);
    void vertex(int x, int y, int handle, int cell);
    void bitmap(int x, int y, int handle, int cell);
    void text(int x, int y, int handle, int options, const char* value);
    void color(uint32_t rgb, uint8_t alpha = 255);
    void setClip();
    void rect(int x0, int y0, int x1, int y1);
    void circle(int x, int y, int radius);

    SDL_Renderer& renderer;
    std::vector<uint8_t> ram;
    std::vector<uint32_t> commands, list;
    std::vector<FrameText> frameTexts;
    std::array<Bitmap, 32> bitmaps{};
    std::vector<Context> contexts;
    Context state;
    BitmapTransform matrix;
    int previousX = 0, previousY = 0;
    bool hasVertex = false;
};
