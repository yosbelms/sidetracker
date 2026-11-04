#include "SampleEditorChain.h"
#include "sampleEditor.h"
#include "SampleFxChain.h"
#include "ptfx_manager.h"
#include "fileManager.h"
#include "fileManagerDefs.h"
#include "mtAudioEngine.h"
#include "mtPadBoard.h"
#include "VoiceStreamManager.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
using namespace samplefx;
constexpr uint32_t kCapacity = EDIT_BUFF_SIZE / 2;
constexpr unsigned kIoFrames = 4096;
constexpr char kTempDir[] = "Workspace/FxChainTemp";
constexpr char kRecipeDir[] = "/Effects/Chains";

bool supported(uint8_t effect)
{
    return effect < kBuiltInEffectsCount && effect != editorEffectCut &&
           effect != editorEffectReverse && effect != editorEffectWavetableSmoother &&
           effect != editorEffectTimestretchBeat && effect != editorEffectTimestretchNote;
}

int findPlugin(const Node& n)
{
    for (unsigned i = 0; i < PtfxManager::getPluginCount(); ++i)
        if (!strcmp(PtfxManager::getPlugin(i)->filename, n.filename)) return i;
    return -1;
}

bool fileFingerprint(FileSystem* fs, const char* path, uint32_t& hash)
{
    auto [ok, file] = fs->openFile(path);
    if (!ok) return false;
    uint8_t bytes[1024];
    size_t remaining = fs->getFileSize(file);
    hash = 2166136261u;
    while (remaining)
    {
        const size_t count = std::min(remaining, sizeof(bytes));
        if (fs->readFile(bytes, count, file) != int32_t(count))
        { fs->closeFile(file); return false; }
        hash = fingerprint(bytes, count, hash);
        remaining -= count;
    }
    fs->closeFile(file);
    return true;
}

class EffectBackend : public Backend
{
  public:
    FileSystem* fs = nullptr;
    mtEffect* effect = nullptr;
    Audio* output = nullptr;
    uint32_t completed = 0;
    const char* message = "Effect failed";

    bool resolve(const Node& node)
    {
        message = "Effect unavailable";
        if (node.effect != kPlugin)
        {
            if (!supported(node.effect)) return false;
            effect = sampleEditorEffect[node.effect];
            return node.count == effectDisplayConstantParams[node.effect].paramsNumber;
        }
        int index = findPlugin(node);
        if (index < 0) { message = "Plugin missing: remove or bypass"; return false; }
        char path[64];
        snprintf(path, sizeof(path), "/Effects/%s", node.filename);
        uint32_t hash;
        if (!fileFingerprint(fs, path, hash) || hash != node.fingerprint)
        { message = "Plugin changed: remove and add again"; return false; }
        if (!PtfxManager::loadPlugin(index, sampleEditorModular)) return false;
        const auto& header = PtfxManager::getActiveHeader();
        if (header.num_params != node.count || header.flags != node.flags) return false;
        effect = &sampleEditorModular;
        return true;
    }

    bool begin(const Node& node, const Audio& input, Audio& out, uint32_t capacity,
               uint32_t generatorFrames) override
    {
        if (!resolve(node)) return false;
        const bool generator = node.effect == kPlugin && (node.flags & PTFX_FLAG_GENERATOR);
        int16_t* src[2] = { input.channel[0], input.channels == 2 ? input.channel[1] : input.channel[0] };
        // Mono built-ins still visit the platform's second channel; its output is private.
        effect->bindOffline(src, out.channel, generator ? 0 : input.frames);
        uint8_t channels = generator ? ((node.flags & PTFX_FLAG_STEREO) ? mtEffect::kChannels : 1)
                                     : input.channels;
        if (node.effect == kPlugin && channels == 2 && !(node.flags & PTFX_FLAG_STEREO))
        { message = "Plugin requires a mono source"; return false; }
        effect->setOfflineChannels(channels);
        for (unsigned i = 0; i < node.count; ++i)
        {
            float value = node.params[i];
            const float low = node.effect == kPlugin ? PtfxManager::getActiveHeader().params[i].min_val
                                                    : effectDisplayConstantParams[node.effect].downConstrain[i];
            const float high = node.effect == kPlugin ? PtfxManager::getActiveHeader().params[i].max_val
                                                     : effectDisplayConstantParams[node.effect].upConstrain[i];
            if (!std::isfinite(value) || value < low || value > high)
            { message = "Parameter outside supported range"; return false; }
            effect->setParameter(value, i);
        }
        if (node.effect == kPlugin) sampleEditorModular.setTargetLength(generatorFrames);
        // Built-ins know their length before start; plugin init can request extra samples.
        if (node.effect != kPlugin && effect->getExpectedProcessLength() > capacity)
        { message = "Effect output too long"; return false; }
        if (!effect->startProcess())
        { message = effect->getExpectedProcessLength() > capacity ? "Effect output too long" : "Effect could not prepare"; return false; }
        out.frames = effect->getExpectedProcessLength();
        out.channels = (node.flags & PTFX_FLAG_CONVERT_TO_MONO) ? 1 : channels;
        if (!out.frames || out.frames > capacity)
        { message = "Effect output too long"; return false; }
        output = &out;
        completed = 0;
        return true;
    }

    bool step(bool& done) override
    {
        const int32_t count = effect->processBlock();
        if (effect->hasProcessingError() || count < 0 ||
            uint32_t(count) > output->frames - completed)
        { message = "Effect returned invalid audio length"; return false; }
        completed += count;
        done = !effect->getProcessState();
        if (done && completed != output->frames)
        { message = "Effect ended before expected length"; return false; }
        return true;
    }
    const char* error() const override { return message; }
};

