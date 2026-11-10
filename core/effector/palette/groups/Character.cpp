// Adapted from PALETTE, Copyright 2026 Filliformes, MIT (see ../LICENSE).
// Derived Airwindows/Mutable DSP attribution is recorded in ../README.md.
#include "../dsp/Helpers.h"
namespace samplefx { namespace palette {
#define SR (s->sr)
#define MAX_DELAY (s->max_delay)
#define sinf fastSin
#define cosf fastCos
#define tanhf fastTanh
/* DRIVE — tube-ish overdrive that KEEPS the low end. The bass band (<~200 Hz) is
 * split off and passed clean; only the harmonic band is driven, through an
 * asymmetric (tube-ish, even-harmonic) tanh with ADAA, DC-blocked after.
 * (Was the Airwindows Spiral sin(x|x|)/|x|: at the gains Amount reaches, x|x| runs
 * to ~80 rad, so it wavefolded dozens of times per cycle - more alias than signal.)
 * The operating point is subtracted, so silence in = silence out even with bias.
 * macro = pre-emphasis tilt (brightens INTO the shaper, never high-passes the
 * output), drift = slow bias wander. State: z1=bass one-pole, z2/z3=DC-block x/y,
 * sm1/sm2=ADAA history L/R, f2=per-sample drive glide. */
PALETTE_HOT_CODE static void fx_drive(slot_dsp_t *s, float *l, float *r, int n,
                     float amount, float macro, float drift){
    float drv  = 1.0f + amount*6.0f + amount*amount*30.0f;
    float tilt = (macro-0.5f)*2.0f;                  /* −1..+1 brightness tilt */
    const float aBass=rateCoeff(.030f,SR), glide=rateCoeff(.01f,SR);                        /* ~200 Hz low/high split */
    const float asym=0.20f;                          /* tube-ish operating point */
    float himk = (0.7f + amount*0.5f)*0.8f;          /* harmonic-band makeup */
    if(s->f2<=0.0f) s->f2=drv;
    for(int i=0;i<n;i++){
        s->f2 += glide*(drv - s->f2);                /* glide: no gain steps */
        float bias = drift>0.0f ? wander(&s->f1,&s->seed,0.0006f)*drift*0.10f : 0.0f;
        float c = asym + bias*4.0f;                  /* operating point (not scaled by drive) */
        float tc = tanhf(c);
        for(int ch=0;ch<s->channels;ch++){
            float x=(ch?r:l)[i];
            float *lp=ch?&s->z1r:&s->z1l;
            *lp += aBass*(x-*lp)+DENORM;
            float bass=*lp, high=x-*lp;              /* clean sub + harmonic band */
            float pre=high*(1.0f+0.6f*tilt);         /* tilt = pre-emphasis, not HPF */
            float sh=tanh_adaa(pre*s->f2 + c, ch?&s->sm2:&s->sm1) - tc;
            float *dx=ch?&s->z2r:&s->z2l, *dy=ch?&s->z3r:&s->z3l;
            float y=sh - *dx + 0.9993f*(*dy); *dx=sh; *dy=y+DENORM;   /* DC block (asym) */
            float wet=bass + y*himk;                 /* bass FULL → low end intact */
            (ch?r:l)[i]=lerpf(x, wet, amount);       /* amount=0 → dry */
        }
    }
}

/* SWEETEN — console preamp. Gentle comp + the Airwindows Density sin-fold
 * saturator (Chris Johnson, MIT) for body, + an Air-style HF tilt shelf.
 * amount=comp/sat, macro=tone (dark↔air), drift=subtle level drift. */
PALETTE_HOT_CODE static void fx_sweeten(slot_dsp_t *s, float *l, float *r, int n,
                       float amount, float macro, float drift){
    float tilt=(macro-0.5f)*2.0f;
    float thr=0.5f, ratio=0.35f+amount*0.4f;         /* gentle comp */
    float density=amount*3.0f, dwet=clampf(density,0.0f,1.0f);  /* Density sat amount */
    float comp=1.0f/(1.0f+density*0.5f);             /* compensate sin-fold gain */
    for(int i=0;i<n;i++){
        float mono=0.5f*(fabsf(l[i])+fabsf(r[i]));
        s->env += (mono>s->env?0.02f:0.004f)*(mono-s->env)+DENORM;  /* fast atk slow rel */
        float gr=1.0f; if(s->env>thr) gr=1.0f-(1.0f-thr/s->env)*ratio;
        float dl=(drift>0.0f)? 1.0f+wander(&s->f1,&s->seed,0.0004f)*drift*0.03f : 1.0f;
        float mdrive=1.0f+amount*2.2f;                /* density drive */
        float mcomp=1.0f/(0.55f+mdrive*0.45f);
        for(int ch=0;ch<s->channels;ch++){
            float dry=(ch?r:l)[i];
            float x=dry*gr*dl;
            /* Airwindows Mojo density fold (Chris Johnson, MIT): pow(|x|,0.25)
             * thickens low-level detail before a very soft sin-fold → perceived
             * density/RMS ("louder without clipping"), the console-glue voice. */
            float xx=x*mdrive;
            float sat=sweetenShape(xx)*mcomp;
            (void)dwet;(void)comp;(void)density;
            /* Air-style HF tilt shelf (macro) */
            float *z=ch?&s->z1r:&s->z1l;
            *z += 0.10f*(sat-*z)+DENORM;              /* LP component */
            float wet=(tilt>=0.0f)? sat+(sat-*z)*tilt*0.8f      /* brighten = air */
                                  : lerpf(sat,*z,-tilt);        /* darken */
            (ch?r:l)[i]=lerpf(dry,wet,amount);        /* amount=0 → dry */
        }
    }
}

/* FUZZ — Big-Muff-style TWO cascaded soft-clip stages. The cascade gives the
 * compressed, sustaining, "singing" voice at MODERATE output (not just loud):
 * input band-limited ~1.2 kHz → starved bias → two soft clips in series →
 * DC-block → post tone. amount=sustain/gain, macro=tone/bias, drift=bias drift.
 * State: z3=input LP, z4/bp=DC-block x/y, z2=tone LP. */
PALETTE_HOT_CODE static void fx_fuzz(slot_dsp_t *s, float *l, float *r, int n,
                    float amount, float macro, float drift){
    float sustain=0.5f+amount*amount*2.6f;           /* pre-gain into the cascade */
    float bias=(macro-0.5f)*0.4f;                    /* starved bias → asymmetry */
    float tone=rateCoeff(.12f+macro*.5f,SR);                     /* post tone LP */
    const float aIn=rateCoeff(.157f,SR), glide=rateCoeff(.004f,SR);                          /* input LP ~1.2 kHz */
    float g=28.0f*sustain;
    for(int i=0;i<n;i++){
        /* bias drift: slow (~0.4 s) and modest. It was a ~11 ms random walk of +-0.3
         * through x87 gain - a loud rumble on its own (-6 dBFS out of SILENCE). */
        s->f2 += glide*(bias - s->f2);              /* bias glides (~6 ms): Tone turns don't jump */
        float b=s->f2 + (drift>0.0f? wander(&s->f1,&s->seed,0.00006f)*drift*0.12f:0.0f);
        float tb=tanhf(g*b);                         /* operating point, subtracted */
        for(int ch=0;ch<s->channels;ch++){
            float x=(ch?r:l)[i];
            float *ilp=ch?&s->z3r:&s->z3l;
            *ilp += aIn*(x-*ilp)+DENORM;
            float xi=*ilp + b;
            /* both stages ADAA (true tanh) — the near-square output aliased hard */
            float s1=-(tanh_adaa(g*xi, ch?&s->sm3:&s->sm1) - tb);          /* stage 1 (inverts) */
            float s2=-tanh_adaa(g*0.5f*s1, ch?&s->sm4:&s->sm2);            /* stage 2 → sustain */
            float *dcx=ch?&s->z4r:&s->z4l, *dcy=ch?&s->bp_r:&s->bp_l;
            float y=s2 - *dcx + 0.9995f*(*dcy);       /* DC blocker (bias offset) */
            *dcx=s2; *dcy=y;
            float *tz=ch?&s->z2r:&s->z2l;
            *tz += tone*(y-*tz)+DENORM;
            (ch?r:l)[i]=lerpf(x, *tz*0.6f, amount);   /* moderate output; amount=0 → dry */
        }
    }
}

/* HOWL — resonant filter-fuzz / synth stab. Cytomic/TPT state-variable filter
 * (Andrew Simper, topology-preserving — unconditionally stable at high freq/Q,
 * unlike the Chamberlin SVF). Bandpass voiced into soft clip. amount=intensity
 * (drive + resonance), macro=resonant freq, drift=res-freq wander.
 * State per ch: lp_*=ic1eq, bp_*=ic2eq. */
static void fx_howl(slot_dsp_t *s, float *l, float *r, int n,
                    float amount, float macro, float drift){
    float base=120.0f*powf(40.0f,macro);             /* resonant freq 120..4800 Hz (TILT) */
    float fz=base*(drift>0.0f?(1.0f+wander(&s->f1,&s->seed,0.0008f)*drift*0.5f):1.0f);
    float g=tanf(3.14159265f*clampf(fz,40.0f,9000.0f)/SR);
    float k=1.0f-amount*0.985f; if(k<0.015f)k=0.015f; /* damping → near self-oscillation */
    float a1=1.0f/(1.0f+g*(g+k)), a2=g*a1, a3=g*a2;
    float drive=1.0f+amount*amount*20.0f;            /* fuzz drive feeding the resonator */
    float voice=0.6f+amount*0.9f;
    for(int ch=0;ch<s->channels;ch++){
        float *ic1=ch?&s->lp_r:&s->lp_l, *ic2=ch?&s->bp_r:&s->bp_l;
        float *src=ch?r:l;
        for(int i=0;i<n;i++){
            /* Fuzz FRONT-END: harmonic-rich excitation so the high-Q filter sings,
             * blooms and stabs (manual: "resonant filter fuzz"), not a clean BP. */
            float fuzz=sb_apply_dist(src[i]*drive,2);
            float v3=fuzz-*ic2;
            float v1=a1*(*ic1)+a2*v3;
            float v2=*ic2+a2*(*ic1)+a3*v3;
            *ic1=2.0f*v1-*ic1+DENORM; *ic2=2.0f*v2-*ic2+DENORM;
            float bp=sb_tanh(v1*voice);              /* soft-clip resonance → self-limits, blooms */
            src[i]=lerpf(src[i],bp*0.9f,amount);     /* amount=0 → dry */
        }
    }
}

/* SWELL — auto volume-swell. Hysteresis gate + Zeno-pole envelopes from Airwindows
 * Swell (Chris Johnson, MIT): separate on/off thresholds (no chatter) and separate
 * attack/release "Zeno arrow" poles. amount=swell time, macro=sensitivity,
 * drift=threshold jitter. State: i1/i2=louder L/R, sm1/sm2=swell gain L/R. */
static void fx_swell(slot_dsp_t *s, float *l, float *r, int n,
                     float amount, float macro, float drift){
    float sens=0.02f+(1.0f-macro)*0.28f;             /* on-threshold */
    float speedOn =0.00015f+(1.0f-amount)*0.0028f;   /* longer swell = slower rise */
    float speedOff=0.0008f;                           /* gentle fade-out */
    for(int i=0;i<n;i++){
        float thOn=sens*(drift>0.0f?(1.0f+wander(&s->f1,&s->seed,0.003f)*drift*0.5f):1.0f);
        float thOff=thOn*0.5f;                        /* hysteresis: off < on */
        for(int ch=0;ch<s->channels;ch++){
            float x=(ch?r:l)[i]; float a=fabsf(x);
            int *louder=ch?&s->i2:&s->i1; float *sw=ch?&s->sm2:&s->sm1;
            if(a>thOn && !*louder) *louder=1;
            if(a<thOff && *louder) *louder=0;
            if(*louder) *sw = *sw*(1.0f-speedOn)+speedOn;   /* Zeno → 1 */
            else      { *sw = *sw*(1.0f-speedOff); if(*sw<1e-15f)*sw=0.0f; } /* Zeno → 0, denormal floor */
            (ch?r:l)[i]=lerpf(x,x*(*sw),amount); /* amount=0 → dry */
        }
    }
}

/* Authentic Mutable Warps tables (warps_data.c, MIT) — see SOURCES. */



/* FOLD — West-Coast wavefolder using the REAL Warps lut_bipolar_fold curve via
 * the ALGORITHM_FOLD interpolation. amount=fold drive, macro=offset/symmetry,
 * drift=fold-point wobble. */
/* The fold curve as a continuous function of the LUT index (linear interpolation,
 * clamped flat outside [1,4095] exactly as the original lookup did) and its running
 * integral, so FOLD can use ADAA. fold_cum[] is built once in create_instance. */
PALETTE_HOT_CODE static void fx_fold(slot_dsp_t* s,float* l,float* r,int n,float amount,float macro,float drift) {
    const float drive=.5f+amount*amount*4, offset=(macro-.5f)*1.6f;
    for(int i=0;i<n;++i) {
        float w=drift>0 ? wander(&s->f1,&s->seed,.0009f)*drift*.25f : 0;
        float center=2048+(offset+w)*(2048.f/2.295f);
        float operating=curveAt(foldCurve,4096,center);
        for(int ch=0;ch<s->channels;++ch) {
            float* out=ch?r:l; float x=out[i];
            float idx=center+x*drive*(2048.f/2.295f);
            float* prev=ch?&s->sm2:&s->sm1;
            if(!s->i1) *prev=idx;
            float y=adaa(foldCurve,foldIntegral,4096,idx,prev)-operating;
            float* dx=ch?&s->z2r:&s->z2l; float* dy=ch?&s->z3r:&s->z3l;
            float dc=y-*dx+.9993f**dy; *dx=y; *dy=dc;
            out[i]=lerpf(x, dc*(.55f/(.6f+.4f*drive)),amount);
        }
        s->i1=1;
    }
}

Kernel character(unsigned type) { static const Kernel kernels[]={fx_drive,fx_sweeten,fx_fuzz,fx_howl,fx_fold,fx_swell}; return type<6?kernels[type]:nullptr; }
} }
