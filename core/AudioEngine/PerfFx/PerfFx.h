#pragma once

#include <atomic>
#include <cstdint>

namespace perffx
{
constexpr unsigned kColumns = 12;
constexpr unsigned kLevels = 4;
constexpr unsigned kLanes = 3;
constexpr unsigned kHistoryFrames = 176400;
constexpr unsigned kDelayFrames = 4096;
constexpr unsigned kMemoryBytes = 752 * 1024;

enum class Effect : uint8_t
{
    // IDs 4, 8, 10 and 18 are reserved for retired effects.
    None = 0, Loop = 1, TripletLoop = 2, Unison = 3, PitchUp = 5, PitchDown = 6,
    Stutter = 7, Scratch = 9, Scatter = 11, Reverse = 12, TapeStop = 13, LowPass = 14,
    BeatRepeat = 15, Pitch = 16, DubEcho = 17, Crush = 19, HighPass = 20
};

struct Column
{
    Effect effect;
    const char* name;
    const char* labels[kLevels];
};

extern const Column columns[kColumns];

enum class Source : uint8_t { Pads = 1, Sequencer };
using Owner = uint16_t;
constexpr Owner owner(Source source, uint8_t key)
{
    return (static_cast<Owner>(source) << 8) | key;
}

struct Storage
{
    int16_t history[kHistoryFrames][2];
    int16_t delay[kLanes][kDelayFrames][2];
};
static_assert(sizeof(Storage) <= kMemoryBytes);

class Rack
{
  public:
    explicit Rack(Storage& storage) : memory(storage) {}
    void press(Owner owner, Effect effect, uint8_t level);
    void release(Owner owner);
    void releaseSource(Source source);
    void reset();
    int soundingLevel(unsigned column) const;

    // Audio thread. Call beginBlock before deciding whether writable blocks are needed.
    void beginBlock(float tempo, float sampleRate, uint8_t mix, bool capture);
    bool wet() const;
    void process(const int16_t* left, const int16_t* right,
                 int16_t* outLeft, int16_t* outRight, unsigned frames);

  private:
    enum class Operation : uint8_t { Press, Release, ReleaseSource };
    struct Command
    {
        Owner owner;
        Effect effect;
        uint8_t level;
        Operation operation;
    };
    struct Held
    {
        Owner owner = 0;
        Effect effect = Effect::None;
        uint8_t level = 0;
        uint32_t order = 0;
    };
    struct Lane
    {
        Effect effect = Effect::None;
        uint8_t level = 0;
        uint32_t order = 0;
        bool attack = false;
        float gate = 0;
        unsigned segment = 0, length = 0;
        unsigned write = 0, valid = 0;
        float position = 0, headB = 0, phase = 0, step = 0;
        float phaseStep = 0, depth = 0, edgeScale = 0, duty = 0;
        float direction = 1, timer = 0, flipPeriod = 0;
        float filterState[2][2] = {};
        unsigned chunkFrames = 0, chunk = 0, chunkOffset = 0;
        unsigned echoFrames = 0;
        unsigned crushPeriod = 0, crushRemaining = 0;
        uint16_t crushMask = 0;
        float heldSamples[2] = {};
    };

    void enqueue(Command command);
    void consume();
    void admit();
    void activate(Lane& lane, const Held& held, uint32_t order);
    void processLane(Lane& lane, unsigned index, float& left, float& right);
    float readHistory(const Lane& lane, unsigned channel) const;
    float readDelay(const Lane& lane, unsigned index, float position, unsigned channel) const;

    static constexpr unsigned kQueue = 128;
    Storage& memory;
    Command commands[kQueue];
    std::atomic<uint8_t> head{0}, tail{0};
    std::atomic_flag producer = ATOMIC_FLAG_INIT;
    std::atomic<bool> resetRequested{false};
    Held held[64];
    uint32_t columnOrder[kColumns] = {};
    uint32_t serial = 0;
    Lane lanes[kLanes];
    uint8_t renderOrder[kLanes] = {};
    unsigned laneCount = 0;
    bool hasHeld = false;
    std::atomic<uint8_t> sounding[kColumns]{};
    unsigned historyWrite = 0, historyValid = 0;
    unsigned beatFrames = 22050;
    float rate = 44100, attackStep = 0, releaseStep = 0;
    float mix = 0, mixTarget = 0, mixStep = 0;
    unsigned mixRemaining = 0;
    bool capture = false;
};
}