class ChainScreen
{
  public:
    enum Page { Chain, Sources, Effects, Parameters, Actions, Recipes, Destinations, Leave };
    enum Work { Idle, Loading, Snapshot, Reading, Processing, Restoring, Writing };
    cSampleEditor* editor = nullptr;
    FileSystem* fs = nullptr;
    EffectBackend backend;
    Runner runner{backend};
    Recipe recipe;
    Recipe lastUsed;
    bool haveLast = false;
    bool open = false;
    bool snapshotReady = false;
    bool touched = false;
    bool playing = false;
    bool originalPlaying = false;
    bool renderPending = false;
    bool saveRecipes = false;
    bool committed = false;
    Page page = Chain;
    Work work = Idle;
    unsigned selected = 0;
    unsigned selectedNode = 0;
    unsigned count = 0;
    unsigned entries[145] = {};
    char rows[145][76] = {};
    char* rowPointers[145] = {};
    strList list = {};
    bool listBound = false;
    hControl control = -1;
    hControl statusControl = -1;
    char statusText[120] = {};
    char title[96] = {};
    char notice[96] = {};
    char snapshotPath[80] = {};
    char outputPath[80] = {};
    char destinationPath[96] = {};
    FileSystem::FileHandle io = nullptr;
    uint32_t offset = 0;
    unsigned channel = 0;
    unsigned sourceSlot = 0;
    unsigned destinationSlot = 0;
    bool sourceStereo = false;
    uint32_t sourceFrames = 0;
    int16_t* sourceAddress[2] = {};
    int16_t* buffers[2][2] = {};
    mtEffect* savedEffect = nullptr;
    uint8_t savedEffectIdx = 0;
    uint16_t savedPlugin = 0;
    float savedParams[12] = {};
    strInstrument sourceInstrument = {};
    strInstrument outputInstrument = {};
    strInstrumentFile outputHeader = {};

    Node& node() { return selectedNode ? recipe.stages[selectedNode - 1] : recipe.source; }
    bool busy() const { return work != Idle; }
    void stopAudio()
    {
        mtPadBoard.cutAllInstrument();
        playing = false;
        originalPlaying = false;
        engine.getSdPlayer()->stop();
    }
    void closeIo()
    {
        if (io) fs->closeFile(io);
        io = nullptr;
    }
    bool openIo(const char* path, FileSystem::FileOpenMode mode)
    {
        closeIo();
        auto [ok, handle] = fs->openFile(path, mode);
        if (ok) io = handle;
        offset = 0;
        channel = 0;
        return ok;
    }
    void error(const char* text)
    {
        closeIo();
        runner.invalidate();
        renderPending = false;
        work = Idle;
        snprintf(notice, sizeof(notice), "%s", text);
        show(Chain);
    }
    void changed()
    {
        stopAudio();
        runner.invalidate();
        notice[0] = 0;
    }
    void addRow(const char* text, unsigned value = 0)
    {
        if (count >= 145) return;
        snprintf(rows[count], sizeof(rows[count]), "%s", text);
        entries[count] = value;
        rowPointers[count] = rows[count];
        ++count;
    }
    const char* name(const Node& n)
    {
        if (n.effect == kSample) return "Current Sample";
        if (n.effect == kPlugin) return n.filename;
        return n.effect < kBuiltInEffectsCount ? effectNamesLabels[n.effect] : "Unavailable";
    }
    void show(Page next);
    void input(unsigned button);
    void turn(int delta);
    void choose();
    void preview(bool render = false);
    void update();
    void discard();
    void finishRestore();
    bool pluginNode(unsigned index, Node& n);
    bool configureParameters();
    void recipeFile(unsigned slot, bool save);
    void render(unsigned slot);
    void finishRender();
};

ChainScreen screen;
void chainButton(uint8_t button) { screen.input(button - interfaceButton0); }
void chainNavigate(int delta)
{
    if (screen.busy() || !screen.count) return;
    screen.selected = std::clamp<int>(int(screen.selected) + delta, 0, screen.count - 1);
    screen.show(screen.page);
}
void chainUp() { chainNavigate(-1); }
void chainDown() { chainNavigate(1); }
void chainEncoder(int16_t delta) { screen.turn(delta); }
void chainSelect() { screen.choose(); }

bool ChainScreen::pluginNode(unsigned index, Node& n)
{
    const auto* entry = PtfxManager::getPlugin(index);
    if (!entry || !PtfxManager::loadPlugin(index, sampleEditorModular)) return false;
    const auto& h = PtfxManager::getActiveHeader();
    n = {};
    n.effect = kPlugin;
    n.flags = h.flags;
    n.count = h.num_params;
    snprintf(n.filename, sizeof(n.filename), "%s", entry->filename);
    char path[64];
    snprintf(path, sizeof(path), "/Effects/%s", n.filename);
    if (!fileFingerprint(fs, path, n.fingerprint)) return false;
    for (unsigned i = 0; i < n.count; ++i) n.params[i] = h.params[i].default_val;
    return true;
}

bool ChainScreen::configureParameters()
{
    Node& n = node();
    if (n.effect == kSample) return false;
    if (!backend.resolve(n))
    { snprintf(notice, sizeof(notice), "%s", backend.error()); return false; }
    if (n.effect == kPlugin) editor->loadModularEffectUI(PtfxManager::getActiveHeader());
    if (!n.count && (selectedNode || !(n.flags & PTFX_FLAG_GENERATOR)))
    { snprintf(notice, sizeof(notice), "This effect has no parameters"); return false; }
    return true;
}

