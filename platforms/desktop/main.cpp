#include "DesktopPlatform.h"
#include "Tracker.h"
#include "AudioStream.h"
#include "config.h"
#include "core/interfaceDefs.h"
#include "FT812.h"
#include "display.h"
#include "fileManager.h"
#include "sampleEditor/SampleEditorChain.h"
#include "mtAudioEngine.h"
#include "mtConfig.h"
#include "core/interface.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <fstream>
#include <sstream>

namespace
{
template <typename T, void (*Destroy)(T*)>
using SdlPtr = std::unique_ptr<T, decltype(Destroy)>;

void check(bool ok)
{
    if (!ok)
        throw std::runtime_error(SDL_GetError());
}

constexpr int panelWidth = 1300, panelHeight = 930;
constexpr int padX = 16, padY = 628, padPitch = 64, padSize = 50;
constexpr SDL_Point wheel{1044, 762};
constexpr int wheelRadius = 105;

struct PanelKey
{
    SDL_Scancode shortcut;
    const char* label;
    const char* hint;
    SDL_Rect bounds;
    uint32_t face = 0x292d33;
};

constexpr PanelKey rightKey(int column, int row, SDL_Scancode shortcut,
                           const char* label, const char* hint = "", uint32_t face = 0x292d33)
{
    return {shortcut, label, hint, {834 + 90 * column, 16 + 90 * row + (row >= 3 ? 56 : 0), 80, 76}, face};
}

constexpr PanelKey keys[] = {
    rightKey(0, 0, SDL_SCANCODE_1, "I. Params", "1"),
    rightKey(1, 0, SDL_SCANCODE_2, "Perform", "2"),
    rightKey(2, 0, SDL_SCANCODE_3, "File", "3"),
    rightKey(3, 0, SDL_SCANCODE_4, "Config", "4"),
    rightKey(4, 0, SDL_SCANCODE_5, "Master", "5"),
    rightKey(0, 1, SDL_SCANCODE_6, "S. Loader", "6"),
    rightKey(1, 1, SDL_SCANCODE_7, "S. Playback", "7"),
    rightKey(2, 1, SDL_SCANCODE_8, "S. Editor", "8"),
    rightKey(3, 1, SDL_SCANCODE_9, "S. Recorder", "9"),
    rightKey(4, 1, SDL_SCANCODE_0, "Song", "0 / F10"),
    rightKey(0, 2, SDL_SCANCODE_N, "Note", "N", 0x246b5b),
    rightKey(1, 2, SDL_SCANCODE_I, "Instrument", "I", 0x77662a),
    rightKey(2, 2, SDL_SCANCODE_X, "FX 1", "X", 0x754a7a),
    rightKey(3, 2, SDL_SCANCODE_V, "FX 2", "V", 0x28616e),
    rightKey(4, 2, SDL_SCANCODE_P, "Pattern", "P / F9"),
    rightKey(0, 3, SDL_SCANCODE_SPACE, "Play", "Space"),
    rightKey(1, 3, SDL_SCANCODE_C, "Copy", "Paste"),
    rightKey(2, 3, SDL_SCANCODE_INSERT, "Insert", "Home"),
    rightKey(3, 3, SDL_SCANCODE_UP, "Up"),
    rightKey(4, 3, SDL_SCANCODE_DELETE, "Delete", "Backspace"),
    rightKey(0, 4, SDL_SCANCODE_R, "Rec", "R", 0x773932),
    rightKey(1, 4, SDL_SCANCODE_LSHIFT, "Shift"),
    rightKey(2, 4, SDL_SCANCODE_LEFT, "Left"),
    rightKey(3, 4, SDL_SCANCODE_DOWN, "Down"),
    rightKey(4, 4, SDL_SCANCODE_RIGHT, "Right"),
};

constexpr uint8_t buttonIds[] = {
    interfaceButtonParams, interfaceButtonPerformance, interfaceButtonFile, interfaceButtonConfig,
    interfaceButtonMaster, interfaceButtonSampleLoad, interfaceButtonSamplePlay, interfaceButtonSampleEdit,
    interfaceButtonSampleRec, interfaceButtonSong, interfaceButtonNote, interfaceButtonInstr,
    interfaceButtonFx1, interfaceButtonFx2, interfaceButtonPattern, interfaceButtonPlay, interfaceButtonCopy,
    interfaceButtonInsert, interfaceButtonUp, interfaceButtonDelete, interfaceButtonRec, interfaceButtonShift,
    interfaceButtonLeft, interfaceButtonDown, interfaceButtonRight,
};
static_assert(std::size(buttonIds) == std::size(keys));

constexpr char padKeys[] = "1234567890-=qwertyuiop[]asdfghjkl;'\\zxcvbnm,./";
constexpr SDL_Scancode padScancodes[] = {
    SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4, SDL_SCANCODE_5, SDL_SCANCODE_6,
    SDL_SCANCODE_7, SDL_SCANCODE_8, SDL_SCANCODE_9, SDL_SCANCODE_0, SDL_SCANCODE_MINUS, SDL_SCANCODE_EQUALS,
    SDL_SCANCODE_Q, SDL_SCANCODE_W, SDL_SCANCODE_E, SDL_SCANCODE_R, SDL_SCANCODE_T, SDL_SCANCODE_Y,
    SDL_SCANCODE_U, SDL_SCANCODE_I, SDL_SCANCODE_O, SDL_SCANCODE_P, SDL_SCANCODE_LEFTBRACKET, SDL_SCANCODE_RIGHTBRACKET,
    SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_F, SDL_SCANCODE_G, SDL_SCANCODE_H,
    SDL_SCANCODE_J, SDL_SCANCODE_K, SDL_SCANCODE_L, SDL_SCANCODE_SEMICOLON, SDL_SCANCODE_APOSTROPHE, SDL_SCANCODE_BACKSLASH,
    SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_C, SDL_SCANCODE_V, SDL_SCANCODE_B, SDL_SCANCODE_N,
    SDL_SCANCODE_M, SDL_SCANCODE_COMMA, SDL_SCANCODE_PERIOD, SDL_SCANCODE_SLASH,
};
static_assert(std::size(padScancodes) == sizeof(padKeys) - 1);
constexpr int padCount = 48;

SDL_Rect softKey(int index) { return {16 + index * 98, 500, 84, 64}; }
SDL_Rect padKey(int index) { return {padX + index % 12 * padPitch, padY + index / 12 * padPitch, padSize, padSize}; }

void drawPanel(SoftwareDisplay& driver, DesktopPlatform& platform, bool padMode)
{
    driver.beginCoProList();
    API_CMD_DLSTART();
    API_SCISSOR_SIZE(panelWidth, panelHeight);
    API_VERTEX_FORMAT(0);
    const auto box = [](SDL_Rect r, uint32_t color)
    {
        API_COLOR(color);
        API_BEGIN(RECTS);
        API_VERTEX2F(r.x, r.y);
        API_VERTEX2F(r.x + r.w - 1, r.y + r.h - 1);
        API_END();
    };
    box({800, 0, panelWidth - 800, panelHeight}, 0x181b20);
    box({0, 480, 800, panelHeight - 480}, 0x181b20);
    const auto button = [&](SDL_Rect r, uint32_t face, const char* label, const char* hint,
                            bool active, uint32_t glow = 0)
    {
        box(r, (active ? 0x62847c : face) + glow);
        API_COLOR(0xf0f3f6);
        API_CMD_TEXT(r.x + r.w / 2, r.y + r.h / 2 - 9, 5, OPT_CENTERX, label);
        API_COLOR(0xb7c0cc);
        API_CMD_TEXT(r.x + r.w / 2, r.y + r.h / 2 + 8, 5, OPT_CENTERX, hint);
    };
    for (int i = 0; i < int(std::size(keys)); ++i)
    {
        const auto& key = keys[i];
        button(key.bounds, key.face, key.label, key.hint, platform.buttons.isButtonPressed(buttonIds[i]));
    }
    for (int i = 0; i < 8; ++i)
    {
        const char label[] = {'F', char('1' + i), 0};
        button(softKey(i), 0x292d33, label, "", platform.buttons.isButtonPressed(i));
    }
    for (int i = 0; i < padCount; ++i)
    {
        const char label[] = {i < int(sizeof(padKeys)) - 1 ? padKeys[i] : ' ', 0};
        const int level = std::min<int>(platform.leds.levels[i], 31);
        // Give dim hardware LED levels enough contrast against the on-screen pad face.
        const uint32_t glow = level > 0 ? 0x010101 * (24 + 3 * level) : 0;
        button(padKey(i), 0x343841, padMode ? label : "", "", platform.pads.isButtonPressed(i), glow);
    }
    API_COLOR(0x303640);
    API_POINT_SIZE(wheelRadius * 16);
    API_BEGIN(2); // FT812 POINTS
    API_VERTEX2F(wheel.x, wheel.y);
    API_END();
    API_COLOR(0xe0e5eb);
    API_CMD_TEXT(wheel.x, wheel.y - 8, 1, OPT_CENTERX, "-     JOG     +");
    API_CMD_TEXT(16, 898, 1, 0, padMode ? "PAD MODE  /  Tab: command mode" : "COMMAND MODE  /  Tab: pad mode");
    API_CMD_TEXT(834, 898, 5, 0, "Mouse wheel: jog   Esc: quit");
    API_DISPLAY();
    API_CMD_SWAP();
    driver.endCoProList();
}

struct Input
{
    int id = -1;
    bool pad = false;
};

Input keyboard(SDL_Scancode key, bool padMode)
{
    if (key >= SDL_SCANCODE_F1 && key <= SDL_SCANCODE_F8)
        return {key - SDL_SCANCODE_F1};
    if (key == SDL_SCANCODE_F9)
        return {interfaceButtonPattern};
    if (key == SDL_SCANCODE_F10)
        return {interfaceButtonSong};
    if (key == SDL_SCANCODE_RSHIFT)
        key = SDL_SCANCODE_LSHIFT;
    if (key == SDL_SCANCODE_BACKSPACE)
        key = SDL_SCANCODE_DELETE;
    if (padMode)
        for (int i = 0; i < int(std::size(padScancodes)); ++i)
            if (key == padScancodes[i])
                return {i, true};
    for (int i = 0; i < int(std::size(keys)); ++i)
        if (keys[i].shortcut == key)
            return {buttonIds[i]};
    return {};
}

Input hit(SDL_Point point)
{
    for (int i = 0; i < int(std::size(keys)); ++i)
        if (SDL_PointInRect(&point, &keys[i].bounds))
            return {buttonIds[i]};
    for (int i = 0; i < 8; ++i)
    {
        const auto rect = softKey(i);
        if (SDL_PointInRect(&point, &rect))
            return {i};
    }
    for (int i = 0; i < padCount; ++i)
    {
        const auto rect = padKey(i);
        if (SDL_PointInRect(&point, &rect))
            return {i, true};
    }
    return {};
}

struct Options
{
    const char* screenshot = nullptr;
    const char* card = nullptr;
    const char* script = nullptr;
    bool panel = false, audio = true;
    int tap = -1;
    int scale = 3;
};

void send(DesktopPlatform& platform, Input input, bool down)
{
    if (input.id >= 0)
        (input.pad ? platform.pads : platform.buttons).set(input.id, down, platform.timer.millis());
}

} // namespace

