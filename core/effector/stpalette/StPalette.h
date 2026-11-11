#pragma once
#include "../SampleFxChain.h"

namespace samplefx { namespace stpalette {
constexpr uint8_t kFirst = 32, kLast = 35;
enum Parameter { Type, Amount, Macro, Drift, Sync, Tail, ParameterCount };
enum Quality : uint8_t { Full, Economy, Minimum };
struct Memory {
    void* context;
    void* (*allocate)(void*, size_t);
    void* get(size_t bytes) const { return allocate(context, bytes); }
};
bool contains(unsigned id);
const char* category(unsigned id);
const char* typeName(unsigned id, unsigned type);
const char* syncName(unsigned division);
unsigned syncCount();
float syncBeats(unsigned division);
Node defaults(unsigned id);
bool valid(const Node&);
bool visible(const Node&, unsigned parameter);
const char* label(unsigned parameter);
float maximum(unsigned parameter);
float step(unsigned parameter);
bool hasTail(const Node&);
bool hasSync(const Node&);
uint32_t seed(const Node&, unsigned stage);
// Conservative initial estimates, NOT calibrated device timing. The runtime
// watchdog and the hardware benchmark are required before timing certification.
unsigned estimatedUs(const Node&, unsigned channels);
size_t requiredMemory(const Node&, unsigned channels);
unsigned latencyFrames(const Node&);
bool resolve(ChainDefinition&, unsigned channels, size_t bytes, unsigned budgetUs = 1000);
unsigned processingQuality(const Node&);
bool lowerQuality(Node&);

class Processor {
public:
    bool prepare(const Node&, unsigned channels, uint32_t inputFrames,
                 Memory, unsigned stage = 1, float tempo = 120);
    bool parameters(const Node&, float tempo = 120);
    bool process(int16_t* const* input, int16_t* const* output, unsigned frames);
    uint32_t tail() const;
    void reset() { state = nullptr; }
    bool prepared() const { return state != nullptr; }
private:
    struct State;
    State* state = nullptr;
};
} }
