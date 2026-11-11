#include "StPalette.h"
#include "dsp/Helpers.h"
#include <new>
#include <cstring>

namespace samplefx { namespace stpalette {
namespace {
unsigned algorithm(const Node& n) { return (n.effect-kFirst)*6+unsigned(n.params[Type]); }
unsigned factor(const Node& n) {
    unsigned a=algorithm(n);
    return a==3 || a==5 || a==8 || a==9 || a==18 || a==19 || a==23 ? 1 : 1u<<processingQuality(n);
}
unsigned profileLatency(unsigned factor) { return factor==4?42:factor==2?14:0; }
unsigned tailLength(const Node& n) { return hasTail(n) ? uint32_t(n.params[Tail]*44100+.5f) : 0; }
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
// Duplicate the ring to keep the 15-tap window contiguous across wrap.
struct FIR {
    float z[30]={}; unsigned at=0;
    const float* push(float x) {
        float* window = z + at;
        window[0] = window[15] = x;
        at = at == 14 ? 0 : at + 1;
        return window + 1;
    }
    static float filter(const float* w) {
        return -.00365145f*(w[14]+w[0])+.01617925f*(w[12]+w[2])
             -.06841178f*(w[10]+w[4])+.3058840f*(w[8]+w[6])+.5f*w[7];
    }
    float decimate(float x, bool emit) {
        const float* w = push(x);
        return emit ? filter(w) : 0.f;
    }
    float interpolate(float x, bool zeroPhase) {
        const float* w = push(zeroPhase ? 0.f : x*2);
        // The zero-stuffed phase has a contribution only from the center tap.
        return zeroPhase ? .5f*w[7] : filter(w);
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
unsigned latencyFrames(const Node& n) { return valid(n) ? profileLatency(factor(n)) : 0; }
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
uint32_t Processor::tail() const { return state ? tailLength(state->node)+profileLatency(state->factor) : 0; }
STPALETTE_HOT_CODE bool Processor::process(int16_t* const* input,int16_t* const* output,unsigned frames) {
    if(!state || frames>128 || !output || !output[0] || (state->channels==2 && !output[1])) return false;
    State& s=*state;
    const unsigned latency=profileLatency(s.factor);
    const unsigned phaseMask=s.factor-1;
    const unsigned tailStart=s.inputFrames+latency;
    const unsigned tailSamples=tailLength(s.node);
    const unsigned end=tailStart+tailSamples;
    const unsigned fadeStart=end-std::min(tailSamples,441u);
    const bool character=s.node.effect==32 && unsigned(s.node.params[Type])<5;
    const bool levelMatch=character || (s.node.effect==35 && s.node.params[Type]==1);
    for(unsigned off=0;off<frames;off+=16) {
        const unsigned n=std::min(16u,frames-off);
        const unsigned start=s.position+off;
        const unsigned available=start<s.inputFrames ? std::min(n,s.inputFrames-start) : 0;
        unsigned low=n;
        if (!latency) {
            for (unsigned c=0;c<s.channels;++c) {
                const unsigned count=input && input[c] ? available : 0;
                for (unsigned i=0;i<count;++i) s.lo[c][i]=s.original[c][i]=input[c][off+i]*(1.f/32768);
                for (unsigned i=count;i<n;++i) s.lo[c][i]=s.original[c][i]=0;
            }
        } else {
            low=0;
            for(unsigned i=0;i<n;++i) {
                unsigned at=start+i;
                const bool firstPhase=!(at&1), emit=!(at&phaseMask);
                for(unsigned c=0;c<s.channels;++c) {
                    float x=i<available && input && input[c] ? input[c][off+i]*(1.f/32768) : 0;
                    s.delay[c][at&63]=x;
                    x=s.down[c][0].decimate(x,firstPhase);
                    if(s.factor==4 && firstPhase) x=s.down[c][1].decimate(x,emit);
                    if(emit) s.lo[c][low]=s.original[c][low]=x;
                }
                if(emit) ++low;
            }
        }
        if(s.channels==1) std::memcpy(s.lo[1],s.lo[0],low*sizeof(float));
        s.amount+=s.smooth*(s.node.params[Amount]*.01f-s.amount);
        s.macro+=s.smooth*(s.node.params[Macro]*.01f-s.macro);
        s.drift+=s.smooth*(s.node.params[Drift]*.01f-s.drift);
        if(low) s.kernel(&s.dsp,s.lo[0],s.lo[1],low,s.amount,s.macro,s.drift);
        if(low && levelMatch) {
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
            unsigned at=start+i;
            const bool emit=!(at&phaseMask);
            for(unsigned c=0;c<s.channels;++c) {
                float y=0;
                if(emit) y=s.lo[c][read]-s.original[c][read];
                if (!latency) {
                    y+=s.original[c][i];
                } else {
                    if(s.factor==4 && !(at&1)) y=s.up[c][1].interpolate(y,at&2);
                    y=s.up[c][0].interpolate(y,at&1);
                    y+=at>=latency?s.delay[c][(at-latency)&63]:0;
                }
                if(!std::isfinite(y) || std::fabs(y)>32) return false;
                // Bounded tail, including regenerative/self-oscillating processors.
                if(at>=fadeStart && at<end)
                    y*=float(end-at-1)/441;
                output[c][off+i]=int16_t(clampf(y*32768,-32768.f,32767.f));
            }
            if(emit) ++read;
        }
    }
    s.position+=frames;
    return true;
}
} }
