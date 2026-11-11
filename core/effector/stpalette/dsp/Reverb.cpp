// Clouds tank derived from Emilie Gillet's MIT-licensed Clouds DSP.
#include "Helpers.h"
#include "../vendor/clouds/clouds/dsp/frame.h"
#include "../vendor/clouds/clouds/dsp/fx/reverb.h"
#include <new>

namespace samplefx { namespace stpalette {
namespace {
template<unsigned Division> struct Tank {
    clouds::Reverb<Division> verb;
    uint16_t memory[16384];
    clouds::FloatFrame frames[16];
};
template<unsigned D> void* makeTank(Memory mem,float sr) {
    void* p=mem.get(sizeof(Tank<D>)); if(!p)return nullptr;
    auto* tank=new(p) Tank<D>{}; tank->verb.Init(tank->memory,sr);
    tank->verb.set_input_gain(.2f); tank->verb.set_amount(1);
    return tank;
}
template<unsigned D> void reverb(slot_dsp_t* s,float* l,float* r,int n,float amount,float macro,float drift,bool bloom) {
    auto& t=*static_cast<Tank<D>*>(s->heavy);
    t.verb.set_time((bloom?.30f:.25f)+macro*(bloom?.65f:.70f));
    t.verb.set_diffusion(bloom?.7f:clampf(.5f+macro*.3f+drift*.1f,0.f,.9f));
    t.verb.set_lp(rateCoeff(clampf((bloom?.55f:.7f)-drift*.2f,.2f,.95f),s->sr));
    for(int i=0;i<n;++i) {t.frames[i].l=l[i]; t.frames[i].r=s->channels==2?r[i]:l[i];}
    t.verb.Process(t.frames,unsigned(n));
    for(int i=0;i<n;++i) {
        float wl=t.frames[i].l*.7f+t.frames[i].r*.3f;
        float wr=t.frames[i].r*.7f+t.frames[i].l*.3f;
        if(s->channels==1) wl=(wl+wr)*.5f;
        if(bloom) {
            float win=std::max(64.f,s->sr*.09f);
            s->f1+=1; if(s->f1>=win)s->f1-=win;
            float p2=s->f1+win*.5f;if(p2>=win)p2-=win;
            float w=.5f-.5f*fastCos(TWO_PI*s->f1/win);
            float a=dlr(s->dl_l,s->wp,win-s->f1)*w+dlr(s->dl_l,s->wp,win-p2)*(1-w);
            float b=s->channels==2?dlr(s->dl_r,s->wp,win-s->f1)*w+dlr(s->dl_r,s->wp,win-p2)*(1-w):a;
            s->dl_l[s->wp]=sb_tanh(wl+a*amount*.85f);
            if(s->channels==2)s->dl_r[s->wp]=sb_tanh(wr+b*amount*.85f);
            if(++s->wp==s->max_delay)s->wp=0;
            wl+=a*amount*.6f; wr+=b*amount*.6f;
        }
        l[i]=lerpf(l[i],wl,amount);
        if(s->channels==2)r[i]=lerpf(r[i],wr,amount);
    }
}
void dispatch(slot_dsp_t* s,float* l,float* r,int n,float a,float m,float d,bool bloom) {
    if(s->quality==0)reverb<1>(s,l,r,n,a,m,d,bloom);
    else if(s->quality==1)reverb<2>(s,l,r,n,a,m,d,bloom);
    else reverb<4>(s,l,r,n,a,m,d,bloom);
}
}
size_t tankBytes(unsigned d) { return d==1?sizeof(Tank<1>):d==2?sizeof(Tank<2>):sizeof(Tank<4>); }
void* prepareTank(Memory mem,unsigned d,float sr) { return d==1?makeTank<1>(mem,sr):d==2?makeTank<2>(mem,sr):makeTank<4>(mem,sr); }
void fx_space(slot_dsp_t* s,float* l,float* r,int n,float a,float m,float d) { dispatch(s,l,r,n,a,m,d,false); }
void fx_bloom(slot_dsp_t* s,float* l,float* r,int n,float a,float m,float d) { dispatch(s,l,r,n,a,m,d,true); }
} }