void ChainScreen::show(Page next)
{
    const bool pageChanged = page != next;
    const unsigned previousCount = count;
    page = next;
    count = 0;
    char text[96];
    if (page == Chain)
    {
        snprintf(text, sizeof(text), "Source: %s", name(recipe.source)); addRow(text);
        for (unsigned i = 0; i < recipe.count; ++i)
        {
            snprintf(text, sizeof(text), "%02u  %s%s", i + 1,
                     recipe.stages[i].bypass ? "[Bypass] " : "", name(recipe.stages[i]));
            addRow(text);
        }
    }
    else if (page == Sources || page == Effects)
    {
        if (page == Sources && sourceFrames) addRow("Current Sample", kSample);
        else if (page == Effects) for (unsigned i = 0; i < kBuiltInEffectsCount; ++i)
            if (supported(i)) addRow(effectNamesLabels[i], i);
        for (unsigned i = 0; i < PtfxManager::getPluginCount(); ++i)
            if (bool(PtfxManager::getPlugin(i)->flags & PTFX_FLAG_GENERATOR) == (page == Sources))
                addRow(PtfxManager::getPluginDisplayName(i), 256 + i);
    }
    else if (page == Parameters)
    {
        const Node& n = node();
        const auto& desc = n.effect == kPlugin ? editor->s_modularEffectParams
                                             : effectDisplayConstantParams[n.effect];
        for (unsigned i = 0; i < n.count; ++i)
        {
            snprintf(text, sizeof(text), "%s: %.3f %s", desc.labelsText[i], n.params[i],
                     desc.afterValueText[i] ? desc.afterValueText[i] : "");
            if (n.effect == kPlugin && PtfxManager::getActiveHeader().params[i].display_type == PARAM_LIST)
            {
                // Plugin list strings live in the currently loaded executable region.
                const auto& h = PtfxManager::getActiveHeader();
                const auto& p = h.params[i];
                uint16_t stringOffset = uint8_t(p.unit[0]) | uint16_t(uint8_t(p.unit[1])) << 8;
                const char* value = reinterpret_cast<const char*>(PtfxLoader::getCodeBuffer() + stringOffset);
                unsigned item = unsigned(std::max(0.0f, n.params[i]));
                const unsigned items = uint8_t(p.unit[2]);
                if (item < items)
                {
                    for (unsigned j = 0; j < item; ++j) value += strlen(value) + 1;
                    snprintf(text, sizeof(text), "%s: %s", desc.labelsText[i], value);
                }
            }
            addRow(text);
        }
        if (!selectedNode && n.effect == kPlugin)
        { snprintf(text, sizeof(text), "Length: %.3f s", recipe.generatorFrames / 44100.0f); addRow(text); }
    }
    else if (page == Actions)
    {
        if (selectedNode)
        { addRow("Bypass / Enable"); addRow("Move Up"); addRow("Move Down"); addRow("Duplicate"); addRow("Remove"); }
        else addRow("Choose Source");
        addRow("Load Recipe", 10); addRow("Save Recipe", 11);
    }
    else if (page == Recipes)
    {
        if (!saveRecipes && haveLast) addRow("Last Used", 0);
        for (unsigned i = 1; i <= 16; ++i)
        { snprintf(text, sizeof(text), "Chain %02u", i); addRow(text, i); }
    }
    else if (page == Destinations)
    {
        for (unsigned i = 0; i < kSampleInstrumentsCount; ++i)
            if (!mtProject.instrument[i].isActive && (i != sourceSlot || !sourceFrames))
            { snprintf(text, sizeof(text), "Empty sample slot %02u", i + 1); addRow(text, i); }
        if (!count) addRow("No empty sample slots", 255);
    }
    else if (page == Leave) { addRow("Keep Editing"); addRow("Discard Chain"); }

    if (!count && page == Sources) addRow("Install .ptfx generators in /Effects", kSample);
    selected = count ? std::min(selected, count - 1) : 0;
    const char* state = runner.state() == Runner::State::Ready ? "Ready" : "Needs preview";
    if (busy()) state = "Preparing";
    snprintf(title, sizeof(title), "FX Chain %u/8", recipe.count);
    editor->showTitleBar(title);
    if (runner.state() == Runner::State::Ready)
        snprintf(statusText, sizeof(statusText), "Ready - %.3f s - %s", runner.result().frames / 44100.0f,
                 runner.result().channels == 2 ? "Stereo" : "Mono");
    else snprintf(statusText, sizeof(statusText), "%s%s%s", state, notice[0] ? " - " : "", notice);
    if (page == Parameters) snprintf(statusText, sizeof(statusText), "Up/Down: parameter   Encoder: value   Reset: default");
    display.setControlText(statusControl, statusText);
    display.setControlShow(statusControl);
    display.refreshControl(statusControl);
    list.data = rowPointers;
    list.length = count;
    list.linesCount = std::max<unsigned>(1, std::min<unsigned>(count, 12));
    // cList owns start as its animated cursor position, not a viewport offset.
    // Rebinding on each refresh restarts the animation from the first row.
    if (!listBound || pageChanged || count != previousCount)
    {
        list.start = selected;
        display.setControlData(control, &list);
        listBound = true;
    }
    display.setControlValue(control, selected);
    display.setControlShow(control);
    display.refreshControl(control);
    const char* labels[8] = { "Back", "Add", "Edit", "Actions", "Original", "Recipes", "Preview", "Render" };
    if (page != Chain)
    { labels[1] = ""; labels[2] = "Select"; labels[3] = ""; labels[4] = ""; labels[5] = ""; labels[6] = ""; labels[7] = ""; }
    if (page == Parameters) { labels[2] = ""; labels[3] = "Reset"; }
    if (playing) labels[6] = "Stop";
    if (busy()) { for (auto& l : labels) l = ""; labels[0] = work == Writing || work == Restoring ? "" : "Cancel"; }
    for (unsigned i = 0; i < 8; ++i)
    {
        display.setControlValue(editor->label[i], 0);
        display.setControlStyle(editor->label[i], controlStyleCenterX | controlStyleCenterY | controlStyleFont3);
        display.setControlColors(editor->label[i], display.getTheme().activeButtonLabelsColors);
        display.setControlPosition(editor->label[i], i * 100 + 50, 424);
        display.setControlSize(editor->label[i], 94, 55);
        display.setControlText(editor->label[i], labels[i]);
        display.setControlText2(editor->label[i], "");
        display.setControlData(editor->label[i], nullptr);
        display.setControlShow(editor->label[i]);
        display.refreshControl(editor->label[i]);
    }
    editor->ip->clearAll();
    for (unsigned i = 0; i < 8; ++i) editor->ip->setButtonObj(interfaceButton0 + i, buttonPress, chainButton);
    editor->ip->setButtonObj(interfaceButtonUp, buttonPress, chainUp);
    editor->ip->setButtonObj(interfaceButtonDown, buttonPress, chainDown);
    editor->ip->setButtonObj(interfaceButtonRight, buttonPress, chainSelect);
    editor->ip->setPotObj(interfacePot0, chainEncoder);
}

