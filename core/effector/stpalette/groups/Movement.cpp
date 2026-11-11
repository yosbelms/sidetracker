// Adapted from PALETTE, Copyright 2026 Filliformes, MIT (see ../LICENSE).
// Derived Airwindows/Mutable DSP attribution is recorded in ../README.md.
#include "../dsp/Helpers.h"
namespace samplefx { namespace stpalette {
#define SR (s->sr)
#define MAX_DELAY (s->max_delay)
#define sinf fastSin
#define cosf fastCos
#define tanhf fastTanh
/* DOUBLER — ADT stereo double-track / slapback. amount=mix, macro=time,
 * drift=random momentary detune. */
static void fx_doubler(slot_dsp_t *s, float *l, float *r, int n,
                       float amount, float macro, float drift){
    float tgtL=(s->sync_time>0.0f)? s->sync_time : SR*(0.012f+macro*0.045f); /* 12..57ms / synced */
    if(tgtL>(float)(MAX_DELAY/2)) tgtL=(float)(MAX_DELAY/2);
    if(s->f2<1.0f) s->f2=tgtL;                        /* f2 = gliding delay time */
    for(int i=0;i<n;i++){
        s->f2 += clampf(0.001f*(tgtL-s->f2), -0.25f, 0.25f);   /* Time glides: no read-head jumps */
        float baseL=s->f2, baseR=baseL*1.28f;
        s->lfo+=0.6f/SR; if(s->lfo>=1.0f)s->lfo-=1.0f;
        s->lfo2+=0.43f/SR; if(s->lfo2>=1.0f)s->lfo2-=1.0f;
        float wob=(drift>0.0f? wander(&s->f1,&s->seed,0.004f)*drift:0.0f);
        float dL=baseL*(1.0f+0.004f*sinf(s->lfo*TWO_PI)+0.01f*wob);
        float dR=baseR*(1.0f+0.004f*sinf(s->lfo2*TWO_PI)-0.01f*wob);
        s->dl_l[s->wp]=l[i]; if(s->channels==2) s->dl_r[s->wp]=r[i];
        float eL=dlr(s->dl_l,s->wp,dL), eR=dlr(s->dl_r,s->wp,dR);
        if(++s->wp==MAX_DELAY)s->wp=0;
        l[i]=lerpf(l[i],l[i]*0.7f+eL,amount);
        r[i]=lerpf(r[i],r[i]*0.7f+eR,amount);
    }
}

/* VIBRATO — fully-stereo modulated delay. L and R get the SAME LFO 90° apart
 * (true stereo vibrato/widening). 2nd LFO through-zero-FMs the sweep (Airwindows
 * character) + HF restore for interpolation dulling. amount=depth, macro=rate,
 * drift=sine→random waveshape + FM depth. lfo=main phase, lfo2=FM phase. */
static void fx_vibrato(slot_dsp_t *s, float *l, float *r, int n,
                       float amount, float macro, float drift){
    float rate=(s->sync_rate>0.0f)? s->sync_rate : 0.5f+macro*7.5f;  /* 0.5..8 Hz / synced */
    float dtarget=SR*0.0005f*(0.3f+amount*4.0f);     /* mod depth in samples */
    float base=SR*0.006f;
    const float glide=rateCoeff(.003f,SR), restore=rateCoeff(.4f,SR);
    float fmRate=rate*1.61f;                          /* incommensurate 2nd LFO */
    float fmDepth=0.2f+drift*0.35f;                   /* through-zero FM amount (tamed) */
    if(s->f2<=0.0f) s->f2=dtarget;
    for(int i=0;i<n;i++){
        s->f2 += glide*(dtarget - s->f2);           /* depth glides: no read-head jumps */
        float depth=s->f2;
        s->lfo2+=fmRate/SR; if(s->lfo2>=1.0f)s->lfo2-=1.0f;
        s->lfo+=(rate*(1.0f+fmDepth*0.5f*sinf(s->lfo2*TWO_PI)))/SR;  /* through-zero FM */
        if(s->lfo>=1.0f)s->lfo-=1.0f; if(s->lfo<0.0f)s->lfo+=1.0f;
        float phR=s->lfo+0.25f; if(phR>=1.0f)phR-=1.0f;   /* R 90° offset = stereo */
        float rndL=(drift>0.0f? wander(&s->f1,&s->seed,0.008f):0.0f);
        float rndR=(drift>0.0f? wander(&s->f3,&s->seed,0.008f):0.0f);
        float modL=lerpf(sinf(s->lfo*TWO_PI),rndL,clampf(drift,0.0f,0.9f));
        float modR=lerpf(sinf(phR*TWO_PI),    rndR,clampf(drift,0.0f,0.9f));
        float dL=base+depth*(1.0f+modL), dR=base+depth*(1.0f+modR);
        s->dl_l[s->wp]=l[i]; if(s->channels==2) s->dl_r[s->wp]=r[i];
        float oL=dlr(s->dl_l,s->wp,dL), oR=dlr(s->dl_r,s->wp,dR);
        /* HF restore: high-shelf the interpolation loss back in */
        s->z1l+=restore*(oL-s->z1l)+DENORM; oL+=(oL-s->z1l)*0.10f;
        s->z1r+=restore*(oR-s->z1r)+DENORM; oR+=(oR-s->z1r)*0.10f;
        l[i]=oL; r[i]=oR;
        if(++s->wp==MAX_DELAY)s->wp=0;
    }
}

/* PHASER — N-stage allpass cascade + feedback. Frequency-accurate: the LFO sweeps
 * the notch frequency in Hz and the first-order allpass coefficient is derived as
 * a=(g-1)/(g+1), g=tan(π·fc/SR) — so notches sit at musical frequencies (Surge-style)
 * instead of a raw 0–1 coefficient. amount=intensity/stages, macro=rate,
 * drift=sweep randomness. */
STPALETTE_HOT_CODE static void fx_phaser(slot_dsp_t *s, float *l, float *r, int n,
                      float amount, float macro, float drift){
    /* 2..12 stages, CONTINUOUS: all 12 allpasses always run (their state stays live)
     * and the output blends between stage n and n+1. An integer stage count jumped
     * the phase response at every step of Amount - a click per step. */
    const int stages=s->quality==0?12:s->quality==1?8:4;
    float stf=2.0f+amount*(stages-2); int n0=(int)stf; if(n0>stages)n0=stages;
    float sfr=(n0<stages)? stf-(float)n0 : 0.0f;
    float rate=(s->sync_rate>0.0f)? s->sync_rate : 0.05f+macro*macro*4.0f;  /* synced */
    float fb=amount*0.6f;
    for(int i=0;i<n;i++){
        s->lfo+=rate/SR; if(s->lfo>=1.0f)s->lfo-=1.0f;
        float rnd=(drift>0.0f? wander(&s->f2,&s->seed,0.01f)*drift*0.4f:0.0f);
        for(int ch=0;ch<s->channels;ch++){
            /* quadrature LFO: R is 90° ahead of L → fully stereo sweep */
            float ph=s->lfo + (ch?0.25f:0.0f); if(ph>=1.0f)ph-=1.0f;
            if (!(s->control & 15)) {
                float sweep=.5f+.5f*sinf(ph*TWO_PI)+rnd;
                float fc=200.f*exp2f(4*clampf(sweep,0,1));
                float g=tanf(3.14159265f*clampf(fc,30.f,SR*.4f)/SR);
                float target=(g-1)/(g+1);
                if(!s->control) s->coefficients[ch]=target;
                s->coefficients[ch+2]=(target-s->coefficients[ch])*(1.f/16);
            }
            float coef=(s->coefficients[ch]+=s->coefficients[ch+2]);
            float *ap=ch?s->ap_r:s->ap_l; float *fbz=ch?&s->z2r:&s->z2l;
            float x=(ch?r:l)[i]+ *fbz*fb, xa=x, xb=x;
            for(int k=0;k<stages;k++){
                float y=coef*x+ap[k];                 /* 1st-order allpass */
                ap[k]=x-coef*y+DENORM; x=y;
                if(k==n0-1) xa=x; else if(k==n0) xb=x;
            }
            x=xa+(xb-xa)*sfr;
            *fbz=x;
            (ch?r:l)[i]=lerpf((ch?r:l)[i],0.5f*((ch?r:l)[i]+x),amount); /* amount=0 → dry */
        }
        ++s->control;
    }
}

/* TREMOLO — Airwindows Tremolo (Chris Johnson, MIT): chase-smoothed skew/density
 * waveshaping. amount=depth, macro=rate. DRIFT = AutoPan: morphs L/R from in-phase
 * (mono tremolo) to anti-phase (the channels duck oppositely → the modulation pans
 * across the stereo field). State: sm1/sm2=chased speed/depth, f4/f5=last targets. */
STPALETTE_HOT_CODE static void fx_tremolo(slot_dsp_t *s, float *l, float *r, int n,
                       float amount, float macro, float drift){
    float speedChase=macro*macro*macro*macro;          /* A^4 (Airwindows) */
    float depthChase=clampf(amount,0.0f,1.0f);
    float speedSpeed=300.0f/(fabsf(s->f4-speedChase)+1.0f);
    float depthSpeed=300.0f/(fabsf(s->f5-depthChase)+1.0f);
    s->f4=speedChase; s->f5=depthChase;
    float pan=clampf(drift,0.0f,1.0f);
    for(int i=0;i<n;i++){
        s->sm1=((s->sm1*speedSpeed)+speedChase)/(speedSpeed+1.0f);
        s->sm2=((s->sm2*depthSpeed)+depthChase)/(depthSpeed+1.0f);
        float speed=(s->sync_rate>0.0f)? s->sync_rate*TWO_PI/SR : 0.0001f+(s->sm1/1000.0f);
        float s2=s->sm2*s->sm2, s4=s2*s2; float skew=1.0f+s->sm2*s4*s4; /* sm2^9 via mults */
        float density=((1.0f-s->sm2)*2.0f)-1.0f;
        float offset=sinf(s->lfo);
        s->lfo+=speed; if(s->lfo>TWO_PI)s->lfo-=TWO_PI;
        float control=fabsf(offset);
        if(density>0.0f) control=control*(1.0f-density)+sinf(control)*density;
        else             control=control*(1.0f+density)+(1.0f-cosf(control))*(-density);
        float ctl[2]={control, lerpf(control,1.0f-control,pan)};  /* R anti-phase at drift=1 */
        for(int ch=0;ch<s->channels;ch++){
            float thickness=((ctl[ch]*2.0f)-1.0f)*skew;
            float out=fabsf(thickness);
            float x=(ch?r:l)[i];
            float br=fabsf(x); if(br>1.57079633f)br=1.57079633f;
            br=(thickness>0.0f)? sinf(br) : (1.0f-cosf(br));
            (ch?r:l)[i]=(x>0.0f)? x*(1.0f-out)+br*out : x*(1.0f-out)-br*out;
        }
    }
}

/* PITCH — delay-line pitch shifter ±1 oct + lo-fi grit. amount=wet mix,
 * macro=pitch, drift=resolution drop. Dual-tap crossfade window. */
STPALETTE_HOT_CODE static void fx_pitch(slot_dsp_t *s, float *l, float *r, int n,
                     float amount, float macro, float drift){
    float ratio=powf(2.0f,(macro-0.5f)*2.0f);        /* 0.5 .. 2.0 */
    float win=SR*0.040f;                              /* 40 ms window */
    /* read delay = win - f1, so the read head moves 1 + (ratio-1) = ratio samples per
     * sample. (Was 1-ratio: speed 2-ratio, i.e. inverted, and FROZEN at Macro 1.) */
    float drate=(ratio-1.0f);                         /* read-ptr drift / sample */
    /* drift = gentle resolution drop: 9 bits (subtle) → 4 bits (gritty), blended in */
    float q=(drift>0.0f)? powf(2.0f, 9.0f-drift*5.0f) : 0.0f;
    for(int i=0;i<n;i++){
        s->dl_l[s->wp]=l[i]; if(s->channels==2) s->dl_r[s->wp]=r[i];
        s->f1+=drate; if(s->f1<0)s->f1+=win; if(s->f1>=win)s->f1-=win;
        float p2=s->f1+win*0.5f; if(p2>=win)p2-=win;
        float w1=0.5f-0.5f*cosf(TWO_PI*s->f1/win);    /* triangular/raised-cos xfade */
        float w2=1.0f-w1;
        float oL=dlr(s->dl_l,s->wp,win-s->f1)*w1 + dlr(s->dl_l,s->wp,win-p2)*w2;
        float oR=dlr(s->dl_r,s->wp,win-s->f1)*w1 + dlr(s->dl_r,s->wp,win-p2)*w2;
        if(++s->wp==MAX_DELAY)s->wp=0;
        if(q>0.0f){ /* round (no DC bias), blend by drift so low drift stays subtle */
            oL=lerpf(oL, floorf(oL*q+0.5f)/q, drift);
            oR=lerpf(oR, floorf(oR*q+0.5f)/q, drift);
        }
        l[i]=lerpf(l[i],oL,amount); r[i]=lerpf(r[i],oR,amount);
    }
}

/* SHIFT — Bode single-sideband frequency shifter using the REAL Mutable Warps
 * QuadratureTransform: a 17-stage first-order allpass network (lut_ap_poles) that
 * produces a broadband 90° quadrature pair, multiplied by a complex carrier.
 * amount=wet mix, macro=shift Hz (±), drift=shift wander.
 * State per channel: s->hil[ch*34 + filt*2 + {0:x,1:y}] (Warps AllPassFilter). */
static inline float warps_ap(float *st, float in, float coef){ /* y=coef*(in-y_)+x_ */
    float y = coef*(in - st[1]) + st[0];
    st[0]=in; st[1]=y; return y;
}
static void fx_shift(slot_dsp_t *s, float *l, float *r, int n,
                     float amount, float macro, float drift){
    float shift=(macro-0.5f)*1000.0f;                /* −500..+500 Hz */
    for(int i=0;i<n;i++){
        float w=(drift>0.0f? wander(&s->f1,&s->seed,0.0006f)*drift*40.0f:0.0f);
        s->lfo2 += (shift+w)/SR; if(s->lfo2>=1.0f)s->lfo2-=1.0f; if(s->lfo2<0.0f)s->lfo2+=1.0f;
        float cw=cosf(s->lfo2*TWO_PI), sw=sinf(s->lfo2*TWO_PI);
        for(int ch=0;ch<s->channels;ch++){
            float *H=&s->hil[ch*34];                  /* 17 filters × 2 states */
            float x=(ch?r:l)[i];
            float iv=0.0f, qv=0.0f;                   /* i_out / q_out */
            for(int k=0;k<17;k++){                     /* QuadratureTransform.Process */
                float coef=-lut_ap_poles[k];
                float *dst=(k&1)?&qv:&iv;
                float src=(k<=1)?x:*dst;               /* first two read input, then cascade */
                *dst=warps_ap(&H[k*2], src, coef);
            }
            float sh=iv*cw - qv*sw;                    /* single-sideband shift */
            (ch?r:l)[i]=lerpf(x,sh,amount);
        }
    }
}


Kernel movement(unsigned type) { static const Kernel kernels[]={fx_doubler,fx_vibrato,fx_phaser,fx_tremolo,fx_pitch,fx_shift}; return type<6?kernels[type]:nullptr; }
} }