void onPotChange(uint8_t, int16_t);
void forceSaveConfig();
uint8_t isAudioOutMuted();

namespace
{
int run(const Options& options)
{
    const bool headless = options.screenshot != nullptr;
    const bool panel = !headless || options.panel;
    check(SDL_Init(headless ? 0 : SDL_INIT_VIDEO) == 0);
    struct SdlSession { ~SdlSession() { SDL_Quit(); } } session;
    std::filesystem::path card;
    if (options.card)
        card = options.card;
    else
    {
        std::unique_ptr<char, decltype(&SDL_free)> path(SDL_GetPrefPath("TrackerCE", "Desktop"), SDL_free);
        check(bool(path));
        card = std::filesystem::path(path.get()) / "SD";
        const auto previousCard = card.parent_path().parent_path() / "Simulator" / "SD";
        if (!std::filesystem::exists(card) && std::filesystem::is_directory(previousCard))
            std::filesystem::rename(previousCard, card);
    }

    const int width = panel ? panelWidth : SoftwareDisplay::width;
    const int height = panel ? panelHeight : SoftwareDisplay::height;
    SdlPtr<SDL_Surface, SDL_FreeSurface> surface(
        SDL_CreateRGBSurfaceWithFormat(0, width * options.scale, height * options.scale,
                                       32, SDL_PIXELFORMAT_ARGB8888), SDL_FreeSurface);
    check(bool(surface));
    SdlPtr<SDL_Renderer, SDL_DestroyRenderer> renderer(SDL_CreateSoftwareRenderer(surface.get()),
                                                     SDL_DestroyRenderer);
    check(bool(renderer));
    check(SDL_RenderSetScale(renderer.get(), options.scale, options.scale) == 0);
    SoftwareDisplay driver(*renderer);
    DesktopPlatform platform(driver, card, headless);
    auto& tracker = *Tracker::getInstance();
    tracker.getStartDisplayFunc()(&platform);
    tracker.init(&platform);

    constexpr int sampleRate = 44118;
    constexpr size_t frames = AUDIO_BLOCK_SAMPLES;
    SDL_AudioDeviceID audio = 0;
    if (!headless && options.audio && SDL_InitSubSystem(SDL_INIT_AUDIO) == 0)
    {
        SDL_AudioSpec spec{};
        spec.freq = sampleRate;
        spec.format = AUDIO_S16SYS;
        spec.channels = 2;
        spec.samples = 512;
        audio = SDL_OpenAudioDevice(nullptr, 0, &spec, nullptr, 0);
    }
    if (!headless && options.audio && !audio)
        std::fprintf(stderr, "Audio disabled: %s\n", SDL_GetError());

    constexpr uint32_t bytesPerBlock = frames * 2 * sizeof(int16_t);
    constexpr uint32_t queueTarget = sampleRate * 60 / 1000 * 2 * sizeof(int16_t);
    uint64_t audioTime = platform.timer.timeUs();
    bool audioStarted = false;
    const auto pumpAudio = [&]
    {
        const uint64_t now = platform.timer.timeUs();
        uint32_t queued = audio ? SDL_GetQueuedAudioSize(audio) : 0;
        if (!audio && now > audioTime + 100000)
            audioTime = now;
        const float volume = isAudioOutMuted() ? 0.0f : std::clamp(platform.volume(), 0.0f, 1.0f);
        while (audio ? queued < queueTarget : now >= audioTime)
        {
            int16_t left[frames]{}, right[frames]{};
            tracker.updateAudioBuffers(left, right, frames);
            if (!audio)
            {
                audioTime += frames * 1000000 / sampleRate;
                continue;
            }

            int16_t stereo[2 * frames];
            for (size_t i = 0; i < frames; ++i)
            {
                stereo[2 * i] = left[i] * volume;
                stereo[2 * i + 1] = right[i] * volume;
            }
            check(SDL_QueueAudio(audio, stereo, sizeof(stereo)) == 0);
            queued += bytesPerBlock;
        }
        if (audio && !audioStarted)
        {
            SDL_PauseAudioDevice(audio, 0);
            audioStarted = true;
        }
    };

    const auto update = [&]
    {
        const auto now = platform.timer.millis();
        platform.buttons.update(now);
        platform.pads.update(now);
        tracker.update();
        pumpAudio();

        // The software coprocessor is synchronous. Process several queued
        // display stages before yielding so a module switch does not wait one
        // SDL loop per stage. Keep the batch bounded for input and audio.
        const uint64_t displayDeadline = platform.timer.timeUs() + 3000;
        for (int stage = 0; stage < 16 && !display.isIdle() &&
                            platform.timer.timeUs() < displayDeadline; ++stage)
            display.update();
    };

    if (headless)
    {
        for (int i = 0; i < 4000 && platform.running; ++i)
        {
            if (i == 2500)
                send(platform, {options.tap}, true);
            if (i == 2560)
                send(platform, {options.tap}, false);
            update();
            platform.timer.advance(1);
        }
        if (options.script)
        {
            const auto advance = [&](unsigned ms)
            {
                for (unsigned i = 0; i < ms && platform.running; ++i)
                { update(); platform.timer.advance(1); }
            };
            std::ofstream frameAudit;
            std::ifstream script(options.script);
            if (!script) throw std::runtime_error("Cannot open desktop script");
            std::string line;
            while (std::getline(script, line))
            {
                if (line.empty() || line[0] == '#') continue;
                std::istringstream fields(line);
                std::string command; fields >> command;
                if (command == "audit-frames")
                {
                    std::string path; std::getline(fields >> std::ws, path);
                    frameAudit.open(path);
                    if (!frameAudit) throw std::runtime_error("Cannot open frame audit");
                    driver.frameObserver = [&](const auto& texts) {
                        frameAudit << "frame\n";
                        for (const auto& text : texts)
                            frameAudit << text.x << '\t' << text.y << '\t' << text.value << '\n';
                    };
                }
                else if (command == "wait")
                { unsigned ms = 0; fields >> ms; advance(std::min(ms, 60000u)); }
                else if (command == "encoder")
                { int value = 0; fields >> value; onPotChange(0, value); advance(50); }
                else if (command == "import")
                {
                    std::string path; unsigned slot = 0; fields >> slot >> path;
                    const auto slash = path.find_last_of('/');
                    std::string directory = slash == std::string::npos ? "/" : path.substr(0, slash);
                    std::string name = path.substr(slash == std::string::npos ? 0 : slash + 1);
                    if (name.size() > 4 && name.substr(name.size() - 4) == ".wav") name.resize(name.size() - 4);
                    newFileManager.clearStatus();
                    if (slot >= kSampleInstrumentsCount || !newFileManager.importSampleToProject(directory.data(), name.data(), slot))
                        throw std::runtime_error("Desktop script sample import failed");
                    advance(2000);
                    if (newFileManager.getStatus() >= fmError) throw std::runtime_error("Sample import error");
                    newFileManager.clearStatus();
                    mtProject.values.lastUsedInstrument = slot;
                }
                else if (command == "button" || command == "press" || command == "release")
                {
                    std::string name; std::getline(fields >> std::ws, name);
                    int id = -1;
                    const bool pad = name.starts_with("pad ");
                    if (pad)
                    {
                        std::istringstream padNumber(name.substr(4));
                        if (!(padNumber >> id) || id >= padCount) id = -1;
                    }
                    else if (name.size() == 1 && name[0] >= '0' && name[0] <= '7') id = name[0] - '0';
                    else for (unsigned i = 0; i < std::size(keys); ++i)
                        if (name == keys[i].label) id = buttonIds[i];
                    if (id < 0) throw std::runtime_error("Unknown desktop script button");
                    if (command != "release") { send(platform, {id, pad}, true); advance(60); }
                    if (command != "press") { send(platform, {id, pad}, false); advance(80); }
                }
                else if (command == "assert-perf-fx")
                {
                    unsigned column = 0;
                    int level = -1;
                    if (!(fields >> column >> level) || column >= perffx::kColumns ||
                        engine.getAudioOutput()->getPerfFx().rack().soundingLevel(column) != level)
                        throw std::runtime_error("Unexpected sounding Perf FX level: " + line);
                    if (mtInterface.isPerformanceModeScreenActive())
                    {
                        const bool inMiddleGroup = column >= 4 && column < 8;
                        for (unsigned row = 0; row < perffx::kLevels; ++row)
                        {
                            int brightness = inMiddleGroup ? mtConfig.values.padsLightBackDim : 0;
                            if (static_cast<int>(row) == level) brightness = mtConfig.values.padsLightBack;
                            if (platform.leds.levels[column + row * perffx::kColumns] != brightness)
                                throw std::runtime_error("Unexpected Perf FX pad light: " + line);
                        }
                    }
                }
                else if (command == "assert-muted")
                {
                    unsigned track = 0, muted = 0;
                    if (!(fields >> track >> muted) || track >= kAllTracks ||
                        (track < kSampleTracks ? mtProject.values.trackMute[track]
                                              : mtProject.values.midiTrackMute[track - kSampleTracks]) != muted)
                        throw std::runtime_error("Unexpected track mute: " + line);
                }
                else if (command == "assert-perf-fx-mix")
                {
                    unsigned percent = 0;
                    if (!(fields >> percent) || getPerfFxMix() != percent)
                        throw std::runtime_error("Unexpected Perf FX Mix: " + line);
                }
                else if (command == "assert-sample")
                {
                    unsigned slot = 0, frame = 0;
                    int expected = 0;
                    if (!(fields >> slot >> frame >> expected) || slot >= kSampleInstrumentsCount ||
                        !mtProject.instrument[slot].isActive || frame >= mtProject.sampleMemory[slot].ramFrames ||
                        mtProject.sampleMemory[slot].address[0][frame] != expected)
                        throw std::runtime_error("Unexpected resident sample PCM: " + line);
                }
                else if (command == "assert-chain")
                {
                    std::string state; fields >> state;
                    if (sampleChainIsOpen() != (state == "open"))
                        throw std::runtime_error("Unexpected sample chain screen state");
                }
                else if (command == "assert-view")
                {
                    std::string view; fields >> view;
                    const bool active = view == "Master" ? mtInterface.isMasterScreenActive() :
                                        view == "Pattern" ? mtInterface.isPatternScreenActive() :
                                        view == "Perform" ? mtInterface.isPerformanceModeScreenActive() : false;
                    if (!active) throw std::runtime_error("Unexpected active view: " + view);
                }
                else if (command == "assert-chain-size")
                {
                    unsigned count; fields >> count;
                    if (sampleChainStageCount() != count)
                        throw std::runtime_error("Unexpected draft chain size");
                }
                else if (command == "assert-preview")
                {
                    std::string state; fields >> state;
                    if (engine.getSampleFxPreview()->isPlaying() != (state == "live"))
                        throw std::runtime_error("Unexpected live preview state");
                }
                else if (command == "assert-sequencer")
                {
                    std::string state; fields >> state;
                    if ((sequencer.getSeqState() != Sequencer::SEQ_STATE_STOP) != (state == "playing"))
                        throw std::runtime_error("Unexpected sequencer state");
                }
                else if (command == "snapshot")
                {
                    std::string path; std::getline(fields >> std::ws, path);
                    check(SDL_SaveBMP(surface.get(), path.c_str()) == 0);
                }
                else throw std::runtime_error("Unknown desktop script command");
            }
            driver.frameObserver = {};
        }
        if (panel)
            drawPanel(driver, platform, false);
        check(SDL_SaveBMP(surface.get(), options.screenshot) == 0);
        return 0;
    }

    SdlPtr<SDL_Window, SDL_DestroyWindow> window(
        SDL_CreateWindow("Tracker CE Desktop", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                         width, height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI), SDL_DestroyWindow);
    check(bool(window));

    std::array<Input, SDL_NUM_SCANCODES> pressed{};
    Input mouse;
    bool padMode = false, latchedShift = false;
    SDL_Rect dest{0, 0, width, height};
    int outputWidth = width, outputHeight = height;
    uint32_t lastPresent = 0;
    bool panelDirty = true;
    auto shownLeds = platform.leds.levels;
    while (platform.running)
    {
        bool inputChanged = false;
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            inputChanged |= event.type == SDL_KEYDOWN || event.type == SDL_KEYUP ||
                            event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP ||
                            event.type == SDL_MOUSEWHEEL;
            switch (event.type)
            {
                case SDL_QUIT: platform.running = false; break;
                case SDL_KEYDOWN:
                case SDL_KEYUP:
                {
                    const auto key = event.key.keysym.scancode;
                    const bool down = event.type == SDL_KEYDOWN;
                    if (event.key.repeat)
                        break;
                    if (down && key == SDL_SCANCODE_ESCAPE)
                    {
                        platform.running = false;
                        break;
                    }
                    if (down && (key == SDL_SCANCODE_TAB || key == SDL_SCANCODE_CAPSLOCK))
                    {
                        padMode = !padMode;
                        break;
                    }
                    if (down && (key == SDL_SCANCODE_PAGEUP || key == SDL_SCANCODE_PAGEDOWN ||
                                 (!padMode && (key == SDL_SCANCODE_LEFTBRACKET || key == SDL_SCANCODE_RIGHTBRACKET))))
                    {
                        onPotChange(0, key == SDL_SCANCODE_PAGEUP || key == SDL_SCANCODE_RIGHTBRACKET ? 1 : -1);
                        break;
                    }
                    if (down)
                        pressed[key] = keyboard(key, padMode);
                    if (pressed[key].id != interfaceButtonShift || !latchedShift)
                        send(platform, pressed[key], down);
                    if (!down)
                        pressed[key] = {};
                    break;
                }
                case SDL_MOUSEWHEEL:
                    onPotChange(0, event.wheel.y * (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1));
                    break;
                case SDL_MOUSEBUTTONDOWN:
                {
                    int windowWidth, windowHeight;
                    SDL_GetWindowSize(window.get(), &windowWidth, &windowHeight);
                    if (windowWidth <= 0 || windowHeight <= 0)
                        break;
                    const SDL_Point click{event.button.x * outputWidth / windowWidth,
                                          event.button.y * outputHeight / windowHeight};
                    if (!SDL_PointInRect(&click, &dest))
                        break;
                    const SDL_Point point{(click.x - dest.x) * width / dest.w, (click.y - dest.y) * height / dest.h};
                    const auto control = hit(point);
                    if (event.button.button == SDL_BUTTON_RIGHT && control.id == interfaceButtonShift && !control.pad)
                    {
                        latchedShift = !latchedShift;
                        send(platform, control, latchedShift);
                        break;
                    }
                    if (event.button.button != SDL_BUTTON_LEFT)
                        break;
                    mouse = control;
                    send(platform, mouse, true);
                    const int dx = point.x - wheel.x, dy = point.y - wheel.y;
                    if (dx * dx + dy * dy <= wheelRadius * wheelRadius)
                        onPotChange(0, dx < 0 ? -1 : 1);
                    break;
                }
                case SDL_MOUSEBUTTONUP:
                    if (event.button.button == SDL_BUTTON_LEFT)
                    {
                        if (mouse.id != interfaceButtonShift || mouse.pad || !latchedShift)
                            send(platform, mouse, false);
                        mouse = {};
                    }
                    break;
            }
        }

        panelDirty |= inputChanged;
        update();
        const auto now = platform.timer.millis();
        if (inputChanged || now - lastPresent >= 16)
        {
            lastPresent = now;
            if (panelDirty || shownLeds != platform.leds.levels)
            {
                drawPanel(driver, platform, padMode);
                panelDirty = false;
                shownLeds = platform.leds.levels;
            }
            auto* output = SDL_GetWindowSurface(window.get());
            check(output != nullptr);
            outputWidth = output->w;
            outputHeight = output->h;
            const double scale = std::min(double(outputWidth) / width, double(outputHeight) / height);
            const int w = width * scale, h = height * scale;
            dest = {(output->w - w) / 2, (output->h - h) / 2, w, h};
            SDL_FillRect(output, nullptr, 0);
            SDL_BlitScaled(surface.get(), nullptr, output, &dest);
            SDL_UpdateWindowSurface(window.get());
        }
        SDL_Delay(1);
    }
    if (audio)
        SDL_CloseAudioDevice(audio);
    forceSaveConfig();
    return 0;
}
}

