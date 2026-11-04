#pragma once

#include <stddef.h>
#include <stdint.h>

// Recipes contain settings only. DSP instances and audio buffers belong to the runner.
namespace samplefx
{
constexpr unsigned kMaxStages = 8;
constexpr unsigned kMaxParams = 12;
constexpr uint8_t kPlugin = 255;
constexpr uint8_t kSample = 254;

struct Node
{
    uint8_t effect = kSample;
    uint8_t count = 0;
    bool bypass = false;
    uint16_t flags = 0;
    uint32_t fingerprint = 0;
    char filename[42] = {};
    float params[kMaxParams] = {};
};

struct Recipe
{
    Node source;
    Node stages[kMaxStages];
    uint32_t generatorFrames = 44100;
    uint8_t count = 0;

    bool add(const Node& node);
    bool remove(unsigned index);
    bool move(unsigned index, int direction);
    bool duplicate(unsigned index);
};

constexpr size_t kRecipeBytes = 16 + (kMaxStages + 1) * 100;
bool encode(const Recipe& recipe, uint8_t* bytes, size_t size);
bool decode(const uint8_t* bytes, size_t size, Recipe& recipe);
uint32_t fingerprint(const void* bytes, size_t size, uint32_t hash = 2166136261u);

struct Audio
{
    int16_t* channel[2] = {};
    uint32_t frames = 0;
    uint8_t channels = 1;
};

// Backends process a complete stage in bounded steps. They must validate output
// capacity before writing and reset all stage state on begin().
class Backend
{
  public:
    virtual ~Backend() = default;
    virtual bool begin(const Node&, const Audio&, Audio&, uint32_t capacity,
                       uint32_t generatorFrames) = 0;
    virtual bool step(bool& complete) = 0;
    virtual const char* error() const = 0;
};

class Runner
{
  public:
    enum class State { Dirty, Preparing, Ready, Failed };
    Runner(Backend& backend) : backend(backend) {}
    bool start(const Recipe&, const Audio& source, int16_t* work[2][2], uint32_t capacity);
    void update();
    void invalidate();
    void cancel() { invalidate(); }
    State state() const { return status; }
    const Audio& result() const { return input; }
    unsigned stage() const { return next; } // 0 = source, 1..8 = effects
    const char* error() const { return failure; }

  private:
    Backend& backend;
    Recipe recipe;
    Audio input, output;
    int16_t* buffers[2][2] = {};
    uint32_t capacity = 0;
    unsigned next = 0;
    unsigned destination = 1;
    bool processing = false;
    State status = State::Dirty;
    const char* failure = "";
    void fail(const char* message);
};
}
