// Adapted from PALETTE, Copyright 2026 Filliformes, MIT (see ../LICENSE).
// Derived Airwindows/Mutable DSP attribution is recorded in ../README.md.
#include "../dsp/Helpers.h"
namespace samplefx { namespace stpalette {
#define SR (s->sr)
#define MAX_DELAY (s->max_delay)
#define sinf fastSin
#define cosf fastCos
#define tanhf fastTanh
/* delay-core helper. Differentiates BBD vs tape via: feedback cap, HF damping,
 * wow rate/depth, flutter, feedback saturation drive, and tape hiss. The wet tap
 * scales with amount so amount=0 → dry. */
static void delay_core(slot_dsp_t *s, float *l, float *r, int n,
                       float amount, float macro, float drift,
                       float fb_cap, float lp_amt, float wow_hz, float wow_depth,
                       float sat_drive, float flutter_depth, float hiss){
    float t=(s->sync_time>0.0f)? s->sync_time : SR*(0.03f*powf(40.0f,macro)); /* 30ms..1.2s / synced */
    if(s->f6<1.0f || s->f6>SR*2.5f) s->f6=t;          /* init/repair smoothed time */
    float fb=amount*fb_cap;
    lp_amt=rateCoeff(lp_amt,SR);
    const float glide=rateCoeff(.0004f,SR);
    float wet=clampf(amount*1.4f,0.0f,1.0f);          /* amount=0 → dry */
    for(int i=0;i<n;i++){
        /* glide delay time, slew-limited to +-0.5 samples/sample (pitch 0.5x..1.5x while
         * it moves, like a tape machine) - unlimited, a fast Time turn read at -8x */
        s->f6 += clampf(glide*(t-s->f6), -0.5f, 0.5f);
        s->lfo+=wow_hz/SR;  if(s->lfo>=1.0f)s->lfo-=1.0f;     /* wow */
        s->lfo2+=6.3f/SR;   if(s->lfo2>=1.0f)s->lfo2-=1.0f;   /* flutter */
        float wob=wow_depth*sinf(s->lfo*TWO_PI)
                 + flutter_depth*sinf(s->lfo2*TWO_PI)
                 + (drift>0.0f? wander(&s->f1,&s->seed,0.001f)*drift*wow_depth*3.0f:0.0f);
        float d=s->f6*(1.0f+wob);
        float tapL=dlr(s->dl_l,s->wp,d), tapR=dlr(s->dl_r,s->wp,d);
        /* 2-pole LP darkening in the feedback (more = warmer/tape) */
        s->z1l+=lp_amt*(tapL-s->z1l)+DENORM; s->z2l+=lp_amt*(s->z1l-s->z2l)+DENORM;
        s->z1r+=lp_amt*(tapR-s->z1r)+DENORM; s->z2r+=lp_amt*(s->z1r-s->z2r)+DENORM;
        float fl=delay_saturate(s->z2l*fb, sat_drive, 0.02f);
        float fr=delay_saturate(s->z2r*fb, sat_drive, 0.02f);
        /* hiss stays OUT of the feedback path: at REELS's >unity feedback it accumulated
         * into loud noise, and held its own gate open (ported from Loopex). */
        s->dl_l[s->wp]=l[i]+fl; if(s->channels==2) s->dl_r[s->wp]=r[i]+fr;
        if(++s->wp==MAX_DELAY)s->wp=0;
        float hsL=0.0f,hsR=0.0f;
        if(hiss>0.0f){
            /* tape hiss on the wet output only, riding a smoothed echo envelope (2 ms up,
             * ~150 ms down) and low-passed ~6 kHz. It used to be switched per SAMPLE on
             * the instantaneous tap level, so it chattered on/off at every zero crossing. */
            float mag=0.5f*(fabsf(tapL)+fabsf(tapR));
            s->env += (mag>s->env? 0.01f : 0.00015f)*(mag-s->env) + DENORM;
            float hg=hiss*clampf(s->env*30.0f,0.0f,1.0f);
            s->z3l += 0.575f*(hg*(frand(&s->seed)-0.5f)-s->z3l) + DENORM;
            s->z3r += 0.575f*(hg*(frand(&s->seed)-0.5f)-s->z3r) + DENORM;
            hsL=s->z3l; hsR=s->z3r;
        }
        l[i]+=(tapL*0.9f+hsL)*wet; r[i]+=(tapR*0.9f+hsR)*wet;
    }
}
/* CASCADE — BBD bucket-brigade: brighter-but-bandlimited repeats, fast subtle
 * clock warble, moderate saturation, no hiss. Clean-ish, metallic, can self-osc. */
static void fx_cascade(slot_dsp_t *s, float *l, float *r, int n,
                       float amount, float macro, float drift){
    delay_core(s,l,r,n,amount,macro,drift,0.95f,0.22f,6.0f,0.00035f,1.3f,0.0f,0.0f);
}
/* REELS — worn tape echo (RE-201): warmer/darker, slow wow + flutter, heavier
 * tape saturation, gated hiss, higher feedback cap (self-oscillates). */
static void fx_reels(slot_dsp_t *s, float *l, float *r, int n,
                     float amount, float macro, float drift){
    delay_core(s,l,r,n,amount,macro,drift,1.05f,0.42f,0.7f,0.0016f,1.9f,0.0006f,0.0006f);
}

/* REVERSE — reverse delay. amount=wet mix, macro=segment time, drift=pitch mod.
 * Records forward; two read heads play the recent past BACKWARD (delay grows by 2
 * samples per sample = reverse at 1x), half a segment apart, each under a sin^2
 * window, so the windows sum to 1 and the reversed stream is continuous. Each head
 * latches the segment length when it wraps, so turning Time never jumps a head.
 * (Was one head whose delay SHRANK by 1/sample: forward at 2x, not reverse.)
 * State: f2=phase, f3/f4=latched segment A/B, f6=smoothed segment. */
STPALETTE_HOT_CODE static void fx_reverse(slot_dsp_t *s, float *l, float *r, int n,
                       float amount, float macro, float drift){
    float seg=(s->sync_time>0.0f)? s->sync_time : SR*(0.08f+macro*0.6f); /* 80..680ms / synced */
    if(seg>(float)(MAX_DELAY/2-8)) seg=(float)(MAX_DELAY/2-8);
    float spd=1.0f+(drift>0.0f? wander(&s->f1,&s->seed,0.0008f)*drift*0.05f:0.0f);
    if(s->f6<1.0f){ s->f6=seg; s->f3=seg; s->f4=seg; s->f2=0.0f; }
    for(int i=0;i<n;i++){
        s->f6 += 0.0005f*(seg-s->f6);
        s->dl_l[s->wp]=l[i]; if(s->channels==2) s->dl_r[s->wp]=r[i];
        float ph=s->f2, phb=ph+0.5f; if(phb>=1.0f) phb-=1.0f;
        float sa=sinf(3.14159265f*ph),  wa=sa*sa;
        float sb=sinf(3.14159265f*phb), wb=sb*sb;
        float da=2.0f+2.0f*s->f3*ph, db=2.0f+2.0f*s->f4*phb;
        float oL=dlr(s->dl_l,s->wp,da)*wa + dlr(s->dl_l,s->wp,db)*wb;
        float oR=dlr(s->dl_r,s->wp,da)*wa + dlr(s->dl_r,s->wp,db)*wb;
        if(++s->wp==MAX_DELAY)s->wp=0;
        float nph=ph+spd/s->f6;
        if(ph<0.5f && nph>=0.5f) s->f4=s->f6;          /* head B wraps (its window is 0) */
        if(nph>=1.0f){ nph-=1.0f; s->f3=s->f6; }       /* head A wraps */
        s->f2=nph;
        l[i]=lerpf(l[i],oL,amount); r[i]=lerpf(r[i],oR,amount);
    }
}

/* COLLAGE — glitch/granular looping delay. amount=feedback, macro=loop time,
 * drift=random double-speed/reverse grains.
 * A grain is a read head at delay f3 inside the loop; the delay moves by (1-speed)
 * per sample, so speed 1 = steady repeat, 2 = octave up, -1 = reverse. (It used to
 * move by -speed: every grain played at 2x and "reverse" grains froze on one sample.)
 * Every grain change - new grain or a wrap inside the loop - hands the old head a
 * ~7 ms fade-out while the new one fades in, instead of jumping. Feedback is
 * soft-clipped. State: f3/f4=head delay/speed, f1/f2=fading head, i1=samples left
 * in grain, i2=crossfade left, f5=smoothed loop length. */
#define COL_XF 300
STPALETTE_HOT_CODE static void fx_collage(slot_dsp_t *s, float *l, float *r, int n,
                       float amount, float macro, float drift){
    const float glide=rateCoeff(.0004f,SR);
    float loop=(s->sync_time>0.0f)? s->sync_time : SR*(0.05f+macro*1.2f);
    if(loop>(float)(MAX_DELAY-COL_XF*4)) loop=(float)(MAX_DELAY-COL_XF*4);
    if(s->f5<1.0f || s->f5>SR*1.3f) s->f5=loop;       /* init/repair smoothed loop time */
    float fb=amount*0.9f;
    for(int i=0;i<n;i++){
        s->f5 += clampf(glide*(loop-s->f5), -0.5f, 0.5f);   /* glide loop time, slew-limited */
        float lps=s->f5;
        /* one crossfade at a time: starting another mid-fade would drop the fading head */
        if(s->i2<=0 && (s->i1<=0 || s->f3<1.0f || s->f3>lps)){
            s->f1=s->f3; s->f2=s->f4; s->i2=(s->f4!=0.0f)? COL_XF : 0;   /* old head fades
                                                         (f4==0 only before the first grain) */
            if(s->i1<=0){                              /* new grain */
                s->i1=(int)(SR*(0.04f+frand(&s->seed)*0.12f));
                s->f4=1.0f;
                if(drift>0.0f && frand(&s->seed)<drift*0.5f) s->f4=(frand(&s->seed)<0.5f?2.0f:-1.0f);
                s->f3=1.0f+frand(&s->seed)*(lps-2.0f);
            } else {                                   /* wrapped inside the loop */
                s->f3 += (s->f3<1.0f)? lps-1.0f : -(lps-1.0f);
                s->f3=clampf(s->f3,1.0f,lps);
            }
        }
        s->i1--;
        float hd=s->f3<1.0f? 1.0f : s->f3;            /* past the edge while a fade finishes */
        float gL=dlr(s->dl_l,s->wp,hd), gR=dlr(s->dl_r,s->wp,hd);
        s->f3 += 1.0f - s->f4;
        if(s->i2>0){                                   /* crossfade from the old head */
            float w=(float)s->i2/(float)COL_XF;
            float od=s->f1<1.0f? 1.0f : s->f1;
            gL=gL*(1.0f-w)+dlr(s->dl_l,s->wp,od)*w;
            gR=gR*(1.0f-w)+dlr(s->dl_r,s->wp,od)*w;
            s->f1 += 1.0f - s->f2; s->i2--;
        }
        s->dl_l[s->wp]=sb_tanh(l[i]+gL*fb); if(s->channels==2) s->dl_r[s->wp]=sb_tanh(r[i]+gR*fb);
        if(++s->wp==MAX_DELAY)s->wp=0;
        l[i]=lerpf(l[i],gL,amount*0.9f); r[i]=lerpf(r[i],gR,amount*0.9f);
    }
}


void fx_space(slot_dsp_t*,float*,float*,int,float,float,float);
void fx_bloom(slot_dsp_t*,float*,float*,int,float,float,float);

Kernel diffusion(unsigned type) { static const Kernel kernels[]={fx_cascade,fx_reels,fx_collage,fx_reverse,fx_space,fx_bloom}; return type<6?kernels[type]:nullptr; }
} }
