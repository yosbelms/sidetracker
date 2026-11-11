#include "SampleEditorChain.h"
#include "sampleEditor.h"
#include "SampleFxPreset.h"
#include "SampleFxRealtime.h"
#include "interruptsControl.h"
#include "ptfx_manager.h"
#include "fileManager.h"
#include "fileManagerDefs.h"
#include "mtAudioEngine.h"
#include "mtPadBoard.h"
#include "VoiceStreamManager.h"
#include "stpalette/StPalette.h"
#include "stpalette/Profiling.h"
#include "Tracker.h"
#include "Platform.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#ifdef TRACKER_DESKTOP
#include <chrono>
#endif

extern uint8_t externalRamScratchBuff[];

namespace
{
using namespace samplefx;
constexpr uint32_t kCapacity = EDIT_BUFF_SIZE / 2;
constexpr unsigned kIoFrames = 4096;
constexpr unsigned kBackButton = 2;
constexpr unsigned kApplyButton = 3;
constexpr unsigned kActionsButton = 4;
constexpr unsigned kMixButton = 5;
constexpr unsigned kValueButton = 7;
constexpr char kTempDir[] = "Workspace/FxChainTemp";
constexpr char kPresetDir[] = "/Effects/Presets";
constexpr char kPresetExtension[] = ".ptpreset";

bool supported(uint8_t effect)
{
    if(stpalette::contains(effect)) return true;
    return effect < kBuiltInEffectsCount && effect != editorEffectCut &&
           effect != editorEffectReverse && effect != editorEffectWavetableSmoother &&
           effect != editorEffectTimestretchBeat && effect != editorEffectTimestretchNote;
}

// These sample-editing utilities are offered in Tools.
bool listedInChain(uint8_t effect)
{
    return supported(effect) && effect != editorEffectNormalizer &&
           effect != editorEffectFadeIn && effect != editorEffectFadeOut;
}

const strEffectDisplayConstantParams& nativeSchema()
{
    static const char* const names[]={"Type","Amount","Macro","Drift","Sync","Tail"};
    static float high[]={5,100,100,100,14,10}, low[]={0,0,0,0,0,0};
    static float steps[]={1,1,1,1,1,.1f}, mult[]={1,1,1,1,1,1};
    static const char* const units[]={"","%","%","%",""," s"};
    static const char types[]={'l','d','d','d','l','f'};
    static const strEffectDisplayConstantParams data={6,names,high,low,steps,mult,units,types};
    return data;
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
    unsigned stageIndex = 1;
    uint32_t dryDelay = 0;
    stpalette::Processor native;
    RealtimeArena nativeArena;
    Audio nativeInput;
    bool nativeActive = false;
    void setStageIndex(unsigned i) override { stageIndex=i; }
    const char* message = "Effect failed";

    bool resolve(const Node& node, bool execute = true)
    {
        message = "Effect unavailable";
        if(stpalette::contains(node.effect)) { effect=nullptr; return stpalette::valid(node); }
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
        if (execute ? !PtfxManager::loadPlugin(index, sampleEditorModular)
                    : !PtfxManager::loadPluginMetadata(index)) return false;
        const auto& header = PtfxManager::getActiveHeader();
        if (header.num_params != node.count || header.flags != node.flags) return false;
        effect = &sampleEditorModular;
        return true;
    }

    bool begin(const Node& node, const Audio& input, Audio& out, uint32_t capacity,
               uint32_t generatorFrames) override
    {
        if (!resolve(node)) return false;
        nativeActive=stpalette::contains(node.effect);
        if(nativeActive)
        {
            nativeArena.reset(externalRamScratchBuff,SHARED_SCRATCH_MEMORY_SIZE);
            nativeInput=input; completed=0; output=&out;
            if(!native.prepare(node,input.channels,input.frames,
                {&nativeArena,[](void* a,size_t n){return static_cast<RealtimeArena*>(a)->allocate(n);}},stageIndex,mtProject.values.globalTempo))
            { message="Native effect memory limit"; return false; }
            uint64_t end=uint64_t(input.frames)+native.tail();
            if(end>capacity){message="Effect output too long";return false;}
            out.frames=end; out.channels=input.channels;
            dryDelay += stpalette::latencyFrames(node);
            return true;
        }
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
        if (stageIndex && node.effect == kPlugin) dryDelay += sampleEditorModular.getPrependSamples();
        out.channels = (node.flags & PTFX_FLAG_CONVERT_TO_MONO) ? 1 : channels;
        if (!out.frames || out.frames > capacity)
        { message = "Effect output too long"; return false; }
        output = &out;
        completed = 0;
        return true;
    }

    bool step(bool& done) override
    {
        if(nativeActive)
        {
            unsigned count=std::min<uint32_t>(128,output->frames-completed);
            int16_t* in[2]={nullptr,nullptr};
            int16_t* out[2]={output->channel[0]+completed,output->channels==2?output->channel[1]+completed:nullptr};
            if(completed<nativeInput.frames) for(unsigned c=0;c<nativeInput.channels;++c) in[c]=nativeInput.channel[c]+completed;
            if(!native.process(in,out,count)){message="Native effect invalid audio";return false;}
            completed+=count; done=completed==output->frames;
            return true;
        }
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

class LivePluginLoader : public RealtimePluginLoader
{
  public:
    FileSystem* fs = nullptr;
    const PtfxHostApi* hostApi() override { return PtfxLoader::getHostApi(); }
    bool load(const Node& node, RealtimePlugin& plugin, RealtimeArena& arena) override
    {
        char path[64];
        snprintf(path, sizeof(path), "/Effects/%s", node.filename);
        uint32_t hash;
        if (!fileFingerprint(fs, path, hash) || hash != node.fingerprint ||
            !PtfxLoader::readHeader(path, plugin.header)) return false;
        const uint32_t codeBytes = plugin.header.code_size + plugin.header.bss_size;
        auto* code = static_cast<uint8_t*>(arena.allocate(codeBytes));
        plugin.scratch = arena.allocate(plugin.header.scratch_size);
        if (!code || !plugin.scratch) return false;
        return PtfxLoader::loadPluginInto(path, code, codeBytes, plugin.header,
                                         plugin.init, plugin.parameter, plugin.process);
    }
};

class ChainScreen
{
  public:
    enum Page { Chain, Actions, SavedPresets, Destinations };
    enum Work { Idle, Loading, Snapshot, Reading, Processing, CapturingDry, Mixing, Restoring, Writing, Applying };
    enum Output { Audition, Apply, NewSlot };
    cSampleEditor* editor = nullptr;
    FileSystem* fs = nullptr;
    EffectBackend backend;
    Runner runner{backend};
    RealtimeChain* live = nullptr;
    LivePluginLoader liveLoader;
    bool livePlaying = false, liveRequested = false, resumeLive = false;
    bool waitingFade = false;
    float liveTempo=120;
    unsigned chainChannels() const {
        return definition.source.effect==kPlugin && (definition.source.flags&PTFX_FLAG_STEREO) ? mtEffect::kChannels : sourceStereo?2:1;
    }
    ChainDefinition definition;
    ChainDefinition lastUsed;
    char presetName[kMaxPresetNameBytes + 1] = {};
    char lastUsedPresetName[kMaxPresetNameBytes + 1] = {};
    bool haveLast = false;
    bool open = false;
    bool visible = false;
    bool haveDraft = false;
    bool autoPreview = false;
    unsigned draftSlot = 0;
    std::function<void()> pendingEditorAction;
    bool snapshotReady = false;
    bool touched = false;
    bool playing = false;
    bool originalPlaying = false;
    Output output = Audition;
    bool mixedReady = false, playAfterMix = false;
    unsigned mixedPercent = 100, toolbarMixPercent = ~0u;
    Audio mixed;
    ChainMix preparedMix;
    uint32_t generatorDryFrames = 0;
    uint8_t generatorDryChannels = 1;
    char mixText[12] = {};
    bool savingPreset = false;
    static constexpr unsigned kSavedPresetLimit = 32;
    static constexpr unsigned kPresetFilenameBytes = kMaxPresetNameBytes + sizeof(kPresetExtension);
    unsigned savedPresetCount = 0;
    bool committed = false;
    Page page = Chain;
    unsigned focus = 0;
    unsigned stageSelection = 0;
    unsigned parameterSelection = 0;
    char nativeName[64] = {};
    unsigned visibleParameterCount() const
    {
        const Node& n=selectedNode?definition.stages[selectedNode-1]:definition.source;
        if(!stpalette::contains(n.effect)) return n.count+(!selectedNode && n.effect==kPlugin?1:0);
        unsigned count=0; for(unsigned p=0;p<n.count;++p) count+=stpalette::visible(n,p);
        return count;
    }
    unsigned parameterIndex() const
    {
        const Node& n=selectedNode?definition.stages[selectedNode-1]:definition.source;
        if(!stpalette::contains(n.effect)) return parameterSelection;
        unsigned row=0;
        for(unsigned p=0;p<n.count;++p) if(stpalette::visible(n,p)) {if(row++==parameterSelection)return p;}
        return 0;
    }
    const strEffectDisplayConstantParams& schema()
    { return stpalette::contains(node().effect)?nativeSchema():node().effect==kPlugin?editor->s_modularEffectParams:effectDisplayConstantParams[node().effect<kBuiltInEffectsCount?node().effect:0]; }
    bool replacingEffect = false;
    unsigned browserSelection = 0;
    static constexpr unsigned kMenuRows = kSampleInstrumentsCount + 16;
    unsigned menuCount = 0, boundMenuCount = 0;
    unsigned menuEntries[kMenuRows] = {};
    char menuRows[kMenuRows][kPresetFilenameBytes] = {};
    char* menuPointers[kMenuRows] = {};
    strList menuList = {};
    hControl menuControl = -1;
    bool menuBound = false;
    bool toolbarReady = false;
    bool toolbarHasValue = false;
    char toolbarPrimary[8][24] = {};
    char toolbarSecondary[52] = {};
    char shownTitle[96] = {};
    bool schemaReady = false;
    bool haveParameters = false;
    Node schemaNode;
    unsigned boundStageCount = 255;
    unsigned boundParameterCount = 255;
    unsigned boundValueParameter = 255;
    strList stagesList = {}, parametersList = {}, valuesList = {};
    char stageRows[9][52] = {};
    char* stagePointers[9] = {};
    char* parameterPointers[13] = {};
    char lengthLabel[16] = "Length";
    char* valuePointers[256] = {};
    hControl stagesControl = -1, parametersControl = -1, valuesControl = -1;
    hControl headers[4] = {-1, -1, -1, -1};
    hControl valueBar = -1, focusControl = -1;
    strFrameData columnsFrame = {};
    char valueText[52] = {};
    const char* valueName = "Value";
    static constexpr uint16_t columnPlaces[4][4] = {
        {1, 31, 297, 392}, {301, 31, 197, 392},
        {501, 31, 197, 392}, {701, 31, 97, 392}
    };
    Work work = Idle;
    unsigned selected = 0;
    unsigned selectedNode = 0;
    unsigned count = 0;
    unsigned entries[145] = {};
    char rows[145][52] = {};
    char* rowPointers[145] = {};
    strList list = {};
    bool listBound = false;
    hControl control = -1;
    char title[96] = {};
    char notice[96] = {};
    char snapshotPath[80] = {};
    char generatorDryPath[80] = {};
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

    Node& node() { return selectedNode ? definition.stages[selectedNode - 1] : definition.source; }
    bool busy() const { return work != Idle; }
    bool mixHeld() const { return page == Chain && editor->buttons->isButtonPressed(interfaceButton0 + kMixButton); }
    void stopAudio()
    {
        engine.getSampleFxPreview()->stop();
        if (live) { live->~RealtimeChain(); live = nullptr; }
        livePlaying = false;
        resumeLive = false;
        waitingFade = false;
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
        mixedReady = false;
        output = Audition;
        work = Idle;
        snprintf(notice, sizeof(notice), "%s", text);
        show(Chain);
    }
    void changed(bool parameterOnly = false)
    {
        const bool resume = !parameterOnly && livePlaying;
        if(resume) { engine.getSampleFxPreview()->requestFadeOut(); waitingFade=true; }
        else if (!parameterOnly || !livePlaying) stopAudio();
        resumeLive |= resume;
        if(!parameterOnly) definition.qualityResolved=false;
        runner.invalidate();
        mixedReady = false;
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
    void addMenuRow(const char* text, unsigned value = 0)
    {
        if (menuCount >= kMenuRows) return;
        snprintf(menuRows[menuCount], sizeof(menuRows[menuCount]), "%s", text);
        menuEntries[menuCount] = value;
        menuPointers[menuCount] = menuRows[menuCount];
        ++menuCount;
    }
    const char* name(const Node& n)
    {
        if(stpalette::contains(n.effect)) {
            snprintf(nativeName,sizeof(nativeName),"St %s: %s",stpalette::category(n.effect),stpalette::typeName(n.effect,unsigned(n.params[stpalette::Type])));
            return nativeName;
        }
        if (n.effect == kSample) return "Current Sample";
        if (n.effect == kPlugin)
        {
            int index = findPlugin(n);
            return index >= 0 ? PtfxManager::getPluginDisplayName(index) : n.filename;
        }
        return n.effect < kBuiltInEffectsCount ? effectNamesLabels[n.effect] : "Unavailable";
    }
    void show(Page next);
    void navigate(int delta);
    void shiftColumn(int delta);
    void editValue(int delta);
    void editMix(int delta);
    void hideColumns();
    bool ensureSchema();
    void drawColumns(bool pageChanged, unsigned previousCount);
    void input(unsigned button);
    void turn(int delta);
    void choose();
    void addSelectedEffect();
    void browseSource();
    void preview(Output action = Audition);
    void startRealtime();
    void playPrepared();
    void startPrepared(const Audio&);
    void prepareOutput(bool play);
    void finishOutput(bool play);
    void updateMix();
    const Audio& result() const { return mixedReady ? mixed : runner.result(); }
    void publishParameters()
    {
        if (!livePlaying || waitingFade) return;
        DISABLE_AUDIO_INTERRUPTS();
        live->setParameters(definition);
        ENABLE_AUDIO_INTERRUPTS();
    }
    void update();
    void discard();
    void leave();
    void removeOrReset();
    void finishRestore();
    bool pluginNode(unsigned index, Node& n);
    void presetFile(unsigned slot, bool save);
    void scanSavedPresets();
    void render(unsigned slot);
    void finishRender();
    void measureCpu();
};

ChainScreen screen;
void chainButton(uint8_t button) { screen.input(button - interfaceButton0); }
void chainNavigate(int delta) { screen.navigate(delta); }
void chainUp() { chainNavigate(-1); }
void chainDown() { chainNavigate(1); }
void chainLeft() { screen.shiftColumn(-1); }
void chainRight() { screen.shiftColumn(1); }
void chainEncoder(int16_t delta) { screen.turn(delta); }
void chainSelect() { screen.choose(); }
void chainDelete() { screen.removeOrReset(); }

bool ChainScreen::pluginNode(unsigned index, Node& n)
{
    // Even a failed image read may have replaced shared UI strings. Reload
    // the selected stage's metadata before drawing it again.
    schemaReady = false;
    const auto* entry = PtfxManager::getPlugin(index);
    if (!entry || !PtfxManager::loadPluginMetadata(index)) return false;
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

void ChainScreen::scanSavedPresets()
{
    menuCount = 0;
    if (!savingPreset && haveLast) addMenuRow("Last Used", 0);
    const unsigned first = menuCount;
    static_assert(kMenuRows > kSavedPresetLimit);
    constexpr size_t suffixLength = sizeof(kPresetExtension) - 1;
    int found = kSavedPresetLimit;
    fs->listDirContents(kPresetDir, FileSystem::kFiles, [](const char* name) {
        const size_t length = strlen(name);
        return length > suffixLength && length - suffixLength <= kMaxPresetNameBytes &&
            !strchr(name, '/') && !strchr(name, '\\') &&
            !strcasecmp(name + length - suffixLength, kPresetExtension);
    }, menuRows[first], (kMenuRows - first) * kPresetFilenameBytes,
       kPresetFilenameBytes, found, menuPointers + first);
    menuCount = first + std::clamp(found, 0, int(kSavedPresetLimit));
    // Firmware sorts the pointers, so recover buffer order before expanding in place.
    std::sort(menuPointers + first, menuPointers + menuCount, std::less<const char*>{});
    for (unsigned i = menuCount; i-- > first;)
    {
        const size_t length = strlen(menuPointers[i]);
        memmove(menuRows[i], menuPointers[i], length + 1);
        menuRows[i][length - suffixLength] = 0;
        menuPointers[i] = menuRows[i];
    }
    std::sort(menuPointers + first, menuPointers + menuCount,
              [](const char* a, const char* b) { return strcmp(a, b) < 0; });
    for (unsigned i = first; i < menuCount; ++i) menuEntries[i] = i + 1;
    // Keep numbered destinations for new unnamed presets, while existing named
    // files are replaced directly under their own filenames.
    for (unsigned slot = 1; savingPreset && menuCount < kSavedPresetLimit && slot <= 16; ++slot)
    {
        char name[16];
        snprintf(name, sizeof(name), "Preset%02u", slot);
        bool exists = false;
        for (unsigned i = 0; i < menuCount; ++i) exists |= !strcasecmp(menuPointers[i], name);
        if (exists) continue;
        snprintf(menuRows[menuCount], sizeof(menuRows[menuCount]), "%s%s", name, kPresetExtension);
        menuRows[menuCount][strlen(name)] = 0;
        menuPointers[menuCount] = menuRows[menuCount];
        menuEntries[menuCount] = menuCount + 1;
        ++menuCount;
    }
    if (!menuCount) addMenuRow("No saved presets", 255);
    if (savingPreset && presetName[0])
        for (unsigned i = 0; i < menuCount; ++i)
            if (!strcmp(menuPointers[i], presetName)) selected = i;
    savedPresetCount = menuCount;
}

void ChainScreen::show(Page next)
{
    const bool pageChanged = page != next;
    const unsigned previousCount = count;
    page = next;
    count = 0;
    char text[96];
    stageSelection = std::min<unsigned>(stageSelection, definition.count);
    if (definition.source.effect == kSample && definition.count && !stageSelection) stageSelection = 1;
    selectedNode = stageSelection;
    // Keep the left-hand catalog independent of the selected chain stage,
    // just as the Sample Loader keeps its file browser independent of slots.
    for (unsigned i = 0; i < kBuiltInEffectsCount; ++i)
        if (listedInChain(i)) addRow(effectNamesLabels[i], i);
    for(unsigned id=stpalette::kFirst;id<=stpalette::kLast;++id) {
        snprintf(text,sizeof(text),"St %s",stpalette::category(id));
        addRow(text,id);
    }
    for (unsigned i = 0; i < PtfxManager::getPluginCount(); ++i)
    {
        const auto* plugin = PtfxManager::getPlugin(i);
        snprintf(text, sizeof(text), "%s%s", (plugin->flags & PTFX_FLAG_GENERATOR) ? "[Gen] " : "",
                 PtfxManager::getPluginDisplayName(i));
        addRow(text, 256 + i);
    }
    browserSelection = count ? std::min(browserSelection, count - 1) : 0;

    menuCount = 0;
    if (page == Actions)
    {
        if (selectedNode)
        {
            addMenuRow("Bypass / Enable"); addMenuRow("Move Up"); addMenuRow("Move Down");
            addMenuRow("Duplicate"); addMenuRow("Remove"); addMenuRow("Replace Effect", 20);
        }
        else addMenuRow("Choose Generator");
        addMenuRow("Load Preset", 10); addMenuRow("Save Preset", 11);
        if(stpalette::contains(node().effect)) addMenuRow("Measure CPU",12);
        addMenuRow("Render to new slot", 13);
    }
    else if (page == SavedPresets)
    {
        if (pageChanged) scanSavedPresets();
        else menuCount = savedPresetCount;
    }
    else if (page == Destinations)
    {
        for (unsigned i = 0; i < kSampleInstrumentsCount; ++i)
            if (!mtProject.instrument[i].isActive && (i != sourceSlot || !sourceFrames))
            { snprintf(text, sizeof(text), "Empty sample slot %02u", i + 1); addMenuRow(text, i); }
        if (!menuCount) addMenuRow("No empty sample slots", 255);
    }
    selected = menuCount ? std::min(selected, menuCount - 1) : 0;
    if (notice[0]) snprintf(title, sizeof(title), "%s", notice);
    else {
        bool economy=false;
        for(unsigned i=0;i<definition.count;++i) economy |= !definition.stages[i].bypass && stpalette::contains(definition.stages[i].effect) && definition.stages[i].quality;
        snprintf(title, sizeof(title), "FX Chain %u/8%s%s%s%s", definition.count,
                  presetName[0] ? " - " : "", presetName,
                  busy() ? " - Preparing" : livePlaying ? " - Live" : "", economy ? " - Economy" : "");
    }
    if (!visible)
    {
        editor->showTitleBar(notice[0] ? notice : playing ? "Sample Editor - Preview" : "Sample Editor");
        display.setControlText(editor->label[6], playing ? "Stop" : "Preview");
        display.refreshControl(editor->label[6]);
        return;
    }
    if (!toolbarReady || strcmp(shownTitle, title))
    {
        editor->showTitleBar(title);
        snprintf(shownTitle, sizeof(shownTitle), "%s", title);
    }
    // Source I/O does not change the selected parameter. Offline DSP and
    // restore may replace shared plugin code, so their lists must detach.
    if (!busy() || !toolbarReady || work == Processing || work == Restoring)
        drawColumns(pageChanged, previousCount);
    snprintf(mixText, sizeof(mixText), "%u%%", definition.mix);
    const char* labels[8] = { "Preview", "Add", "Back", "Apply", "Actions", "Mix", "", valueName };
    if (page != Chain)
    { labels[0] = "Back"; labels[1] = "Select"; for (unsigned i = 2; i < 8; ++i) labels[i] = ""; }
    else
    {
        unsigned id = count ? entries[browserSelection] : kSample;
        bool source = id == kSample || (id >= 256 && (PtfxManager::getPlugin(id - 256)->flags & PTFX_FLAG_GENERATOR));
        labels[1] = replacingEffect ? "Replace" : source ? "Use src" : "Add";
        if (playing) labels[0] = "Stop";
    }
    const bool hasValue = page == Chain && valueName[0];
    for (unsigned i = 0; i < 8; ++i)
    {
        const bool changed = !toolbarReady || strcmp(toolbarPrimary[i], labels[i]) ||
            (i == kMixButton && toolbarMixPercent != definition.mix) ||
            (i == kValueButton && (toolbarHasValue != hasValue || strcmp(toolbarSecondary, hasValue ? valueText : "")));
        if (!changed) continue;
        if (!toolbarReady)
        {
            display.setControlStyle(editor->label[i], controlStyleCenterX | controlStyleCenterY | controlStyleFont3);
            display.setControlStyle2(editor->label[i], controlStyleCenterX | controlStyleCenterY | controlStyleFont2);
            display.setControlColors(editor->label[i], i == kValueButton || i == kMixButton ? display.getTheme().activeLabelsColors
                                                              : display.getTheme().activeButtonLabelsColors);
            display.setControlPosition(editor->label[i], i * 100 + 50, 424);
            display.setControlSize(editor->label[i], 94, 55);
            display.setControlData(editor->label[i], nullptr);
            display.setControlShow(editor->label[i]);
        }
        const char* detail = i == kValueButton && hasValue ? valueText :
            page == Chain && i == kMixButton ? mixText : "";
        display.setControlValue(editor->label[i], detail[0] ? 1 : 0);
        display.setControlText(editor->label[i], labels[i]);
        display.setControlText2(editor->label[i], detail);
        display.refreshControl(editor->label[i]);
        snprintf(toolbarPrimary[i], sizeof(toolbarPrimary[i]), "%s", labels[i]);
    }
    // Exclude the cached value footer immediately when entering a menu, even
    // if its old label is still waiting for a display-list refresh.
    if (page == Chain) display.setControlShow(editor->label[kValueButton]);
    else display.setControlHide(editor->label[kValueButton]);
    if (!toolbarReady)
    {
        display.setControlValue(editor->bgLabel, 255);
        display.setControlShow(editor->bgLabel);
        display.refreshControl(editor->bgLabel);
    }
    snprintf(toolbarSecondary, sizeof(toolbarSecondary), "%s", hasValue ? valueText : "");
    toolbarHasValue = hasValue;
    toolbarMixPercent = definition.mix;
    toolbarReady = true;
    editor->ip->clearAll();
    editor->setViewSwitchFunctions();
    for (unsigned i = 0; i < 8; ++i) editor->ip->setButtonObj(interfaceButton0 + i, buttonPress, chainButton);
    editor->ip->setButtonObj(interfaceButtonUp, buttonPress, chainUp);
    editor->ip->setButtonObj(interfaceButtonDown, buttonPress, chainDown);
    editor->ip->setButtonObj(interfaceButtonLeft, buttonPress, chainLeft);
    editor->ip->setButtonObj(interfaceButtonRight, buttonPress, chainRight);
    editor->ip->setPotObj(interfacePot0, chainEncoder);
    editor->ip->setButtonObj(interfaceButtonInsert, buttonPress, chainSelect);
    editor->ip->setButtonObj(interfaceButtonDelete, buttonPress, chainDelete);
    // Publish the cursor, value bar, labels and title together instead of
    // swapping the display after each individual control update.
    display.synchronizeRefresh();
}

bool ChainScreen::ensureSchema()
{
    if (busy()) return false;
    const Node& n = node();
    if (schemaReady && schemaNode.effect == n.effect && schemaNode.fingerprint == n.fingerprint &&
        !strcmp(schemaNode.filename, n.filename)) return haveParameters;
    schemaNode = n;
    schemaReady = true;
    haveParameters = false;
    boundParameterCount = 255;
    boundValueParameter = 255;
    if (n.effect == kSample || busy()) return false;
    if (!backend.resolve(n, false)) return false;
    if (n.effect == kPlugin) editor->loadModularEffectUI(PtfxManager::getActiveHeader());
    haveParameters = n.count || (!selectedNode && (n.flags & PTFX_FLAG_GENERATOR));
    return haveParameters;
}

void ChainScreen::hideColumns()
{
    for (auto h : {control, stagesControl, menuControl, parametersControl, valuesControl,
                   valueBar, focusControl}) display.setControlHide(h);
    for (auto h : headers) display.setControlHide(h);
}

void ChainScreen::drawColumns(bool pageChanged, unsigned previousCount)
{
    const unsigned activeColumn = page == Chain ? focus : 1;
    // Snap changed selections before publishing the batch. Animated high-
    // priority lists otherwise delay the matching parameter/value controls.
    const bool implicitSample = definition.source.effect == kSample;
    unsigned stageCount = 0;
    for (unsigned i = 0; i <= definition.count; ++i)
    {
        if (!i && implicitSample) continue;
        const Node& n = i ? definition.stages[i - 1] : definition.source;
        if (!i) snprintf(stageRows[stageCount], sizeof(stageRows[stageCount]), "Gen: %s", name(n));
        else snprintf(stageRows[stageCount], sizeof(stageRows[stageCount]), "%s%s",
            n.bypass ? "- " : "", name(n));
        stagePointers[stageCount] = stageRows[stageCount];
        ++stageCount;
    }
    const unsigned stageCursor = stageSelection - (implicitSample && definition.count ? 1 : 0);
    stagesList.data = stagePointers;
    stagesList.length = stageCount;
    stagesList.linesCount = stageCount;
    stagesList.lineWidthMax = activeColumn == 1 ? 178 : 0;
    stagesList.lineTextLenMax = 40;
    if (boundStageCount != stagesList.length || stagesList.start != int(stageCursor))
    {
        stagesList.start = stageCursor;
        display.setControlData(stagesControl, &stagesList);
        boundStageCount = stagesList.length;
    }
    display.setControlValue(stagesControl, stageCursor);
    if (page == Chain) display.setControlShow(stagesControl);
    else display.setControlHide(stagesControl);
    display.refreshControl(stagesControl);

    list.data = rowPointers;
    list.length = count;
    list.linesCount = std::max<unsigned>(1, std::min<unsigned>(count, 12));
    list.lineWidthMax = activeColumn == 0 ? 278 : 0;
    list.lineTextLenMax = 64;
    if (!listBound || count != previousCount || list.start != int(browserSelection))
    {
        list.start = browserSelection;
        display.setControlData(control, &list);
        listBound = true;
    }
    display.setControlValue(control, browserSelection);
    display.setControlShow(control);
    display.refreshControl(control);

    if (page != Chain)
    {
        menuList.data = menuPointers;
        menuList.length = menuCount;
        menuList.linesCount = std::max(1u, std::min(menuCount, 12u));
        menuList.lineWidthMax = 178; menuList.lineTextLenMax = 48;
        if (!menuBound || pageChanged || boundMenuCount != menuCount || menuList.start != int(selected))
        {
            menuList.start = selected;
            display.setControlData(menuControl, &menuList);
            menuBound = true;
            boundMenuCount = menuCount;
        }
        display.setControlValue(menuControl, selected);
        display.setControlShow(menuControl);
        display.refreshControl(menuControl);
    }
    else display.setControlHide(menuControl);

    // Use the same focused/inactive list colors as the Sample Loader.
    const auto colorsFor = [&](unsigned column) {
        return column == activeColumn ? display.getTheme().sampleLoaderActiveListColors
                                      : display.getTheme().sampleLoaderInactiveListColors;
    };
    display.setControlColors(control, colorsFor(0));
    display.setControlColors(stagesControl, colorsFor(1));
    display.setControlColors(menuControl, colorsFor(1));
    display.setControlColors(parametersControl, colorsFor(2));
    display.setControlColors(valuesControl, colorsFor(3));

    bool available = page == Chain && !busy() && ensureSchema();
    const Node& n = node();
    const auto& d = schema();
    unsigned parameterCount = available ? visibleParameterCount() : 0;
    const unsigned parameterCursor = parameterCount ? std::min(parameterSelection, parameterCount - 1) : 0;
    if (!busy()) parameterSelection = parameterCursor;
    unsigned row=0;
    for (unsigned i = 0; i < n.count && available; ++i)
        if(!stpalette::contains(n.effect) || stpalette::visible(n,i)) parameterPointers[row++] = const_cast<char*>(d.labelsText[i]);
    if (available && parameterCount > n.count) parameterPointers[row++] = lengthLabel;
    if (!parameterCount) parameterPointers[0] = const_cast<char*>(n.effect == kSample ? "Sample source" :
        busy() ? "Preparing..." : n.count ? "Unavailable" : "No parameters");
    parametersList.data = parameterPointers;
    parametersList.length = n.effect == kSample ? 0 : std::max(1u, parameterCount);
    parametersList.linesCount = std::min<unsigned>(parametersList.length, 12);
    parametersList.lineWidthMax = activeColumn == 2 ? 178 : 0;
    parametersList.lineTextLenMax = 24;
    if (boundParameterCount != parametersList.length || parametersList.start != int(parameterCursor))
    {
        parametersList.start = parameterCursor;
        display.setControlData(parametersControl, &parametersList);
        boundParameterCount = parametersList.length;
    }
    display.setControlValue(parametersControl, parameterCursor);
    if (page != Chain || n.effect == kSample) display.setControlHide(parametersControl);
    else display.setControlShow(parametersControl);
    display.refreshControl(parametersControl);

    display.setControlHide(valuesControl);
    display.setControlHide(valueBar);
    // A queued auto-refresh must not follow strings into a plugin image that
    // the offline runner is about to replace.
    if (!available) valuesList.length = 0;
    unsigned param = parameterIndex();
    float value = 0, low = 0, high = 1;
    const char* unit = "";
    if (available && param < n.count)
    {
        value = n.params[param]; low = d.downConstrain[param]; high = d.upConstrain[param];
        unit = d.afterValueText[param] ? d.afterValueText[param] : "";
        snprintf(valueText, sizeof(valueText), d.displayType[param] == 'd' ? "%.0f%s" : "%.3f%s", value * d.displayMult[param], unit);
        if (stpalette::contains(n.effect) && (param==stpalette::Type || param==stpalette::Sync))
        {
            unsigned length=param==stpalette::Type?6:stpalette::syncCount();
            for(unsigned i=0;i<length;++i) valuePointers[i]=const_cast<char*>(param==stpalette::Type?stpalette::typeName(n.effect,i):stpalette::syncName(i));
            valuesList.data=valuePointers; valuesList.length=length;
            valuesList.linesCount=std::min(12u,length); valuesList.lineWidthMax=activeColumn==3?78:0;
            valuesList.lineTextLenMax=32; valuesList.start=unsigned(value);
            display.setControlData(valuesControl,&valuesList); display.setControlValue(valuesControl,unsigned(value));
            display.setControlShow(valuesControl); display.refreshControl(valuesControl);
            snprintf(valueText,sizeof(valueText),"%s",valuePointers[unsigned(value)]);
        }
        else if (n.effect == kPlugin && PtfxManager::getActiveHeader().params[param].display_type == PARAM_LIST)
        {
            const auto& p = PtfxManager::getActiveHeader().params[param];
            unsigned offset = uint8_t(p.unit[0]) | unsigned(uint8_t(p.unit[1])) << 8;
            char* item = reinterpret_cast<char*>(PtfxLoader::getCodeBuffer() + offset);
            unsigned length = uint8_t(p.unit[2]);
            for (unsigned i = 0; i < length; ++i) { valuePointers[i] = item; item += strlen(item) + 1; }
            valuesList.data = valuePointers; valuesList.length = length;
            valuesList.linesCount = std::min<unsigned>(12, length);
            valuesList.lineWidthMax = activeColumn == 3 ? 78 : 0; valuesList.lineTextLenMax = 32;
            if (boundValueParameter != param || valuesList.start != int(std::clamp(value, 0.0f, float(length - 1))))
            {
                valuesList.start = unsigned(std::clamp(value, 0.0f, float(length - 1)));
                display.setControlData(valuesControl, &valuesList);
                boundValueParameter = param;
            }
            unsigned selectedValue = unsigned(std::clamp(value, 0.0f, float(length - 1)));
            display.setControlValue(valuesControl, selectedValue);
            display.setControlShow(valuesControl);
            display.refreshControl(valuesControl);
            snprintf(valueText, sizeof(valueText), "%s", valuePointers[selectedValue]);
        }
        else display.setControlShow(valueBar);
    }
    else if (available)
    {
        value = definition.generatorFrames; low = 1; high = kCapacity;
        snprintf(valueText, sizeof(valueText), "%.3f s", value / 44100.0f);
        display.setControlShow(valueBar);
    }
    else valueText[0] = 0;
    int percent = high > low ? int((double(std::clamp(value, low, high)) - low) * 100 / (double(high) - low)) : 0;
    display.setControlValue(valueBar, std::clamp(percent, 0, 100));
    display.refreshControl(valueBar);
    valueName = available ? (param < n.count ? d.labelsText[param] : lengthLabel) : "";

    const char* titles[4] = {"Effects", page == Actions ? "Actions" : page == SavedPresets ? (savingPreset ? "Save Preset" : "Load Preset") :
        page == Destinations ? "Render to" : "Chain", "Parameter", "Value"};
    for (unsigned i = 0; i < 4; ++i)
    {
        display.setControlText(headers[i], titles[i]);
        if (page != Chain && i >= 2) display.setControlHide(headers[i]);
        else display.setControlShow(headers[i]);
        display.refreshControl(headers[i]);
    }
    display.setControlValue(focusControl, activeColumn);
    display.setControlShow(focusControl);
    display.refreshControl(focusControl);
}

void ChainScreen::navigate(int delta)
{
    if (busy() || !delta) return;
    if (mixHeld()) { editMix(-delta); return; }
    if (page != Chain)
        selected = std::clamp<int>(int(selected) + delta, 0, menuCount ? menuCount - 1 : 0);
    else if (focus == 0)
        browserSelection = std::clamp<int>(int(browserSelection) + delta, 0, count ? count - 1 : 0);
    else if (focus == 1)
    {
        const int firstStage = definition.source.effect == kSample && definition.count ? 1 : 0;
        stageSelection = std::clamp<int>(int(stageSelection) + delta, firstStage, definition.count);
        selectedNode = stageSelection;
        parameterSelection = 0;
        schemaReady = false;
    }
    else if (focus == 2 && ensureSchema())
    {
        unsigned length = visibleParameterCount();
        parameterSelection = std::clamp<int>(int(parameterSelection) + delta, 0, length ? length - 1 : 0);
        boundValueParameter = 255;
    }
    else if (focus == 3) {
        if(!ensureSchema())return;
        unsigned parameter=parameterIndex();
        bool list=parameter<node().count && schema().displayType[parameter]=='l';
        editValue(list?delta:-delta); return;
    }
    show(page);
}

void ChainScreen::shiftColumn(int delta)
{
    if (busy()) return;
    if (mixHeld()) { editMix(delta); return; }
    if (page != Chain)
    {
        if (delta < 0) { if (page == Destinations) output = Audition; focus = 1; show(Chain); }
        else choose();
        return;
    }
    focus = std::clamp<int>(int(focus) + delta, 0, 3);
    if (focus >= 2 && !ensureSchema()) focus = 1;
    show(Chain);
}

void ChainScreen::editValue(int delta)
{
    if (!ensureSchema()) return;
    Node& n = node();
    unsigned i = parameterIndex();
    if(i<n.count) {
        const auto& d=schema();
        if(std::clamp(n.params[i]+delta*d.changeStep[i],d.downConstrain[i],d.upConstrain[i])==n.params[i]) return;
    }
    bool typeChange=stpalette::contains(n.effect) && (i==stpalette::Type || i==stpalette::Sync);
    changed(!typeChange);
    if (i >= n.count)
    {
        unsigned step = (n.flags & PTFX_FLAG_WAVETABLE) ? 2048 : 441;
        definition.generatorFrames = std::clamp<int64_t>(int64_t(definition.generatorFrames) + int64_t(delta) * step,
            step == 2048 ? 2048 : 1, kCapacity);
    }
    else
    {
        const auto& d = schema();
        n.params[i] = std::clamp(n.params[i] + delta * d.changeStep[i], d.downConstrain[i], d.upConstrain[i]);
    }
    if(typeChange) { schemaReady=false; boundParameterCount=255; boundValueParameter=255; }
    else publishParameters();
    show(Chain);
}

void ChainScreen::turn(int delta)
{
    if (busy()) return;
    if (mixHeld()) editMix(delta);
    else if (page == Chain && focus == 3) editValue(delta);
    else navigate(delta);
}

void ChainScreen::browseSource()
{
    replacingEffect = false;
    focus = 0;
    for (unsigned i = 0; i < count; ++i)
    {
        unsigned id = entries[i];
        if (id >= 256 && ((definition.source.effect == kSample && (PtfxManager::getPlugin(id - 256)->flags & PTFX_FLAG_GENERATOR)) ||
            (definition.source.effect == kPlugin && !strcmp(PtfxManager::getPlugin(id - 256)->filename, definition.source.filename))))
        { browserSelection = i; break; }
    }
    snprintf(notice, sizeof(notice), "Choose a generator, then Use src");
    show(Chain);
}

void ChainScreen::addSelectedEffect()
{
    if (!count || busy()) return;
    unsigned id = entries[browserSelection];
    bool source = id == kSample || (id >= 256 && (PtfxManager::getPlugin(id - 256)->flags & PTFX_FLAG_GENERATOR));
    if (replacingEffect && !selectedNode && !source)
    { error("Select a chain effect to replace"); return; }
    if (replacingEffect && selectedNode && source)
    { error("Generators belong in Source"); return; }
    if (!source && !replacingEffect && definition.count >= kMaxStages)
    { error("Maximum 8 effects"); return; }
    Node n;
    if (id >= 256)
    {
        if (!pluginNode(id - 256, n)) { error("Cannot load plugin"); return; }
    }
    else
    {
        n.effect = id;
        if(stpalette::contains(id)) n=stpalette::defaults(id);
        else if (id != kSample)
        {
            n.count = effectDisplayConstantParams[id].paramsNumber;
            memcpy(n.params, effectDefaultParamsTable[id].parameter, sizeof(n.params));
        }
    }
    changed();
    if (source)
    {
        definition.source = n;
        stageSelection = 0;
        if (n.flags & PTFX_FLAG_WAVETABLE) definition.generatorFrames = 8 * 2048;
    }
    else if (replacingEffect) definition.stages[selectedNode - 1] = n;
    else
    {
        if (!definition.add(n)) { error("Maximum 8 effects"); return; }
        stageSelection = definition.count;
    }
    bool replaced = replacingEffect;
    replacingEffect = false;
    selectedNode = stageSelection;
    schemaReady = false;
    parameterSelection = 0;
    // Stay in the browser after Add so several effects can be appended quickly.
    focus = replaced ? 1 : 0;
    show(Chain);
}

void ChainScreen::choose()
{
    if (busy()) return;
    if (page == Chain)
    {
        if (focus == 0) addSelectedEffect();
        else if (focus == 1) { selected = 0; show(Actions); }
        else if (ensureSchema()) { focus = 3; show(Chain); }
        return;
    }
    if (page == Actions)
    {
        unsigned action = menuEntries[selected];
        if (action == 12) { measureCpu(); return; }
        if (action == 13) { preview(NewSlot); return; }
        if (action == 20)
        {
            replacingEffect = true;
            focus = 0;
            snprintf(notice, sizeof(notice), "Choose an effect, then Replace");
            show(Chain); return;
        }
        if (action >= 10)
        { savingPreset = action == 11; selected = 0; show(SavedPresets); return; }
        if (!selectedNode) { browseSource(); return; }
        changed();
        unsigned i = selectedNode - 1;
        if (selected == 0) definition.stages[i].bypass = !definition.stages[i].bypass;
        if (selected == 1 && definition.move(i, -1)) --selectedNode;
        if (selected == 2 && definition.move(i, 1)) ++selectedNode;
        if (selected == 3 && !definition.duplicate(i)) snprintf(notice, sizeof(notice), "Maximum 8 effects");
        if (selected == 4) { definition.remove(i); selectedNode = std::min<unsigned>(selectedNode, definition.count); }
        stageSelection = selectedNode;
        schemaReady = false;
        focus = 1;
        show(Chain);
    }
    else if (page == SavedPresets && menuEntries[selected] != 255) presetFile(menuEntries[selected], savingPreset);
    else if (page == Destinations && menuEntries[selected] != 255) render(menuEntries[selected]);
}

void ChainScreen::measureCpu()
{
    if(!stpalette::contains(node().effect) || busy())return;
    stopAudio();
    Timer* timer=Tracker::getInstance()->getPlatform()->getTimer();
#ifdef TRACKER_DESKTOP
    // Headless scripts freeze virtual firmware time inside a command. Measure
    // actual host work rather than exporting meaningless zero-duration rows.
    auto clock=[](void*) -> uint32_t { return uint32_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); };
    unsigned ticksPerUs=1;
#else
    auto clock=[](void* t) -> uint32_t {return static_cast<Timer*>(t)->clockCycles();};
    unsigned ticksPerUs=timer->clockCyclesPerUs();
#endif
    fs->mkdir(0,"/Effects");
    auto [ok,file]=fs->openFile("/Effects/StPaletteCPU.csv",FileSystem::kWriteCreateNew);
    if(!ok){error("Cannot save CPU report");return;}
    static const char heading[]="category,type,quality,processing_quality,channels,max_ticks,average_ticks,ticks_per_us,arena_bytes\n";
    bool success=fs->writeFile(heading,sizeof(heading)-1,file)==sizeof(heading)-1;
    for(unsigned q=0;q<3 && success;++q) {
        Node n=node();n.quality=q;
        RealtimeArena arena;arena.reset(externalRamScratchBuff,SHARED_SCRATCH_MEMORY_SIZE);
        auto result=stpalette::profile(n,chainChannels(),{&arena,[](void* a,size_t bytes){return static_cast<RealtimeArena*>(a)->allocate(bytes);}},timer,clock);
        char line[192];
        int length=snprintf(line,sizeof(line),"%s,%s,%u,%u,%u,%lu,%lu,%lu,%lu\n",stpalette::category(n.effect),stpalette::typeName(n.effect,unsigned(n.params[0])),q,stpalette::processingQuality(n),chainChannels(),(unsigned long)result.maximumTicks,(unsigned long)(result.blocks?result.totalTicks/result.blocks:0),(unsigned long)ticksPerUs,(unsigned long)(SHARED_SCRATCH_MEMORY_SIZE-arena.available()));
        success=result.success && length>0 && unsigned(length)<sizeof(line) && fs->writeFile(line,length,file)==length;
    }
    fs->closeFile(file);
    schemaReady=false;
    snprintf(notice,sizeof(notice),success?"Saved Effects/StPaletteCPU.csv":"CPU measurement failed");
    focus=1;show(Chain);
}

void ChainScreen::removeOrReset()
{
    if (busy() || page != Chain) return;
    if (focus == 3 && ensureSchema())
    {
        changed(true);
        unsigned i = parameterIndex();
        if(stpalette::contains(node().effect) && i==stpalette::Type) changed();
        if (i < node().count) node().params[i] = stpalette::contains(node().effect)?stpalette::defaults(node().effect).params[i]:node().effect == kPlugin ?
            PtfxManager::getActiveHeader().params[i].default_val : effectDefaultParamsTable[node().effect].parameter[i];
        schemaReady=false;
        publishParameters();
    }
    else if (focus == 1 && !selectedNode && definition.source.effect == kPlugin)
    {
        changed();
        definition.source = {};
        schemaReady = false;
    }
    else if (focus == 1 && selectedNode)
    {
        changed();
        definition.remove(selectedNode - 1);
        stageSelection = selectedNode = std::min<unsigned>(selectedNode, definition.count);
        schemaReady = false;
    }
    show(Chain);
}

void ChainScreen::input(unsigned button)
{
    if (button == kBackButton && page == Chain) { leave(); return; }
    if (busy()) return;
    if (page != Chain)
    {
        if (button == 0) { if (page == Destinations) output = Audition; focus = 1; show(Chain); }
        else if (button == 1) choose();
        return;
    }
    if (button == 0) { if (playing) { stopAudio(); show(Chain); } else preview(); }
    else if (button == 1) addSelectedEffect();
    else if (button == kApplyButton) preview(Apply);
    else if (button == kActionsButton) { focus = 1; selected = 0; show(Actions); }
    else if (button == kValueButton && ensureSchema()) { focus = 3; show(Chain); }
}

void ChainScreen::playPrepared()
{
    const Audio& audio = result();
    std::vector<int16_t*> addresses = { audio.channel[0], audio.channel[audio.channels == 2 ? 1 : 0] };
    instrumentPlayer[0].clearInterfacePlayingEndFlag();
    mtPadBoard.startNoteWithoutInstrument(60, addresses, audio.frames, DEFAULT_INSTRUMENT_VOLUME, INTERFACE_BUTTON_PREVIEW);
    playing = true;
    show(Chain);
}

void ChainScreen::editMix(int delta)
{
    const unsigned next = std::clamp<int>(int(definition.mix) + delta, 0, 100);
    if (next == definition.mix) return;
    definition.mix = next;
    notice[0] = 0;
    mixedReady = false;
    if (livePlaying && !waitingFade) publishParameters();
    else if (runner.state() == Runner::State::Ready)
    {
        const bool resume = playing;
        stopAudio();
        prepareOutput(resume);
        return;
    }
    show(Chain);
}

void ChainScreen::startPrepared(const Audio& source)
{
    mixedReady = false;
    generatorDryFrames = 0;
    backend.dryDelay = 0;
    if (!runner.start(definition, source, buffers, kCapacity)) { error(runner.error()); return; }
    work = Processing;
}

void ChainScreen::finishOutput(bool play)
{
    if (output == Apply) render(sourceSlot);
    else if (output == NewSlot) { selected = 0; show(Destinations); }
    else if (play) playPrepared();
    else show(Chain);
}

void ChainScreen::prepareOutput(bool play)
{
    if (definition.mix == 100) { mixedReady = false; finishOutput(play); return; }
    if (mixedReady && mixedPercent == definition.mix) { finishOutput(play); return; }
    mixedReady = false;
    const auto& wet = runner.result();
    const unsigned destination = wet.channel[0] == buffers[0][0] ? 1 : 0;
    mixed = {{buffers[destination][0], buffers[destination][1]}, wet.frames, wet.channels};
    const bool sample = definition.source.effect == kSample;
    const unsigned frames = sample ? sourceFrames : generatorDryFrames;
    const unsigned channels = sample ? (sourceStereo ? 2 : 1) : generatorDryChannels;
    if (!openIo(sample ? snapshotPath : generatorDryPath, FileSystem::kRead) ||
        fs->getFileSize(io) != size_t(frames) * channels * 2)
    { error("Cannot read dry source for Mix"); return; }
    preparedMix.reset(definition.mix);
    playAfterMix = play;
    work = Mixing;
    show(Chain);
}

void ChainScreen::updateMix()
{
    const auto& wet = runner.result();
    const unsigned count = std::min<uint32_t>(kIoFrames, wet.frames - offset);
    const bool sample = definition.source.effect == kSample;
    const unsigned dryFrames = sample ? sourceFrames : generatorDryFrames;
    const unsigned dryChannels = sample ? (sourceStereo ? 2 : 1) : generatorDryChannels;
    auto* dry = reinterpret_cast<int16_t*>(externalRamScratchBuff);
    static_assert(2 * kIoFrames * sizeof(int16_t) <= SHARED_SCRATCH_MEMORY_SIZE, "Mix scratch exceeds arena");
    const unsigned prefix = offset < backend.dryDelay ? std::min<uint32_t>(count, backend.dryDelay - offset) : 0;
    const unsigned at = offset + prefix >= backend.dryDelay ? offset + prefix - backend.dryDelay : 0;
    const unsigned read = at < dryFrames ? std::min(count - prefix, dryFrames - at) : 0;
    for (unsigned c = 0; c < dryChannels; ++c)
    {
        auto* channel = dry + c * kIoFrames;
        memset(channel, 0, count * sizeof(int16_t));
        if (read && (!fs->seekFile((uint32_t(c) * dryFrames + at) * 2, io) ||
            fs->readFile(channel + prefix, read * 2, io) != int32_t(read * 2)))
        { error("Dry source read failed"); return; }
    }
    for (unsigned i = 0; i < count; ++i)
    {
        const int32_t gain = preparedMix.advance();
        int16_t left = dry[i];
        if (wet.channels == 1 && dryChannels == 2) left = int16_t((int32_t(left) + dry[kIoFrames + i]) / 2);
        mixed.channel[0][offset + i] = ChainMix::blend(left, wet.channel[0][offset + i], gain);
        if (wet.channels == 2)
            mixed.channel[1][offset + i] = ChainMix::blend(dry[(dryChannels == 2 ? kIoFrames : 0) + i], wet.channel[1][offset + i], gain);
    }
    offset += count;
    if (offset != wet.frames) return;
    closeIo();
    mixedPercent = definition.mix;
    mixedReady = true;
    work = Idle;
    finishOutput(playAfterMix);
}

void ChainScreen::startRealtime()
{
    runner.invalidate();
    mixedReady = false;
    for (unsigned i = 0; i < definition.count; ++i)
    {
        const Node& n = definition.stages[i];
        if (n.bypass || n.effect == kPlugin) continue;
        if(stpalette::contains(n.effect)) { if(!stpalette::valid(n)){error("Invalid native effect");return;} continue; }
        if (!supported(n.effect) || n.count != effectDisplayConstantParams[n.effect].paramsNumber)
        { error("Effect unavailable"); return; }
        const auto& d = effectDisplayConstantParams[n.effect];
        for (unsigned p = 0; p < n.count; ++p)
            if (!std::isfinite(n.params[p]) || n.params[p] < d.downConstrain[p] || n.params[p] > d.upConstrain[p])
            { error("Parameter outside supported range"); return; }
    }
    Audio source{ { buffers[0][0], buffers[0][1] }, sourceFrames, uint8_t(sourceStereo ? 2 : 1) };
    touched = true;
    liveLoader.fs = fs;
    // Place the live engine in editor RAM too; nine private stage headers and
    // DSP work blocks must not consume scarce internal SRAM on the devices.
    const uintptr_t begin = reinterpret_cast<uintptr_t>(buffers[1][0]);
    const uintptr_t aligned = (begin + 31) & ~uintptr_t(31);
    const size_t reserved = aligned - begin + sizeof(RealtimeChain);
    live = new (reinterpret_cast<void*>(aligned)) RealtimeChain;
    // Source remains immutable in the first work area. Private code, scratch,
    // delay lines and stage state use the second; the SD snapshot restores it.
    if (live->prepare(definition, source, reinterpret_cast<void*>(begin + reserved),
                      kCapacity * sizeof(int16_t) - reserved, liveLoader, kCapacity,mtProject.values.globalTempo))
    {
        liveRequested = false;
        livePlaying = playing = true;
        notice[0] = 0;
        engine.getSampleFxPreview()->setBudgetTimer(Tracker::getInstance()->getPlatform()->getTimer());
        liveTempo=mtProject.values.globalTempo;
        engine.getSampleFxPreview()->start(live);
        show(Chain);
        return;
    }
    snprintf(notice, sizeof(notice), "%s", live->error());
    if (live) { live->~RealtimeChain(); live = nullptr; }
    liveRequested = false;
    for(unsigned i=0;i<definition.count;++i) if(stpalette::contains(definition.stages[i].effect))
    { error(live ? live->error() : "Native preview memory limit"); return; }
    // Noncausal effects (normalization and prepending) keep exact offline
    // behavior. Never silently bypass a stage in order to start live audio.
    startPrepared(source);
    show(Chain);
}

void ChainScreen::preview(Output action)
{
    stopAudio();
    const bool render = action != Audition;
    bool native=false; for(unsigned i=0;i<definition.count;++i) native |= stpalette::contains(definition.stages[i].effect);
    if(native && (!definition.qualityResolved || !render) &&
       !stpalette::resolve(definition, chainChannels(), kCapacity*2-65536, render ? ~0u : RealtimeChain::kChainBudgetUs))
    { error(render ? "FX memory limit" : "FX estimate too high. Apply"); return; }
#ifdef TRACKER_DESKTOP
    bool armPlugin = definition.source.effect == kPlugin;
    for (unsigned i = 0; i < definition.count; ++i)
        armPlugin |= definition.stages[i].effect == kPlugin && !definition.stages[i].bypass;
    if (armPlugin) { error("PTFX: Tracker only"); return; }
#endif
    output = action;
    liveRequested = !render;
    if (render && runner.state() == Runner::State::Ready)
    { prepareOutput(false); return; }
    if (sourceFrames && !snapshotReady) { error("Source snapshot unavailable; reopen Effects"); return; }
    schemaReady = false;
    touched = true;
    if (definition.source.effect == kSample)
    {
        if (!snapshotReady || !sourceFrames) { error("Choose a generator or an existing sample"); return; }
        if (!openIo(snapshotPath, FileSystem::kRead)) { error("Cannot read source snapshot"); return; }
        work = Reading;
    }
    else if (liveRequested) { startRealtime(); return; }
    else
    {
        Audio source{ { buffers[0][0], buffers[0][1] }, 0, 1 };
        startPrepared(source);
    }
    show(Chain);
}

void ChainScreen::presetFile(unsigned slot, bool save)
{
    stopAudio();
    if (!slot && !save && haveLast)
    {
        changed();
        definition = lastUsed;
        memcpy(presetName, lastUsedPresetName, sizeof(presetName));
        stageSelection = 0;
        schemaReady = false;
        focus = 1;
        selected = 0;
        show(Chain);
        return;
    }
    if (!slot || slot > savedPresetCount) { error("Choose a saved preset"); return; }
    char path[80], temp[84], backup[84];
    const char* filename = menuPointers[slot - 1];
    const char* extension = filename + strlen(filename) + 1;
    if (save)
    {
        fs->mkdir(0, "/Effects");
        fs->mkdir(0, kPresetDir);
    }
    snprintf(path, sizeof(path), "%s/%s.%s", kPresetDir, filename, extension);
    snprintf(temp, sizeof(temp), "%s.tmp", path);
    snprintf(backup, sizeof(backup), "%s.bak", path);
    // Recover an interrupted replacement before attempting another read/write.
    if (!fs->exists(path) && fs->exists(backup)) fs->rename(backup, path);
    uint8_t bytes[kPresetFileBytes];
    if (save)
    {
        if (!encodePreset(definition, bytes, sizeof(bytes)) || !openIo(temp, FileSystem::kWriteCreateNew))
        { error("Cannot save preset"); return; }
        bool ok = fs->writeFile(bytes, sizeof(bytes), io) == int32_t(sizeof(bytes));
        closeIo();
        if (!ok) { fs->remove(temp); error("Preset write failed"); return; }
        fs->remove(backup);
        bool previous = fs->exists(path);
        if ((previous && !fs->rename(path, backup)) || !fs->rename(temp, path))
        { if (previous && !fs->exists(path)) fs->rename(backup, path); fs->remove(temp); error("Preset save failed"); return; }
        fs->remove(backup);
        snprintf(notice, sizeof(notice), "Saved %s", filename);
    }
    else
    {
        if (!openIo(path, FileSystem::kRead)) { error("Preset not found"); return; }
        ChainDefinition loaded;
        bool ok = fs->getFileSize(io) == sizeof(bytes) && fs->readFile(bytes, sizeof(bytes), io) == int32_t(sizeof(bytes)) && decodePreset(bytes, sizeof(bytes), loaded);
        closeIo();
        if (!ok) { error("Invalid or unsupported preset"); return; }
        changed(); definition = loaded; stageSelection = 0; schemaReady = false; focus = 1;
        snprintf(notice, sizeof(notice), "Loaded %s", filename);
    }
    snprintf(presetName, sizeof(presetName), "%s", filename);
    selected = 0;
    show(Chain);
}

void ChainScreen::render(unsigned slot)
{
    stopAudio();
    const bool replacing = output == Apply;
    if (slot >= kSampleInstrumentsCount || (!replacing && mtProject.instrument[slot].isActive) ||
        runner.state() != Runner::State::Ready)
    { error("Choose an empty sample slot"); return; }
    destinationSlot = slot;
    outputInstrument = sourceInstrument;
    if (definition.source.effect != kSample) newFileManager.setDefaultActiveInstrument(&outputInstrument);
    const auto& audio = result();
    outputInstrument.isActive = 1;
    outputInstrument.sample.length = audio.frames;
    outputInstrument.sample.address = nullptr;
    if (!replacing || !sourceInstrument.isActive)
        snprintf(outputInstrument.sample.file_name, SAMPLE_NAME_SIZE, "Chain %02u", slot + 1);
    else
        memcpy(outputInstrument.sample.file_name, sourceInstrument.sample.file_name, SAMPLE_NAME_SIZE);
    // Length-changing operations have no general loop/slice mapping yet.
    if (definition.source.effect != kSample || audio.frames != sourceFrames)
    {
        outputInstrument.sliceNumber = 0;
        memset(outputInstrument.slices, 0, sizeof(outputInstrument.slices));
        outputInstrument.loopPoint1 = 1;
        outputInstrument.loopPoint2 = MAX_16BIT - 1;
        outputInstrument.playMode = (definition.source.flags & PTFX_FLAG_WAVETABLE) ? playModeWavetable : playModeSingleShot;
    }
    if (outputInstrument.playMode == playModeWavetable &&
        (!outputInstrument.sample.wavetable_window_size || audio.frames % outputInstrument.sample.wavetable_window_size))
    { error("Wavetable needs complete windows"); return; }
    if (replacing)
    {
        if (!newFileManager.importSampleFromSampleEditor(audio.channel[0],
              audio.channels == 2 ? audio.channel[1] : nullptr, audio.frames, slot, outputInstrument))
        { error("Cannot apply sample: check SD and sample memory"); return; }
        work = Applying;
        show(Chain);
        return;
    }
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
    const auto& audio = result();
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
    haveDraft = false;
    newFileManager.setInstrumentStructChanged(destinationSlot);
    newFileManager.setProjectStructChanged();
    lastUsed = definition;
    memcpy(lastUsedPresetName, presetName, sizeof(lastUsedPresetName));
    haveLast = true;
    // Restore current editor data before it is saved or another instrument is selected.
    work = Idle;
    output = Audition;
    discard();
}

void ChainScreen::leave()
{
    haveDraft = !committed && (definition.count || definition.source.effect == kPlugin);
    draftSlot = sourceSlot;
    autoPreview = false;
    stopAudio();
    // A commit already writing is allowed to finish before dispatching a view switch.
    if (work == Writing || work == Restoring || work == Applying) return;
    runner.cancel();
    output = Audition;
    liveRequested = false;
    closeIo();
    work = Idle;
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
    if (live) { live->~RealtimeChain(); live = nullptr; }
    const bool applied = work == Applying;
    if (!applied)
    {
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
    }
    fs->remove(snapshotPath);
    fs->remove(generatorDryPath);
    fs->remove(outputPath);
    work = Idle;
    open = false;
    newFileManager.suspendAutosave(false);
    sequencer.setMidiInVoiceMode(Sequencer::midiInVoiceMode_SampleEditor);
    if (visible) hideColumns();
    visible = false;
    if (applied) return;
    if (committed)
    {
        mtProject.values.lastUsedInstrument = destinationSlot;
        editor->start(0);
        return;
    }
    editor->reloadSpectrumData();
    editor->switchScreen(cSampleEditor::mainScreen);
    editor->restoreFunctions();
}

void ChainScreen::update()
{
    if(open && livePlaying && live && liveTempo!=mtProject.values.globalTempo) {
        liveTempo=mtProject.values.globalTempo;
        DISABLE_AUDIO_INTERRUPTS(); live->setTempo(liveTempo); ENABLE_AUDIO_INTERRUPTS();
    }
    if(open && waitingFade) {
        if(engine.getSampleFxPreview()->isPlaying()) return;
        bool resume=resumeLive; stopAudio(); resumeLive=resume;
    }
    const auto budget = open && livePlaying && live ? live->budgetOverrun() : RealtimeChain::BudgetOverrun{};
    if(budget) {
        unsigned best=kMaxStages,saving=0;
        for(unsigned i=0;i<definition.count;++i) {
            Node& n=definition.stages[i]; if(!stpalette::contains(n.effect)||n.bypass||n.quality==stpalette::Minimum)continue;
            Node lower=n; if(!stpalette::lowerQuality(lower))continue;
            unsigned gain=stpalette::estimatedUs(n,chainChannels())-stpalette::estimatedUs(lower,chainChannels());
            if(gain>saving){saving=gain;best=i;}
        }
        if(best==kMaxStages)
        {
            stopAudio();
            if (budget.chainUs > RealtimeChain::kChainBudgetUs)
                snprintf(notice, sizeof(notice), "FX %u/%u us. Apply", budget.chainUs, RealtimeChain::kChainBudgetUs);
            else
                snprintf(notice, sizeof(notice), "Audio %u FX %u. Apply", budget.callbackUs, budget.chainUs);
            show(page);
            return;
        }
        changed(); stpalette::lowerQuality(definition.stages[best]); definition.qualityResolved=true; return;
    }
    if (open && autoPreview && work == Idle)
    { autoPreview = false; preview(); return; }
    if (open && resumeLive && work == Idle)
    { resumeLive = false; preview(); return; }
    if (open && livePlaying && !engine.getSampleFxPreview()->isPlaying())
    {
        livePlaying = playing = false;
        if (live->hasFailed())
        {
            // Delay/history growth and plugin length changes need a new live
            // instance. Rebuild automatically; no full render is required.
            if (strstr(live->error(), "restart preview")) { preview(); return; }
            snprintf(notice, sizeof(notice), "%s", live->error());
        }
        if (!visible) { leave(); return; }
        show(page);
    }
    if (open && playing && !livePlaying && work == Idle && instrumentPlayer[0].getInterfacePlayingEndFlag())
    {
        instrumentPlayer[0].clearInterfacePlayingEndFlag();
        mtPadBoard.clearVoice(0);
        playing = false;
        originalPlaying = false;
        if (!visible) { leave(); return; }
        show(page);
    }
    if (!open || work == Idle) return;
    if (work == Applying)
    {
        const auto status = newFileManager.getStatus();
        if (status >= fmError)
        {
            newFileManager.clearStatus();
            pendingEditorAction = {};
            error("Apply failed; chain retained");
        }
        else if (status == fmImportSampleFromSampleEditorEnd)
        {
            lastUsed = definition;
            memcpy(lastUsedPresetName, presetName, sizeof(lastUsedPresetName));
            haveLast = true;
            haveDraft = false;
            // The editor reopens the committed slot on its next update.
            finishRestore();
            editor->ip->blockAllInputs();
        }
        return;
    }
    if (work == Mixing) { updateMix(); return; }
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
    if (work == Snapshot || work == Reading || work == Restoring || work == Writing || work == CapturingDry)
    {
        const bool writing = work == Snapshot || work == Writing || work == CapturingDry;
        uint32_t frames = work == Writing || work == CapturingDry ? (work == Writing ? result().frames : runner.result().frames) : sourceFrames;
        unsigned channels = work == Writing || work == CapturingDry ? (work == Writing ? result().channels : runner.result().channels) : (sourceStereo ? 2 : 1);
        unsigned size = std::min<uint32_t>(kIoFrames, frames - offset);
        int16_t* address = nullptr;
        if (work == Snapshot || work == Restoring) address = sourceAddress[channel];
        else if (work == Writing) address = result().channel[channel];
        else if (work == CapturingDry) address = runner.result().channel[channel];
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
        else if (finished == CapturingDry) { work = Processing; show(Chain); }
        else if (originalPlaying)
        {
            std::vector<int16_t*> addresses = { buffers[destinationSlot][0], buffers[destinationSlot][sourceStereo ? 1 : 0] };
            instrumentPlayer[0].clearInterfacePlayingEndFlag();
            mtPadBoard.startNoteWithoutInstrument(60, addresses, sourceFrames, DEFAULT_INSTRUMENT_VOLUME, INTERFACE_BUTTON_PREVIEW);
            playing = true;
            show(Chain);
        }
        else if (liveRequested) startRealtime();
        else
        {
            Audio source{ { buffers[0][0], buffers[0][1] }, sourceFrames, uint8_t(sourceStereo ? 2 : 1) };
            startPrepared(source);
            show(Chain);
        }
    }
    else if (work == Processing)
    {
        unsigned before = runner.stage();
        runner.update();
        if (runner.state() == Runner::State::Failed) { error(runner.error()); return; }
        if (runner.state() == Runner::State::Ready)
        { work = Idle; prepareOutput(output == Audition); }
        else if (before == 0 && runner.stage() == 1 && definition.source.effect == kPlugin)
        {
            generatorDryFrames = runner.result().frames;
            generatorDryChannels = runner.result().channels;
            if (!openIo(generatorDryPath, FileSystem::kWriteCreateNew)) { error("Cannot snapshot generator dry source"); return; }
            work = CapturingDry;
            show(Chain);
        }
        else if (runner.stage() != before)
        { snprintf(notice, sizeof(notice), "Stage %u/%u", runner.stage(), definition.count); show(Chain); }
    }
}
}

bool sampleChainHasDraft() { return screen.haveDraft && screen.draftSlot == mtProject.values.lastUsedInstrument; }
unsigned sampleChainStageCount() { return screen.definition.count; }
bool sampleChainIsOpen() { return screen.open && screen.visible; }
bool sampleChainUpdate()
{
    if (screen.open) { screen.update(); return true; }
    if (screen.pendingEditorAction && newFileManager.getStatus() == fmIdle &&
        newFileManager.getCurrentOperation() == fmNoOperation)
    {
        auto action = std::move(screen.pendingEditorAction);
        screen.pendingEditorAction = {};
        action();
        return true;
    }
    return false;
}
bool sampleChainDeferEditorAction(std::function<void()> action)
{
    if (!screen.open) return false;
    screen.pendingEditorAction = std::move(action);
    screen.leave();
    return true;
}
void sampleChainResetControls()
{
    // Module switches destroy these controls. Recreate them on the next open.
    screen.control = screen.stagesControl = screen.parametersControl = -1;
    screen.valuesControl = screen.valueBar = screen.focusControl = screen.menuControl = -1;
    for (auto& handle : screen.headers) handle = -1;
    screen.listBound = false;
    screen.toolbarReady = false;
}

static void openChainSession(cSampleEditor* editor, bool visible)
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
    if (!screen.haveDraft || screen.draftSlot != screen.sourceSlot)
    {
        screen.definition = {};
        screen.presetName[0] = 0;
    }
    screen.runner.invalidate();
    screen.notice[0] = 0;
    screen.open = true;
    screen.visible = visible;
    screen.toolbarReady = false;
    screen.autoPreview = !visible;
    screen.touched = false;
    screen.snapshotReady = false;
    screen.output = ChainScreen::Audition;
    screen.mixedReady = false;
    screen.generatorDryFrames = 0;
    screen.liveRequested = false;
    screen.selected = 0;
    screen.listBound = false;
    screen.boundStageCount = 255;
    screen.boundParameterCount = 255;
    screen.boundValueParameter = 255;
    screen.schemaReady = false;
    screen.stageSelection = screen.selectedNode = screen.parameterSelection = screen.focus = 0;
    screen.replacingEffect = false;
    screen.menuBound = false;
    screen.work = ChainScreen::Idle;
    screen.fs->mkdir(1, kTempDir);
    // Only this controller owns this directory. Clear abandoned internal temp files.
    screen.fs->removeOnlyDirFiles(kTempDir);
    static unsigned session = 0;
    snprintf(screen.snapshotPath, sizeof(screen.snapshotPath), "%s/source-%u.raw", kTempDir, ++session);
    snprintf(screen.generatorDryPath, sizeof(screen.generatorDryPath), "%s/dry-%u.raw", kTempDir, session);
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
    if (!visible) { screen.show(ChainScreen::Chain); return; }
    for (unsigned i = 0; i < 8; ++i) display.setControlHide(editor->bar[i]);
    for (unsigned i = 0; i < cSampleEditor::kEffectPageParams; ++i) display.setControlHide(editor->paramList[i]);
    editor->hideSpectrum(); editor->hideSpectrumPoints(); editor->hidePlayhead(); editor->hideEffectList();
    display.setControlHide(editor->frame);
    display.setControlHide(editor->blankSpaceCover);
    if (screen.control < 0)
    {
        strControlProperties properties;
        for (unsigned i = 0; i < 4; ++i) screen.columnsFrame.places[i] = screen.columnPlaces[i];
        screen.columnsFrame.placesCount = 4;
        properties.style = 0;
        properties.data = &screen.columnsFrame;
        // Frames are allocated before content, as in Pattern Fill, so their
        // outline is drawn above each column's background.
        screen.focusControl = display.createControl<cFrame>(&properties);
        properties.x = 1; properties.y = 60; properties.w = 297; properties.h = 363;
        properties.style = controlStyleBackground;
        properties.data = &screen.list;
        screen.control = display.createControl<cList>(&properties);
        properties.x = 301; properties.w = 197; properties.data = &screen.stagesList;
        screen.stagesControl = display.createControl<cList>(&properties);
        properties.data = &screen.menuList;
        screen.menuControl = display.createControl<cList>(&properties);
        properties.x = 501; properties.w = 197; properties.data = &screen.parametersList;
        screen.parametersControl = display.createControl<cList>(&properties);
        properties.x = 701; properties.w = 97; properties.data = &screen.valuesList;
        screen.valuesControl = display.createControl<cList>(&properties);
        properties.data = nullptr;
        properties.h = 363;
        properties.style = controlStyleValue_0_100 | controlStyleBackground |
                           (display.getTheme().barShadow > 0 ? controlStyleGradient : 0) |
                           (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
        screen.valueBar = display.createControl<cBar>(&properties);
        properties.colors = display.getTheme().activeLabelsColors;
        properties.style = controlStyleBackground | controlStyleCenterX | controlStyleCenterY | controlStyleFont3;
        for (unsigned i = 0; i < 4; ++i)
        {
            properties.x = screen.columnPlaces[i][0] + (screen.columnPlaces[i][2] + 1) / 2;
            properties.w = screen.columnPlaces[i][2];
            properties.y = 31; properties.h = 28;
            properties.colors = display.getTheme().topBarLabelColors;
            screen.headers[i] = display.createControl<cLabel>(&properties);
        }
    }
    screen.show(ChainScreen::Chain);
}

void sampleChainOpen(cSampleEditor* editor)
{
    if (sampleChainDeferEditorAction([editor] { sampleChainOpen(editor); })) return;
    openChainSession(editor, true);
}

bool sampleChainPreview(cSampleEditor* editor)
{
    if (screen.open) { screen.leave(); return true; }
    if (!screen.haveDraft || screen.draftSlot != mtProject.values.lastUsedInstrument) return false;
    openChainSession(editor, false);
    return true;
}