int main(int argc, char** argv)
{
    Options options;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if (arg == "--sd" && i + 1 < argc)
            options.card = argv[++i];
        else if (arg == "--script" && i + 1 < argc)
            options.script = argv[++i];
        else if (arg == "--screenshot" && i + 1 < argc)
            options.screenshot = argv[++i];
        else if (arg == "--tap-button" && i + 1 < argc)
        {
            const std::string_view name = argv[++i];
            const auto* key = std::find_if(std::begin(keys), std::end(keys),
                                           [&](const auto& key) { return key.label == name; });
            if (key == std::end(keys))
            {
                std::fprintf(stderr, "Unknown button: %s\n", argv[i]);
                return 2;
            }
            options.tap = buttonIds[key - keys];
        }
        else if (arg == "--scale" && i + 1 < argc)
        {
            const std::string_view scale = argv[++i];
            if (scale != "1" && scale != "2" && scale != "3")
            {
                std::fprintf(stderr, "--scale must be 1, 2, or 3\n");
                return 2;
            }
            options.scale = scale.front() - '0';
        }
        else if (arg == "--panel")
            options.panel = true;
        else if (arg == "--no-audio")
            options.audio = false;
        else
        {
            std::fprintf(stderr, "Usage: %s [--sd DIR] [--no-audio] [--scale 1|2|3] [--screenshot FILE.bmp [--panel] [--tap-button LABEL]]\n",
                         argv[0]);
            return 2;
        }
    }
    try
    {
        return run(options);
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "tracker-ce-desktop: %s\n", error.what());
        return 1;
    }
}