void ChainScreen::turn(int delta)
{
    if (busy() || !count || !delta) return;
    if (page == Parameters)
    {
        if (editor->buttons->isButtonPressed(interfaceButtonShift))
        { selected = std::clamp<int>(int(selected) + delta, 0, count - 1); }
        else
        {
            changed();
            Node& n = node();
            if (selected >= n.count)
                {
                    const unsigned step = (n.flags & PTFX_FLAG_WAVETABLE) ? 2048 : 441;
                    recipe.generatorFrames = std::clamp<int64_t>(int64_t(recipe.generatorFrames) + int64_t(delta) * step, step == 2048 ? 2048 : 1, kCapacity);
                }
            else
            {
                const auto& d = n.effect == kPlugin ? editor->s_modularEffectParams : effectDisplayConstantParams[n.effect];
                n.params[selected] = std::clamp(n.params[selected] + delta * d.changeStep[selected],
                                                d.downConstrain[selected], d.upConstrain[selected]);
            }
        }
    }
    else selected = std::clamp<int>(int(selected) + delta, 0, count - 1);
    show(page);
}

void ChainScreen::choose()
{
    if (busy()) return;
    if (page == Chain)
    {
        selectedNode = selected;
        if (selectedNode == 0 && recipe.source.effect == kSample) { selected = 0; show(Sources); }
        else if (configureParameters()) { selected = 0; show(Parameters); }
        else show(Chain);
    }
    else if (page == Sources || page == Effects)
    {
        Node n;
        unsigned id = entries[selected];
        if (id >= 256)
        {
            if (!pluginNode(id - 256, n)) { error("Cannot load plugin"); return; }
        }
        else
        {
            n.effect = id;
            if (id != kSample)
            {
                n.count = effectDisplayConstantParams[id].paramsNumber;
                memcpy(n.params, effectDefaultParamsTable[id].parameter, sizeof(n.params));
            }
        }
        if (page == Sources && n.effect == kSample && !sourceFrames)
        { error("Install a .ptfx generator in /Effects"); return; }
        changed();
        if (page == Sources)
        {
            recipe.source = n;
            if (n.flags & PTFX_FLAG_WAVETABLE) recipe.generatorFrames = 8 * 2048;
        }
        else if (!recipe.add(n)) { error("Maximum 8 effects"); return; }
        selected = page == Sources ? 0 : recipe.count;
        show(Chain);
    }
    else if (page == Actions)
    {
        const unsigned action = entries[selected];
        if (action >= 10)
        { saveRecipes = action == 11; selected = 0; show(Recipes); return; }
        if (!selectedNode) { selected = 0; show(Sources); return; }
        changed();
        unsigned i = selectedNode - 1;
        if (selected == 0) recipe.stages[i].bypass = !recipe.stages[i].bypass;
        if (selected == 1 && recipe.move(i, -1)) --selectedNode;
        if (selected == 2 && recipe.move(i, 1)) ++selectedNode;
        if (selected == 3 && !recipe.duplicate(i)) snprintf(notice, sizeof(notice), "Maximum 8 effects");
        if (selected == 4) { recipe.remove(i); selectedNode = std::min<unsigned>(selectedNode, recipe.count); }
        selected = selectedNode;
        show(Chain);
    }
    else if (page == Recipes) recipeFile(entries[selected], saveRecipes);
    else if (page == Destinations && entries[selected] != 255) render(entries[selected]);
    else if (page == Leave)
    { if (selected == 0) show(Chain); else discard(); }
}

