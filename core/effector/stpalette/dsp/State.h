#pragma once
#include "../StPalette.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace samplefx { namespace stpalette {
struct History {
    int16_t* data=nullptr; int length=0;
    struct Ref {
        int16_t* p;
        operator float() const { return *p * (4.0f/32768); }
        Ref& operator=(float x) { *p=int16_t(std::clamp(x*8192, -32768.f,32767.f)); return *this; }
    };
    Ref operator[](int i) { return {data+i}; }
    float operator[](int i) const { return data[i]*(4.0f/32768); }
};
struct slot_dsp_t {
    History dl_l, dl_r;
    int wp=0, channels=1, max_delay=0, quality=0;
    float sr=44100;
    unsigned control=0;
    float coefficients[4]={};
    void* heavy=nullptr;
    /* generic one-pole / cascaded filter states (stereo) */
    float  z1l,z1r, z2l,z2r, z3l,z3r, z4l,z4r;
    /* state-variable filter (Filter / Howl) stereo: lp & bp integrators */
    float  lp_l,bp_l, lp_r,bp_r;
    /* biquad (peaking/shelf) stereo, direct-form I */
    float  bx1l,bx2l,by1l,by2l, bx1r,bx2r,by1r,by2r;
    /* envelope followers */
    float  env, env2;
    /* parameter smoothing companions */
    float  sm1,sm2,sm3,sm4;
    /* LFO phases (0..1) */
    float  lfo,lfo2,lfo3;
    /* allpass chains: Phaser (≤12 stages, 1 state each) stereo */
    float  ap_l[16], ap_r[16], ap2_l[16], ap2_r[16];
    /* SHIFT: Warps QuadratureTransform Hilbert — 17 allpass filters × 2 states,
     * per channel: hil[ch*34 + filt*2 + (0=x,1=y)] */
    float  hil[68];
    /* misc per-effect scratch */
    float  f1,f2,f3,f4,f5,f6;
    int    i1,i2,i3,i4;
    /* HALO resonator bank: 6 voices × per-channel damping one-pole */
    float  halo_lp_l[6], halo_lp_r[6];
    uint32_t seed;            /* per-slot deterministic RNG (drift) */
    /* tempo sync (set per block by the host): >0 = use instead of Macro mapping */
    float  sync_time;         /* synced delay time in samples (0 = free) */
    float  sync_rate;         /* synced LFO rate in Hz (0 = free) */
    float  lvlDry, lvlWet, lvlGain;   /* Character-group level match (see process_block) */
    float  lvlW;                      /* how much of it applies (fades with Amount) */
};
using Kernel = void(*)(slot_dsp_t*,float*,float*,int,float,float,float);
Kernel character(unsigned);
Kernel movement(unsigned);
Kernel diffusion(unsigned);
Kernel texture(unsigned);
} }
