#include "STPaletteEffects.h"
#include <algorithm>
#include <cmath>

namespace samplefx { namespace stpalette {
namespace {
const char* const categories[] = {"Character", "Movement", "Diffusion", "Texture"};
const char* const types[4][6] = {
    {"Drive", "Sweeten", "Fuzz", "Howl", "Fold", "Swell"},
    {"Doubler", "Vibrato", "Phaser", "Tremolo", "Pitch", "Shift"},
    {"Cascade", "Reels", "Collage", "Reverse Delay", "Space", "Bloom"},
    {"Filter", "Squash", "Cassette", "Broken", "Interference", "Halo"}
};
const char* const divisions[] = {"Free", "1/1", "1/2", "1/2T", "1/2D", "1/4", "1/4T", "1/4D", "1/8", "1/8T", "1/8D", "1/16", "1/16T", "1/16D", "1/32"};
constexpr float beats[] = {0, 4, 2, 4.f/3, 3, 1, 2.f/3, 1.5f, .5f, 1.f/3, .75f, .25f, 1.f/6, .375f, .125f};
// Provisional worst-case microseconds on MK66/180 MHz, including adapters.
constexpr unsigned costs[24] = {150,100,120,70,150,25, 55,70,130,40,90,125, 80,90,110,90,155,250, 45,100,85,70,90,160};
// Resampler overhead can outweigh savings. Host profiling rejects known losing
// profiles; these capabilities still require MK66 cycle calibration.
constexpr uint8_t profiles[24]={3,1,3,1,1,1, 1,3,7,1,1,3, 1,3,1,1,3,3, 1,7,3,1,3,7};
}
bool contains(unsigned id) { return id >= kFirst && id <= kLast; }
const char* category(unsigned id) { return contains(id) ? categories[id-kFirst] : "Unavailable"; }
const char* typeName(unsigned id, unsigned type) { return contains(id) && type < 6 ? types[id-kFirst][type] : "Unavailable"; }
unsigned syncCount() { return 15; }
const char* syncName(unsigned d) { return d < syncCount() ? divisions[d] : "Free"; }
float syncBeats(unsigned d) { return d < syncCount() ? beats[d] : 0; }
const char* label(unsigned p) { static const char* const names[] = {"Type","Amount","Macro","Drift","Sync","Tail"}; return p < ParameterCount ? names[p] : ""; }
float maximum(unsigned p) { return p == Type ? 5 : p == Sync ? 14 : p == Tail ? 10 : 100; }
float step(unsigned p) { return p == Tail ? .1f : 1; }
Node defaults(unsigned id) {
    Node n; n.effect = id; n.count = ParameterCount;
    n.params[Amount] = id == kLast ? 50 : 0; n.params[Macro] = 50; n.params[Tail] = 2;
    return n;
}
bool valid(const Node& n) {
    if (!contains(n.effect) || n.count != ParameterCount || n.flags || n.quality > Minimum) return false;
    for (unsigned p=0; p<ParameterCount; ++p)
        if (!std::isfinite(n.params[p]) || n.params[p] < 0 || n.params[p] > maximum(p)) return false;
    return n.params[Type] == std::floor(n.params[Type]) && n.params[Sync] == std::floor(n.params[Sync]);
}
bool hasSync(const Node& n) {
    unsigned a = (n.effect-kFirst)*6 + unsigned(n.params[Type]);
    return a == 6 || a == 7 || a == 8 || a == 9 || (a >= 12 && a <= 15);
}
bool hasTail(const Node& n) {
    unsigned a = (n.effect-kFirst)*6 + unsigned(n.params[Type]);
    return a == 6 || a == 7 || a == 10 || (a >= 12 && a <= 17) || a == 20 || a == 21 || a == 23;
}
bool visible(const Node& n, unsigned p) { return p < ParameterCount && (p != Sync || hasSync(n)) && (p != Tail || hasTail(n)); }
uint32_t seed(const Node& n, unsigned stage) { return 0x9e3779b9u ^ (n.effect*0x85ebca6bu) ^ (unsigned(n.params[Type])*0xc2b2ae35u) ^ (stage*0x6d2b79f5u); }
unsigned processingQuality(const Node& n) {
    unsigned a=(n.effect-kFirst)*6+unsigned(n.params[Type]);
    if(a>=24)return Full;
    unsigned q=std::min<unsigned>(n.quality,Minimum);
    while(q && !(profiles[a]&(1u<<q))) --q;
    return q;
}
bool lowerQuality(Node& n) {
    if(!valid(n))return false;
    unsigned a=(n.effect-kFirst)*6+unsigned(n.params[Type]);
    for(unsigned q=n.quality+1;q<=Minimum;++q) if(profiles[a]&(1u<<q)){n.quality=q;return true;}
    return false;
}
unsigned estimatedUs(const Node& n, unsigned channels) {
    if (!valid(n) || n.bypass) return 0;
    unsigned a=(n.effect-kFirst)*6 + unsigned(n.params[Type]);
    unsigned cost=costs[a]*channels*5/4;
    if(a==3 || a==5 || a==9 || a==18) return cost+12;
    unsigned q=processingQuality(n);
    if(a==8) return cost*(q==0?16:q==1?12:8)/16+12;
    if(a==23) return cost*(q==0?8:q==1?6:5)/8+12;
    if(a==19) return cost*(q==0?100:q==1?65:55)/100+12;
    return cost/(1u<<q)+12;
}
bool resolve(ChainDefinition& r, unsigned channels, size_t bytes, unsigned budgetUs) {
    if (channels < 1 || channels > 2 || r.count > kMaxStages) return false;
    for (unsigned i=0;i<r.count;++i) if (contains(r.stages[i].effect)) {
        if (!valid(r.stages[i])) return false;
        if (!r.qualityResolved) r.stages[i].quality=Full;
    }
    for (;;) {
        size_t memory=0; unsigned cost=35;
        for (unsigned i=0;i<r.count;++i) {
            const Node& n=r.stages[i]; if (n.bypass) continue;
            if (contains(n.effect)) { memory += requiredMemory(n,channels); cost += estimatedUs(n,channels); }
            else cost += 55; // Existing effects are also charged, never silently bypassed.
        }
        if (memory <= bytes && cost <= budgetUs) { r.qualityResolved=true; return true; }
        unsigned best=kMaxStages, saving=0;
        for (unsigned i=0;i<r.count;++i) {
            const Node& n=r.stages[i]; if (!contains(n.effect) || n.bypass || n.quality == Minimum) continue;
            Node lower=n; if(!lowerQuality(lower))continue;
            unsigned gain=estimatedUs(n,channels)-estimatedUs(lower,channels);
            if (memory > bytes) gain += unsigned((requiredMemory(n,channels)-requiredMemory(lower,channels))/1024);
            if (gain > saving) { saving=gain; best=i; }
        }
        if (best==kMaxStages) return false;
        lowerQuality(r.stages[best]);
    }
}
} }