void ChainScreen::input(unsigned button)
{
    if (busy())
    {
        if (button == 0 && work != Writing && work != Restoring)
        {
            if (work == Snapshot || work == Loading) { closeIo(); open = false; newFileManager.suspendAutosave(false); sequencer.setMidiInVoiceMode(Sequencer::midiInVoiceMode_SampleEditor); display.setControlHide(control); display.setControlHide(statusControl); editor->switchScreen(cSampleEditor::mainScreen); editor->restoreFunctions(); }
            else { closeIo(); runner.cancel(); work = Idle; renderPending = false; show(Chain); }
        }
        return;
    }
    if (button == 0)
    { stopAudio(); selected = 0; show(page == Chain ? Leave : Chain); return; }
    if (page == Parameters)
    {
        if (button == 3 && selected < node().count)
        {
            changed();
            node().params[selected] = node().effect == kPlugin ? PtfxManager::getActiveHeader().params[selected].default_val
                : effectDefaultParamsTable[node().effect].parameter[selected];
            show(Parameters);
        }
        return;
    }
    if (page != Chain) { if (button == 2) choose(); return; }
    if (button == 1) { selected = 0; show(Effects); }
    else if (button == 2) choose();
    else if (button == 3) { selectedNode = selected; selected = 0; show(Actions); }
    else if (button == 4 && recipe.source.effect == kSample && snapshotReady)
    {
        if (playing) { stopAudio(); show(Chain); return; }
        // Use the spare buffer for original audition without replacing the ready result.
        unsigned b = runner.state() == Runner::State::Ready && runner.result().channel[0] == buffers[0][0] ? 1 : 0;
        if (!openIo(snapshotPath, FileSystem::kRead)) { error("Cannot read original"); return; }
        originalPlaying = true;
        destinationSlot = b; // private buffer index during original audition
        work = Reading;
        show(Chain);
    }
    else if (button == 5) { saveRecipes = false; selected = 0; show(Recipes); }
    else if (button == 6) { if (playing) { stopAudio(); show(Chain); } else preview(); }
    else if (button == 7) preview(true);
}

void ChainScreen::preview(bool render)
{
    stopAudio();
    renderPending = render;
    if (runner.state() == Runner::State::Ready)
    {
        if (render) { selected = 0; show(Destinations); return; }
        const Audio& audio = runner.result();
        std::vector<int16_t*> addresses = { audio.channel[0], audio.channel[audio.channels == 2 ? 1 : 0] };
        instrumentPlayer[0].clearInterfacePlayingEndFlag();
        mtPadBoard.startNoteWithoutInstrument(60, addresses, audio.frames, DEFAULT_INSTRUMENT_VOLUME, INTERFACE_BUTTON_PREVIEW);
        playing = true;
        show(Chain);
        return;
    }
    if (sourceFrames && !snapshotReady) { error("Source snapshot unavailable; reopen Chain"); return; }
    touched = true;
    if (recipe.source.effect == kSample)
    {
        if (!snapshotReady || !sourceFrames) { error("Choose a generator or an existing sample"); return; }
        if (!openIo(snapshotPath, FileSystem::kRead)) { error("Cannot read source snapshot"); return; }
        work = Reading;
    }
    else
    {
        Audio source{ { buffers[0][0], buffers[0][1] }, 0, 1 };
        runner.start(recipe, source, buffers, kCapacity);
        work = Processing;
    }
    show(Chain);
}

void ChainScreen::recipeFile(unsigned slot, bool save)
{
    stopAudio();
    if (!slot && !save && haveLast) { changed(); recipe = lastUsed; selected = 0; show(Chain); return; }
    fs->mkdir(0, "/Effects"); fs->mkdir(0, kRecipeDir);
    char path[80], temp[84], backup[84];
    snprintf(path, sizeof(path), "%s/Chain%02u.ptchain", kRecipeDir, slot);
    snprintf(temp, sizeof(temp), "%s.tmp", path);
    snprintf(backup, sizeof(backup), "%s.bak", path);
    // Recover an interrupted replacement before attempting another read/write.
    if (!fs->exists(path) && fs->exists(backup)) fs->rename(backup, path);
    uint8_t bytes[kRecipeBytes];
    if (save)
    {
        if (!encode(recipe, bytes, sizeof(bytes)) || !openIo(temp, FileSystem::kWriteCreateNew))
        { error("Cannot save recipe"); return; }
        bool ok = fs->writeFile(bytes, sizeof(bytes), io) == int32_t(sizeof(bytes));
        closeIo();
        if (!ok) { fs->remove(temp); error("Recipe write failed"); return; }
        fs->remove(backup);
        bool previous = fs->exists(path);
        if ((previous && !fs->rename(path, backup)) || !fs->rename(temp, path))
        { if (previous && !fs->exists(path)) fs->rename(backup, path); fs->remove(temp); error("Recipe save failed"); return; }
        fs->remove(backup);
        snprintf(notice, sizeof(notice), "Saved Chain %02u", slot);
    }
    else
    {
        if (!openIo(path, FileSystem::kRead)) { error("Recipe not found"); return; }
        Recipe loaded;
        bool ok = fs->getFileSize(io) == sizeof(bytes) && fs->readFile(bytes, sizeof(bytes), io) == int32_t(sizeof(bytes)) && decode(bytes, sizeof(bytes), loaded);
        closeIo();
        if (!ok) { error("Invalid or unsupported recipe"); return; }
        changed(); recipe = loaded;
    }
    selected = 0;
    show(Chain);
}

