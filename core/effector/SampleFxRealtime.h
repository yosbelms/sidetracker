#pragma once

#include "SampleFxChain.h"
#include "tracker_plugin.h"
#include "palette/Palette.h"
#include "SampleFxMix.h"
#include <atomic>
#include <memory>

class mtEffect;

namespace samplefx
{
// All allocation and file access happens in prepare(), with playback stopped.
// process() and parameter application run only on the audio thread.
class RealtimeArena
{
  public:
    void reset(void* memory, size_t bytes) { base = static_cast<uint8_t*>(memory); remaining = bytes; }
    void* allocate(size_t bytes);
    size_t available() const { return remaining; }
  private:
    uint8_t* base = nullptr;
    size_t remaining = 0;
};

class RealtimeBuiltin
{
  public:
    RealtimeBuiltin();
    ~RealtimeBuiltin();
    void reset();
    bool prepare(const Node&, uint32_t frames, uint8_t channels, RealtimeArena&, unsigned stage = 1, float tempo = 120);
    void parameters(const Node&, float tempo = 120);
    bool process(int16_t* const* input, int16_t* const* output, unsigned frames);
    uint32_t tail() const;
    static bool supports(uint8_t effect);
  private:
    std::unique_ptr<mtEffect> fx;
    palette::Processor native;
    bool nativeParametersValid = true;
    Node settings;
    uint8_t channels = 1;
    uint32_t inputFrames = 0, position = 0, delayFrames = 0, historyFrames = 0;
    unsigned echoes = 0, heldFrames = 0;
    int16_t held[2] = {};
    int16_t* history[2] = {};
    float gains[99] = {};
};

struct RealtimePlugin
{
    PtfxHeader header = {};
    PtfxInitFunc init = nullptr;
    PtfxSetParamFunc parameter = nullptr;
    PtfxProcessBlockFunc process = nullptr;
    void* scratch = nullptr;
    uint32_t prepend = 0, append = 0;
};

class RealtimePluginLoader
{
  public:
    virtual ~RealtimePluginLoader() = default;
    virtual bool load(const Node&, RealtimePlugin&, RealtimeArena&) = 0;
    virtual const PtfxHostApi* hostApi() = 0;
};

class RealtimeChain
{
  public:
    static constexpr unsigned kBlock = 128;
    static constexpr unsigned kChainBudgetUs = 1000;
    static constexpr unsigned kCallbackBudgetUs = 2031;
    struct BudgetOverrun
    {
        unsigned chainUs;
        unsigned callbackUs;
        explicit operator bool() const { return chainUs || callbackUs; }
    };

    bool prepare(const ChainDefinition&, const Audio&, void* memory, size_t bytes,
                 RealtimePluginLoader&, uint32_t capacity, float tempo = 120);
    // Same topology only. Caller publishes settings while audio interrupts are disabled.
    void setParameters(const ChainDefinition& definition) { pending = definition; parametersDirty = true; }
    unsigned process(int16_t* left, int16_t* right, unsigned frames);
    bool finished() const { return position >= totalFrames || failed; }
    bool hasFailed() const { return failed; }
    const char* error() const { return message; }
    uint32_t length() const { return totalFrames; }
    uint32_t played() const { return position; }
    uint8_t channels() const { return outputChannels; }
    void release();
    void reportDuration(unsigned chainUs, unsigned callbackUs = 0);
    BudgetOverrun budgetOverrun() const;
    bool usesNative() const { return nativeUsed; }
    void setTempo(float value) { tempo=value; parametersDirty=true; }
  private:
    struct Stage
    {
        RealtimeBuiltin builtin;
        RealtimePlugin plugin;
        Node node;
        uint32_t inputFrames = 0, outputFrames = 0;
        uint8_t channels = 1, outputChannels = 1;
    } stages[kMaxStages + 1];
    Audio source;
    ChainMix mix;
    unsigned dryLatency = 0, dryHistoryFrames = 0;
    int16_t* dryHistory[2] = {};
    uint8_t dryChannels = 1;
    ChainDefinition pending;
    unsigned count = 0;
    uint32_t position = 0, totalFrames = 0, capacity = 0;
    uint8_t outputChannels = 1;
    bool parametersDirty = false, failed = false;
    unsigned slowBlocks = 0;
    // Both measurements come from one audio-thread update.
    std::atomic<uint32_t> overrun{0};
    float tempo=120;
    bool nativeUsed=false;
    const char* message = "";
    int16_t work[2][2][kBlock] = {};
    bool fail(const char* text) { message = text; failed = true; return false; }
    bool updateLengths();
    bool applyParameters();
};
}
