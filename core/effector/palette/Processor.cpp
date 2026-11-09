#include "Palette.h"
#include "dsp/Helpers.h"
#include <new>
#include <cstring>

namespace samplefx { namespace palette {
namespace {
unsigned algorithm(const Node& n) { return (n.effect-kFirst)*6+unsigned(n.params[Type]); }
unsigned factor(const Node& n) {
    unsigned a=algorithm(n);
    return a==3 || a==5 || a==8 || a==9 || a==18 || a==19 || a==23 ? 1 : 1u<<processingQuality(n);
}
unsigned history(const Node& n, unsigned rate) {
    unsigned a=algorithm(n);
    if(a==23) return 6144;
    if(a==17) return 8192;
    if(a==6) return hasSync(n) && n.params[Sync] ? rate*2+8 : (rate*9/100)+8;
    if(a==7) return rate/50+8;
    if(a==10) return rate/20+8;
    if(a==20 || a==21) return rate/20+8;
    if(a>=12 && a<=15) return rate*2+8;
    return 0;
}
// 15-tap half-band FIR: symmetric coefficients and zero-valued alternate taps.
// Four cascaded paths provide stateful 2:1 or 4:1 conversion and a known delay.
struct FIR {
    float z[15]={}; unsigned at=0;
    float tap(unsigned distance) const { int p=int(at)-int(distance);return z[p<0?p+15:p]; }
    float push(float x) {
        z[at]=x;
        // Fold symmetric pairs and omit the half-band filter's zero taps.
        float sum=-.00365145f*(tap(0)+tap(14))+.01617925f*(tap(2)+tap(12))
                 -.06841178f*(tap(4)+tap(10))+.3058840f*(tap(6)+tap(8))+.5f*tap(7);
        at=at==14?0:at+1; return sum;
    }
};
}
// Large working arrays live in the editor arena, not Original's 4 KiB stack.
struct Processor::State {
    slot_dsp_t dsp{};
    Node node;
    Kernel kernel=nullptr;
    unsigned channels=1, factor=1, position=0, inputFrames=0, outputFrames=0;
    float amount=0, macro=.5f, drift=0, smooth=0;
    FIR down[2][2], up[2][2];
    float delay[2][64]={};
    float lo[2][16]={}, original[2][16]={};
    float lastGain=1;
    float powerStep=0, gateRelease=0, gateEnvelope=0;
};
unsigned latencyFrames(const Node& n) { if(!valid(n))return 0;unsigned d=factor(n);return d==4?42:d==2?14:0; }
size_t requiredMemory(const Node& n,unsigned channels) {
    if(!valid(n) || channels<1 || channels>2) return 0;
    return sizeof(Processor) + 4096 + history(n,44100/factor(n))*channels*2 +
        (algorithm(n)==16 || algorithm(n)==17 ? 34000+512 : 0);
}
// Opaque Clouds tank allocation and lifecycle are entirely arena based.
size_t tankBytes(unsigned division);
void* prepareTank(Memory,unsigned division,float rate);

bool Processor::prepare(const Node& n,unsigned channels,uint32_t frames,Memory mem,unsigned stage,float tempo) {
    reset();
    if(!valid(n) || channels<1 || channels>2 || !frames) return false;
    void* bytes=mem.get(sizeof(State)); if(!bytes) return false;
    static_assert(sizeof(State)<4096,"Native state exceeds memory admission allowance");
    state=new(bytes) State{};
    State& s=*state; s.node=n; s.channels=channels; s.factor=factor(n);
    s.inputFrames=frames; s.position=0;
    s.dsp.channels=channels; s.dsp.sr=44100.f/s.factor; s.dsp.quality=processingQuality(n);
    s.dsp.seed=seed(n,stage); s.dsp.max_delay=history(n,unsigned(s.dsp.sr));
    if(s.dsp.max_delay) {
        for(unsigned c=0;c<channels;++c) {
            auto* p=static_cast<int16_t*>(mem.get(s.dsp.max_delay*2)); if(!p){reset();return false;}
            (c?s.dsp.dl_r:s.dsp.dl_l)={p,s.dsp.max_delay};
        }
        if(channels==1) s.dsp.dl_r=s.dsp.dl_l;
    }
    unsigned a=algorithm(n);
    if(a==16 || a==17) {
        s.dsp.heavy=prepareTank(mem,s.factor,s.dsp.sr);
        if(!s.dsp.heavy){reset();return false;}
    }
    s.kernel=n.effect==32?character(unsigned(n.params[Type])):n.effect==33?movement(unsigned(n.params[Type])):n.effect==34?diffusion(unsigned(n.params[Type])):texture(unsigned(n.params[Type]));
    s.amount=n.params[Amount]*.01f; s.macro=n.params[Macro]*.01f; s.drift=n.params[Drift]*.01f;
    s.smooth=1-std::exp(-16.f/(44100*.015f));
    s.powerStep=1-std::exp(-16.f/(44100*.15f)); s.gateRelease=1-s.powerStep;
    parameters(n,tempo);
    return true;
}
bool Processor::parameters(const Node& n,float tempo) {
    if(!state || !valid(n) || n.effect!=state->node.effect || n.params[Type]!=state->node.params[Type] || n.quality!=state->node.quality) return false;
    state->node=n;
    float beats=hasSync(n)?syncBeats(unsigned(n.params[Sync])):0;
    state->dsp.sync_time=beats>0?std::min(state->dsp.sr*2-4,state->dsp.sr*60*beats/clampf(tempo,10.f,800.f)):0;
    state->dsp.sync_rate=beats>0?clampf(tempo,10.f,800.f)/(60*beats):0;
    return true;
}
uint32_t Processor::tail() const { return state ? (hasTail(state->node) ? uint32_t(state->node.params[Tail]*44100+.5f) : 0)+latencyFrames(state->node) : 0; }
bool Processor::process(int16_t* const* input,int16_t* const* output,unsigned frames) {
    if(!state || frames>128 || !output || !output[0] || (state->channels==2 && !output[1])) return false;
    State& s=*state;
    const unsigned latency=s.factor==4?42:s.factor==2?14:0;
    const unsigned phaseMask=s.factor-1;
    const unsigned end=s.inputFrames+tail();
    const bool fadeTail=hasTail(s.node) && s.node.params[Tail]>0;
    for(unsigned off=0;off<frames;off+=16) {
        unsigned n=std::min(16u,frames-off), low=0;
        for(unsigned i=0;i<n;++i) {
            unsigned at=s.position+off+i;
            for(unsigned c=0;c<s.channels;++c) {
                float x=at<s.inputFrames && input && input[c] ? input[c][off+i]*(1.f/32768) : 0;
                s.delay[c][at&63]=x;
                if(s.factor>1) x=s.down[c][0].push(x);
                if(s.factor==4 && !(at&1)) x=s.down[c][1].push(x);
                if((at&phaseMask)==0) s.lo[c][low]=s.original[c][low]=x;
            }
            if((at&phaseMask)==0) ++low;
        }
        if(s.channels==1) { std::memcpy(s.lo[1],s.lo[0],low*4); std::memcpy(s.original[1],s.original[0],low*4); }
        s.amount+=s.smooth*(s.node.params[Amount]*.01f-s.amount);
        s.macro+=s.smooth*(s.node.params[Macro]*.01f-s.macro);
        s.drift+=s.smooth*(s.node.params[Drift]*.01f-s.drift);
        if(low) s.kernel(&s.dsp,s.lo[0],s.lo[1],low,s.amount,s.macro,s.drift);
        bool character=s.node.effect==32 && unsigned(s.node.params[Type])<5;
        if(low && (character || (s.node.effect==35 && s.node.params[Type]==1))) {
            float dryPower=0,wetPower=0,peak=0;
            for(unsigned c=0;c<s.channels;++c) for(unsigned i=0;i<low;++i) {
                float x=s.original[c][i],y=s.lo[c][i];
                dryPower+=x*x;wetPower+=y*y;peak=std::max(peak,std::fabs(x));
            }
            float inv=1.f/(low*s.channels);
            s.dsp.lvlDry+=s.powerStep*(dryPower*inv-s.dsp.lvlDry);
            s.dsp.lvlWet+=s.powerStep*(wetPower*inv-s.dsp.lvlWet);
            float gain=1;
            if(character && s.dsp.lvlDry>1e-12f && s.dsp.lvlWet>1e-12f) {
                float ratio=clampf(s.dsp.lvlDry/s.dsp.lvlWet,1e-8f,1e8f);
                float quarter=std::sqrt(std::sqrt(ratio));
                gain=clampf(quarter*std::sqrt(quarter),.05f,4.f); // ratio^(3/8)
            }
            s.gateEnvelope=std::max(peak,s.gateEnvelope*s.gateRelease);
            float gate=clampf((s.gateEnvelope-.00025119f)/(.001f-.00025119f),0.f,1.f);
            gate=gate*gate*(3-2*gate);
            float strength=std::min(s.amount*10,1.f);
            for(unsigned i=0;i<low;++i) {
                s.lastGain+=.002f*(gain-s.lastGain);
                for(unsigned c=0;c<s.channels;++c) {
                    float wet=s.lo[c][i]*(1+(s.lastGain-1)*strength);
                    s.lo[c][i]=lerpf(s.original[c][i],wet,gate);
                }
            }
        }
        unsigned read=0;
        for(unsigned i=0;i<n;++i) {
            unsigned at=s.position+off+i;
            for(unsigned c=0;c<s.channels;++c) {
                float y=0;
                if((at&phaseMask)==0) y=s.lo[c][read]-s.original[c][read];
                if(s.factor==4 && !(at&1)) y=s.up[c][1].push(y*2);
                if(s.factor>1) y=s.up[c][0].push((at&1)?0:y*2);
                y+=at>=latency?s.delay[c][(at-latency)&63]:0;
                if(!std::isfinite(y) || std::fabs(y)>32) return false;
                // Bounded tail, including regenerative/self-oscillating processors.
                if(fadeTail && at>=s.inputFrames+latency && end>at && end-at<=441)
                    y*=float(end-at-1)/441;
                output[c][off+i]=int16_t(clampf(y*32768,-32768.f,32767.f));
            }
            if((at&phaseMask)==0) ++read;
        }
    }
    s.position+=frames;
    return true;
}
} }