void ChainScreen::render(unsigned slot)
{
    stopAudio();
    if (slot >= kSampleInstrumentsCount || mtProject.instrument[slot].isActive || runner.state() != Runner::State::Ready)
    { error("Choose an empty sample slot"); return; }
    destinationSlot = slot;
    outputInstrument = sourceInstrument;
    if (recipe.source.effect != kSample) newFileManager.setDefaultActiveInstrument(&outputInstrument);
    const auto& audio = runner.result();
    outputInstrument.isActive = 1;
    outputInstrument.sample.length = audio.frames;
    outputInstrument.sample.address = nullptr;
    snprintf(outputInstrument.sample.file_name, SAMPLE_NAME_SIZE, "Chain %02u", slot + 1);
    // Length-changing operations have no general loop/slice mapping yet.
    if (recipe.source.effect != kSample || audio.frames != sourceFrames)
    {
        outputInstrument.sliceNumber = 0;
        memset(outputInstrument.slices, 0, sizeof(outputInstrument.slices));
        outputInstrument.loopPoint1 = 1;
        outputInstrument.loopPoint2 = MAX_16BIT - 1;
        outputInstrument.playMode = (recipe.source.flags & PTFX_FLAG_WAVETABLE) ? playModeWavetable : playModeSingleShot;
    }
    if (outputInstrument.playMode == playModeWavetable &&
        (!outputInstrument.sample.wavetable_window_size || audio.frames % outputInstrument.sample.wavetable_window_size))
    { error("Wavetable needs complete windows"); return; }
    uint32_t ramFrames = isPlayModeStreamable(outputInstrument.playMode) ? std::min<uint32_t>(audio.frames, kStreamPreCacheFrames) : audio.frames;
    if (mtProject.used_memory + ramFrames * audio.channels * 2 > std::min<uint32_t>(mtProject.max_memory, EDIT_BUFF_OFFSET))
    { error("Not enough sample memory"); return; }
    newFileManager.writeInstrumentToFileStruct(&outputInstrument, &outputHeader);
    snprintf(destinationPath, sizeof(destinationPath), cWorkspaceInstrumentFileFormatNew, slot + 1);
    if (!openIo(outputPath, FileSystem::kWriteCreateNew)) { error("Cannot create rendered sample"); return; }
    if (fs->writeFile(&outputHeader, sizeof(outputHeader), io) != int32_t(sizeof(outputHeader)))
    { error("Sample header write failed"); fs->remove(outputPath); return; }
    work = Writing;
    show(Chain);
}

void ChainScreen::finishRender()
{
    closeIo();
    const auto& audio = runner.result();
    if (fs->getFileSize(outputPath) != sizeof(outputHeader) + size_t(audio.frames) * audio.channels * 2)
    { error("Rendered sample write incomplete"); return; }
    char backup[110]; snprintf(backup, sizeof(backup), "%s.chainbak", destinationPath);
    if (!fs->exists(destinationPath) && fs->exists(backup)) fs->rename(backup, destinationPath);
    fs->remove(backup);
    bool previous = fs->exists(destinationPath);
    if ((previous && !fs->rename(destinationPath, backup)) || !fs->rename(outputPath, destinationPath))
    { if (previous && !fs->exists(destinationPath)) fs->rename(backup, destinationPath); error("Cannot commit rendered sample"); return; }
    // Validate the backing file before publishing any instrument metadata.
    bool stereo = false;
    if (!newFileManager.isPtiFileValid(destinationPath, stereo, sizeof(strInstrumentFile), audio.frames) || stereo != (audio.channels == 2))
    {
        fs->remove(destinationPath);
        if (previous) fs->rename(backup, destinationPath);
        error("Rendered sample validation failed"); return;
    }
    const bool stream = isPlayModeStreamable(outputInstrument.playMode);
    const uint32_t ramFrames = stream ? std::min<uint32_t>(audio.frames, kStreamPreCacheFrames) : audio.frames;
    const uint32_t ramBytes = ramFrames * audio.channels * 2;
    if (mtProject.used_memory + ramBytes > std::min<uint32_t>(mtProject.max_memory, EDIT_BUFF_OFFSET))
    {
        fs->remove(destinationPath);
        if (previous) fs->rename(backup, destinationPath);
        error("Not enough sample memory"); return;
    }
    const strInstrument oldInstrument = mtProject.instrument[destinationSlot];
    const strSampleMemory oldMemory = mtProject.sampleMemory[destinationSlot];
    const unsigned first = newFileManager.calcFirstSlotToMoveInMemory(destinationSlot + 1);
    newFileManager.moveSampleMemory(first, ramBytes);
    engine.setBlockedInstrument(destinationSlot);
    mtProject.instrument[destinationSlot] = outputInstrument;
    newFileManager.setInstrumentSampleAddress(destinationSlot, audio.channels, ramFrames);
    mtProject.instrument[destinationSlot].sample.length = audio.frames;
    bool loaded = true;
    if (stream)
        loaded = voiceStreamManager.setupInstrumentStream(destinationSlot, destinationPath, sizeof(strInstrumentFile), audio.frames, audio.channels == 2);
    else
        for (unsigned c = 0; c < audio.channels; ++c)
            memcpy(mtProject.sampleMemory[destinationSlot].address[c], audio.channel[c], audio.frames * 2);
    if (!loaded)
    {
        voiceStreamManager.clearInstrumentMetadata(destinationSlot);
        mtProject.instrument[destinationSlot] = oldInstrument;
        mtProject.sampleMemory[destinationSlot] = oldMemory;
        newFileManager.moveSampleMemory(first, -int32_t(ramBytes));
        engine.clearBlockedInstrument(destinationSlot);
        fs->remove(destinationPath);
        if (previous) fs->rename(backup, destinationPath);
        error("Cannot load rendered sample"); return;
    }
    mtProject.used_memory += ramBytes;
    engine.clearBlockedInstrument(destinationSlot);
    fs->remove(backup);
    committed = true;
    newFileManager.setInstrumentStructChanged(destinationSlot);
    newFileManager.setProjectStructChanged();
    lastUsed = recipe;
    haveLast = true;
    // Restore current editor data before it is saved or another instrument is selected.
    work = Idle;
    renderPending = false;
    discard();
}

void ChainScreen::discard()
{
    stopAudio();
    if (touched && snapshotReady && sourceFrames)
    {
        if (!openIo(snapshotPath, FileSystem::kRead)) { error("Cannot restore source; keep SD inserted"); return; }
        work = Restoring;
        show(Chain);
    }
    else finishRestore();
}

void ChainScreen::finishRestore()
{
    closeIo();
    if (savedEffectIdx == editorEffectModular)
    {
        if (!PtfxManager::loadPlugin(savedPlugin, sampleEditorModular))
        { error("Cannot restore editor plugin"); return; }
        editor->loadModularEffectUI(PtfxManager::getActiveHeader());
    }
    editor->currentEffect = savedEffect;
    editor->currentEffectIdx = savedEffectIdx;
    memcpy(editor->effectDisplayParamsToAdjust[savedEffectIdx].parameter, savedParams, sizeof(savedParams));
    if (sourceFrames && sourceAddress[0])
    {
        if (!sourceStereo) sourceAddress[1] = sourceAddress[0];
        unsigned b = sourceAddress[0] == buffers[0][0] ? 1 : 0;
        int16_t* dst[2] = { buffers[b][0], buffers[b][1] };
        savedEffect->bindOffline(sourceAddress, dst, sourceFrames);
    }
    for (unsigned i = 0; i < editor->getCurrentEffectDisplayParams().paramsNumber; ++i)
    { float v = savedParams[i]; savedEffect->setParameter(v, i); }
    savedEffect->changeSelectionRange(editor->selection.startPoint, editor->selection.endPoint);
    fs->remove(snapshotPath);
    fs->remove(outputPath);
    work = Idle;
    open = false;
    newFileManager.suspendAutosave(false);
    sequencer.setMidiInVoiceMode(Sequencer::midiInVoiceMode_SampleEditor);
    display.setControlHide(control); display.setControlHide(statusControl);
    if (committed)
    {
        editor->afterReloadInstrumentIdx = destinationSlot;
        if (editor->confirmedDataIsChanged)
        {
            editor->reloadOnEndSaving = true;
            if (newFileManager.importSampleFromSampleEditor(sourceAddress[0], sourceAddress[sourceStereo ? 1 : 0], sourceFrames, sourceSlot))
                editor->ip->blockAllInputs();
            else { editor->reloadOnEndSaving = false; editor->restoreFunctions(); }
        }
        else
        {
            mtProject.values.lastUsedInstrument = destinationSlot;
            editor->start(0);
        }
        return;
    }
    editor->reloadSpectrumData();
    editor->switchScreen(cSampleEditor::mainScreen);
    editor->restoreFunctions();
}

void ChainScreen::update()
{
    if (open && playing && work == Idle && instrumentPlayer[0].getInterfacePlayingEndFlag())
    {
        instrumentPlayer[0].clearInterfacePlayingEndFlag();
        mtPadBoard.clearVoice(0);
        playing = false;
        originalPlaying = false;
        show(page);
    }
    if (!open || work == Idle) return;
    if (work == Loading)
    {
        savedEffect->update();
        if (savedEffect->getLoadingState()) return;
        if (!savedEffect->isDataLoaded()) { error("Cannot load source sample"); return; }
        for (unsigned c = 0; c < mtEffect::kChannels; ++c)
            sourceAddress[c] = savedEffect->getAddresToPolyphonicPlay(mtEffect::Channel(c));
        if (!openIo(snapshotPath, FileSystem::kWriteCreateNew)) { error("Cannot snapshot source"); return; }
        work = Snapshot;
    }
    if (work == Snapshot || work == Reading || work == Restoring || work == Writing)
    {
        const bool writing = work == Snapshot || work == Writing;
        uint32_t frames = work == Writing ? runner.result().frames : sourceFrames;
        unsigned channels = work == Writing ? runner.result().channels : (sourceStereo ? 2 : 1);
        unsigned size = std::min<uint32_t>(kIoFrames, frames - offset);
        int16_t* address = nullptr;
        if (work == Snapshot || work == Restoring) address = sourceAddress[channel];
        else if (work == Writing) address = runner.result().channel[channel];
        else address = buffers[originalPlaying ? destinationSlot : 0][channel];
        int32_t result = writing ? fs->writeFile(address + offset, size * 2, io)
                                 : fs->readFile(address + offset, size * 2, io);
        if (result != int32_t(size * 2))
        {
            // During restore keep the snapshot and the draft available for retry.
            error(writing ? "SD write failed" : "SD read failed; keep source snapshot"); return;
        }
        offset += size;
        if (offset == frames) { offset = 0; ++channel; }
        if (channel < channels) return;
        Work finished = work;
        closeIo();
        work = Idle;
        if (finished == Snapshot) { snapshotReady = true; show(Chain); }
        else if (finished == Restoring) finishRestore();
        else if (finished == Writing) finishRender();
        else if (originalPlaying)
        {
            std::vector<int16_t*> addresses = { buffers[destinationSlot][0], buffers[destinationSlot][sourceStereo ? 1 : 0] };
            instrumentPlayer[0].clearInterfacePlayingEndFlag();
            mtPadBoard.startNoteWithoutInstrument(60, addresses, sourceFrames, DEFAULT_INSTRUMENT_VOLUME, INTERFACE_BUTTON_PREVIEW);
            playing = true;
            show(Chain);
        }
        else
        {
            Audio source{ { buffers[0][0], buffers[0][1] }, sourceFrames, uint8_t(sourceStereo ? 2 : 1) };
            runner.start(recipe, source, buffers, kCapacity);
            work = Processing;
            show(Chain);
        }
    }
    else if (work == Processing)
    {
        unsigned before = runner.stage();
        runner.update();
        if (runner.state() == Runner::State::Failed) { error(runner.error()); return; }
        if (runner.state() == Runner::State::Ready)
        { work = Idle; preview(renderPending); }
        else if (runner.stage() != before)
        { snprintf(notice, sizeof(notice), "Stage %u/%u", runner.stage(), recipe.count); show(Chain); }
    }
}
}

bool sampleChainIsOpen() { return screen.open; }
void sampleChainUpdate() { screen.update(); }

void sampleChainOpen(cSampleEditor* editor)
{
    if (screen.open || mtProject.values.lastUsedInstrument >= kSampleInstrumentsCount ||
        newFileManager.getCurrentOperation() != fmNoOperation) return;
    // The working areas are carved from the sample bank. Refuse overlap with any
    // resident instrument rather than overwriting unrelated sample audio.
    if (mtProject.used_memory > EDIT_BUFF_OFFSET)
    { editor->showTitleBar("FX Chain: RAM full"); return; }
    screen.editor = editor;
    screen.fs = newFileManager.getFileSystem();
    screen.backend.fs = screen.fs;
    screen.stopAudio();
    newFileManager.suspendAutosave(true);
    sequencer.setMidiInVoiceMode(Sequencer::midiInVoiceMode_ignore);
    screen.sourceSlot = mtProject.values.lastUsedInstrument;
    screen.sourceInstrument = mtProject.instrument[screen.sourceSlot];
    screen.sourceInstrument.sample.length = editor->editorInstrument->sample.length;
    screen.sourceFrames = editor->editorInstrument->isActive ? editor->editorInstrument->sample.length : 0;
    screen.sourceStereo = screen.sourceFrames && editor->isCurrentSampleStereo();
    screen.savedEffect = editor->currentEffect;
    screen.savedEffectIdx = editor->currentEffectIdx;
    screen.savedPlugin = editor->currentModularPluginIdx;
    screen.sourceAddress[0] = screen.sourceAddress[1] = nullptr;
    memcpy(screen.savedParams, editor->effectDisplayParamsToAdjust[screen.savedEffectIdx].parameter, sizeof(screen.savedParams));
    for (unsigned b = 0; b < 2; ++b)
        for (unsigned c = 0; c < 2; ++c)
            screen.buffers[b][c] = externalRamEffectsBuff + (c < mtEffect::kChannels ? c : 0) * EDIT_BUFF_SIZE + b * kCapacity;
    screen.committed = false;
    screen.recipe = {};
    screen.runner.invalidate();
    screen.notice[0] = 0;
    screen.open = true;
    screen.touched = false;
    screen.snapshotReady = false;
    screen.renderPending = false;
    screen.selected = 0;
    screen.listBound = false;
    screen.work = ChainScreen::Idle;
    screen.fs->mkdir(1, kTempDir);
    // Only this controller owns this directory. Clear abandoned internal temp files.
    screen.fs->removeOnlyDirFiles(kTempDir);
    static unsigned session = 0;
    snprintf(screen.snapshotPath, sizeof(screen.snapshotPath), "%s/source-%u.raw", kTempDir, ++session);
    snprintf(screen.outputPath, sizeof(screen.outputPath), "%s/output-%u.pti", kTempDir, session);
    if (screen.sourceFrames)
    {
        if (screen.savedEffect->isDataLoaded())
        {
            for (unsigned c = 0; c < mtEffect::kChannels; ++c)
                screen.sourceAddress[c] = screen.savedEffect->getAddresToPolyphonicPlay(mtEffect::Channel(c));
            if (screen.openIo(screen.snapshotPath, FileSystem::kWriteCreateNew)) screen.work = ChainScreen::Snapshot;
            else snprintf(screen.notice, sizeof(screen.notice), "Cannot snapshot source");
        }
        else if (screen.savedEffect->startLoad(screen.sourceSlot) == mtEffect::loadingSuccessful)
            screen.work = ChainScreen::Loading;
        else snprintf(screen.notice, sizeof(screen.notice), "Cannot load source sample");
    }
    for (unsigned i = 0; i < 8; ++i) display.setControlHide(editor->bar[i]);
    for (unsigned i = 0; i < cSampleEditor::kEffectPageParams; ++i) display.setControlHide(editor->paramList[i]);
    editor->hideSpectrum(); editor->hideSpectrumPoints(); editor->hidePlayhead(); editor->hideEffectList();
    display.setControlHide(editor->frame);
    display.setControlHide(editor->blankSpaceCover);
    if (screen.control < 0)
    {
        strControlProperties properties;
        properties.x = 1; properties.y = 31; properties.w = 798; properties.h = 353;
        properties.style = controlStyleBackground;
        properties.data = &screen.list;
        screen.control = display.createControl<cList>(&properties);
        properties.y = 389; properties.h = 32;
        properties.style = controlStyleBackground | controlStyleFont2;
        properties.colors = display.getTheme().activeLabelsColors;
        properties.data = nullptr;
        screen.statusControl = display.createControl<cLabel>(&properties);
    }
    screen.show(screen.sourceFrames ? ChainScreen::Chain : ChainScreen::Sources);
}
