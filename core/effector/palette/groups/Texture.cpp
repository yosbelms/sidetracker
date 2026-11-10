// Adapted from PALETTE, Copyright 2026 Filliformes, MIT (see ../LICENSE).
// Derived Airwindows/Mutable DSP attribution is recorded in ../README.md.
#include "../dsp/Helpers.h"
namespace samplefx { namespace palette {
#define SR (s->sr)
#define MAX_DELAY (s->max_delay)
#define sinf fastSin
#define cosf fastCos
#define tanhf fastTanh
/* FILTER — single-knob DJ filter on resonant Cytomic/TPT SVFs (stable). amount
 * sweeps LP→through→HP (center = open/neutral, like a DJ isolator); macro = resonance;
 * drift = cutoff wander. A dedicated LP and a dedicated HP filter both run all the
 * time (each parked wide open on the other side), and the output blends to each one
 * continuously around the center. A single SVF switching between its LP and HP taps
 * at the center jumped in phase there - the loudest click in the audit.
 * State per ch: LP = lp_x and bp_x (ic1, ic2), HP = z1x and z2x; f1 = wander. */
static inline float svf_tpt(float x, float *ic1, float *ic2, float a1, float a2, float a3, float k, int hp){
    float v3=x-*ic2, v1=a1*(*ic1)+a2*v3, v2=*ic2+a2*(*ic1)+a3*v3;
    *ic1=2.0f*v1-*ic1+DENORM; *ic2=2.0f*v2-*ic2+DENORM;
    return hp? (x-k*v1-v2) : v2;
}
static void fx_filter(slot_dsp_t *s, float *l, float *r, int n,
                      float amount, float macro, float drift){
    float amt=clampf(amount,0.0f,1.0f);
    float wf=(drift>0.0f)? 1.0f+wander(&s->f1,&s->seed,0.0007f)*drift*0.3f : 1.0f;
    float fl=(amt<0.5f)? 200.0f*powf(90.0f, amt*2.0f) : 18000.0f;          /* LP 200 Hz .. 18 kHz */
    float fh=(amt>0.5f)? 20.0f*powf(20.0f,(amt-0.5f)*2.0f) : 20.0f;        /* HP 20 Hz .. 400 Hz */
    fl=clampf(fl*wf,20.0f,18000.0f); fh=clampf(fh*wf,15.0f,18000.0f);
    float wl=clampf((0.5f-amt)/0.05f,0.0f,1.0f);                          /* dry exactly at center */
    float wh=clampf((amt-0.5f)/0.05f,0.0f,1.0f);
    if(s->f4==0.0f){ s->f2=wl; s->f3=wh; s->f4=1.0f; }                     /* f2/f3 = last weights */
    float wl0=s->f2, wh0=s->f3, dwl=(wl-wl0)/(float)n, dwh=(wh-wh0)/(float)n;
    s->f2=wl; s->f3=wh;
    float k=1.0f-clampf(macro,0.0f,1.0f)*0.9f;                            /* resonance (1/Q) */
    float gl=tanf(3.14159265f*fl/SR), gh=tanf(3.14159265f*fh/SR);
    float l1=1.0f/(1.0f+gl*(gl+k)), l2=gl*l1, l3=gl*l2;
    float h1=1.0f/(1.0f+gh*(gh+k)), h2=gh*h1, h3=gh*h2;
    for(int ch=0;ch<s->channels;ch++){
        float *src=ch?r:l;
        float *lc1=ch?&s->lp_r:&s->lp_l, *lc2=ch?&s->bp_r:&s->bp_l;
        float *hc1=ch?&s->z1r:&s->z1l,   *hc2=ch?&s->z2r:&s->z2l;
        for(int i=0;i<n;i++){
            float x=src[i];
            float lp=svf_tpt(x,lc1,lc2,l1,l2,l3,k,0);
            float hp=svf_tpt(x,hc1,hc2,h1,h2,h3,k,1);
            float a=(float)(i+1);
            src[i]=x+(lp-x)*(wl0+dwl*a)+(hp-x)*(wh0+dwh*a);
        }
    }
}

/* SQUASH — vari-mu compressor + overdrive. PORTED from Airwindows Pressure4
 * (Chris Johnson, MIT). amount=compression, macro=mu character (punchy↔smooth),
 * drift=threshold jitter. State: f1/f2=muSpeed A/B, f3/f4=muCoeff A/B, i1=flip.
 * Faithful to the Pressure4 per-sample algorithm; VST/dither scaffolding dropped. */
static void fx_squash(slot_dsp_t *s, float *l, float *r, int n,
                      float amount, float macro, float drift){
    if(s->f1<1.0f){ s->f1=s->f2=10000.0f; s->f3=s->f4=1.0f; } /* Pressure4 ctor inits */
    float At=clampf(amount,0.0f,1.0f), B=0.4f;         /* B = release speed (fixed, musical) */
    if(s->z1l<=0.0f) s->z1l=At+1e-6f;                    /* z1l = per-sample Amount (makeup is up to 20x) */
    float release=powf(1.28f-B,5.0f)*32768.0f;          /* overallscale = 1 @ 44.1k */
    float fastest=sqrtf(release);
    float mewiness=(macro*2.0f)-1.0f, unmew;
    int positivemu; if(mewiness>=0){positivemu=1;unmew=1.0f-mewiness;}
                    else{positivemu=0;mewiness=-mewiness;unmew=1.0f-mewiness;}
    float outGain=1.0f;
    for(int i=0;i<n;i++){
        s->z1l += 0.01f*(At+1e-6f - s->z1l);
        float A=s->z1l;
        float threshold=1.0f-(A*0.95f);
        float muMakeupGain=1.0f/threshold;
        float comp=0.5f+0.5f*threshold;                  /* gain-comp: cancels the 1/threshold makeup */
        float th=threshold*(drift>0.0f?(1.0f+wander(&s->f5,&s->seed,0.004f)*drift*0.2f):1.0f);
        float xl=l[i]*muMakeupGain, xr=r[i]*muMakeupGain;
        float sense=fabsf(xl); if(fabsf(xr)>sense) sense=fabsf(xr);
        const unsigned period=s->quality==0?1:s->quality==1?4:8;
        if(!(s->control % period)) {
        float *spd = s->i1?&s->f1:&s->f2;                /* muSpeedA/B */
        float *cof = s->i1?&s->f3:&s->f4;                /* muCoefficientA/B */
        if(sense>th){
            float muVary=th/sense, muAttack=sqrtf(fabsf(*spd));
            *cof = *cof*(muAttack-1.0f);
            *cof += (muVary<th)? th : muVary;
            *cof /= muAttack;
        } else {
            *cof = *cof*((*spd)*(*spd)-1.0f) + 1.0f;
            *cof /= (*spd)*(*spd);
        }
        float ns=(*spd)*((*spd)-1.0f) + fabsf(sense*release)+fastest;
        *spd = ns/(*spd);
        float target = positivemu? (*cof)*(*cof) : sqrtf(fabsf(*cof));
        target = target*mewiness + (*cof)*unmew;
            if(!s->control) s->coefficients[0]=target;
            s->coefficients[1]=(target-s->coefficients[0])/period;
            s->i1^=1;
        }
        float coeff=(s->coefficients[0]+=s->coefficients[1]);
        if(s->quality && sense>4) coeff*=4/sense;
        xl*=coeff*outGain; xr*=coeff*outGain;
        /* Pressure4 second-stage sin() overdrive */
        float br=fabsf(xl); br=(br>1.57079633f)?1.0f:sinf(br); xl=(xl>0)?br:-br;
        br=fabsf(xr);      br=(br>1.57079633f)?1.0f:sinf(br); xr=(xr>0)?br:-br;
        l[i]=lerpf(l[i],xl*comp,A); r[i]=lerpf(r[i],xr*comp,A); /* level-matched; amount=0 → dry */
        ++s->control;
    }
}

/* CASSETTE — worn cassette: WOW (slow sine) + FLUTTER (Airwindows ToTape6-style
 * random-walk pitch wobble) + tape COMPRESSION + mello tape saturation + HF roll.
 * Minimal hiss. amount=degrade intensity, macro=tone, drift=warble depth.
 * State: lfo=wow phase; f2=flutter rate, f3=flutter sweep, f4=flutter target;
 * z1=tone LP; env=compressor follower; base delay. */
PALETTE_HOT_CODE static void fx_cassette(slot_dsp_t *s, float *l, float *r, int n,
                        float amount, float macro, float drift){
    float wow_depth =(0.4f+amount*1.2f)*(1.0f+drift*1.2f); /* samples of wow */
    float flut_depth=(0.3f+amount*0.9f)*(1.0f+drift*1.6f); /* samples of flutter */
    float roll=rateCoeff(.06f+(1.f-macro)*.5f,SR);              /* tone: darker at low macro */
    float hiss=amount*0.0005f;                        /* tape hiss (see below) */
    float base=SR*0.006f;
    float sat_d=1.0f+amount*0.7f, asym=0.10f*amount;
    float comp_amt=amount*0.6f;                       /* tape compression depth */
    for(int i=0;i<n;i++){
        s->lfo+=0.55f/SR; if(s->lfo>=1.0f)s->lfo-=1.0f;            /* wow ~0.55 Hz */
        /* ToTape6 random-walk flutter: rate eases toward a new random target */
        s->f2 = s->f2*0.9985f + s->f4*0.0015f;
        s->f3 += (5.0f + s->f2*8.0f)/SR;
        if(s->f3>=1.0f){ s->f3-=1.0f; s->f4=0.24f+frand(&s->seed)*0.74f; }
        float d=base + wow_depth*sinf(s->lfo*TWO_PI) + flut_depth*sinf(s->f3*TWO_PI);
        s->dl_l[s->wp]=l[i]; if(s->channels==2) s->dl_r[s->wp]=r[i];
        float xL=dlr(s->dl_l,s->wp,d), xR=dlr(s->dl_r,s->wp,d);
        if(++s->wp==MAX_DELAY)s->wp=0;
        /* tape compression (gentle, program-dependent gain reduction) */
        float mono=0.5f*(fabsf(xL)+fabsf(xR));
        s->env+=(mono>s->env?0.05f:0.002f)*(mono-s->env)+DENORM;
        float gr=1.0f/(1.0f+s->env*comp_amt*2.0f);
        xL*=gr; xR*=gr;
        /* mello tape sat + HF rolloff */
        xL=tape_asym(tape_cubic(xL,sat_d),1.0f,asym);
        xR=tape_asym(tape_cubic(xR,sat_d),1.0f,asym);
        s->z1l+=roll*(xL-s->z1l)+DENORM; s->z1r+=roll*(xR-s->z1r)+DENORM;
        /* hiss rides the smoothed level (env), low-passed ~6 kHz. It was switched per
         * sample on the instantaneous level, so it chattered at every zero crossing. */
        float hg=hiss*clampf(s->env*30.0f,0.0f,1.0f);
        s->z2l += 0.575f*(hg*((frand(&s->seed)-0.5f)*2.0f)-s->z2l) + DENORM;
        s->z2r += 0.575f*(hg*((frand(&s->seed)-0.5f)*2.0f)-s->z2r) + DENORM;
        l[i]=lerpf(l[i],s->z1l+s->z2l,amount); r[i]=lerpf(r[i],s->z1r+s->z2r,amount);
    }
}

/* BROKEN — motor-failure pitch drops + AM/FM wobble + dropouts. amount=breakdown,
 * macro=rate of failures, drift=dropout randomness. */
PALETTE_HOT_CODE static void fx_broken(slot_dsp_t *s, float *l, float *r, int n,
                      float amount, float macro, float drift){
    float rate=0.1f+macro*macro*3.0f;                /* motor failure LFO Hz */
    for(int i=0;i<n;i++){
        /* Amount and Drift scale the READ DELAY: glide them per sample, or a knob jump
         * moves the read head tens of samples at once */
        s->f5 += 0.002f*(amount-s->f5); s->f6 += 0.002f*(drift-s->f6);
        float amt=s->f5, drf=s->f6;
        s->lfo+=rate/SR; if(s->lfo>=1.0f)s->lfo-=1.0f;
        s->lfo2+=rate*1.7f/SR; if(s->lfo2>=1.0f)s->lfo2-=1.0f;   /* own phase: x1.7 of a
                                     * wrapping phase jumped the read head ~60 samples */
        /* periodic motor stall: read offset is ~0 at rest (so wet≈dry, NO doubling)
         * and grows during the dip (pitch drops, then recovers each cycle). */
        float dip=amt*(0.5f-0.5f*cosf(s->lfo*TWO_PI));
        s->dl_l[s->wp]=l[i]; if(s->channels==2) s->dl_r[s->wp]=r[i];
        float rd=3.0f + dip*SR*0.03f                  /* baseline ~0 + up to ~30 ms drop */
               + (drf>0.0f? (0.5f+0.5f*sinf(s->lfo2*TWO_PI))*drf*SR*0.003f : 0.0f);
        float xL=dlr(s->dl_l,s->wp,rd), xR=dlr(s->dl_r,s->wp,rd);
        if(++s->wp==MAX_DELAY)s->wp=0;
        /* DRIFT = occasional SMOOTHED dropouts (gate ramps → no clicks) + the warble above */
        if(drift>0.0f && frand(&s->seed)<drift*0.0004f) s->i1=(int)(SR*0.05f*frand(&s->seed));
        float gtarget=1.0f; if(s->i1>0){ s->i1--; gtarget=0.0f; }
        s->f4 += 0.008f*(gtarget - s->f4) + DENORM;   /* two-pole ~5 ms gate: a one-pole */
        s->f3 += 0.008f*(s->f4    - s->f3) + DENORM;   /* starts with a corner (a tick)   */
        float am=1.0f-amt*0.3f*(0.5f-0.5f*cosf(s->lfo*TWO_PI*2.0f));      /* AM wobble */
        l[i]=lerpf(l[i],xL*s->f3*am,amount); r[i]=lerpf(r[i],xR*s->f3*am,amount);
    }
}

/* INTERFERENCE — lo-fi telecom/radio destruction. Core PORTED from Airwindows
 * DeRez2 (Chris Johnson, MIT): sample-rate reduction + soften + µ-law encode +
 * bit-depth quantize. Ring-mod carrier (macro) + static bursts (drift) on top.
 * amount=crush intensity, macro=carrier/tone, drift=static. State: z1=lastSample,
 * z2=heldSample, z3=lastDry, z4=lastOut (per ch); f1=position, f2/f3=incrA/incrB. */
static void fx_interference(slot_dsp_t *s, float *l, float *r, int n,
                            float amount, float macro, float drift){
    float A=clampf(amount,0.0f,1.0f);
    /* crush scales WITH amount (was inverted → heavy crush + noise at low amounts).
     * targetA = sample-rate increment: ~1 (clean) at A=0 → 0.03 (heavy SR reduction) at A=1.
     * targetB = bit-quant step: 0 (no quantize) at A=0 → coarse at A=1. */
    float targetA=1.0f - A*0.92f; if(targetA<0.08f)targetA=0.08f;   /* tamed (Loopex) */
    float soften=(1.0f+targetA)/2.0f;
    float targetB=A*A*0.22f;                          /* bit-depth derez (scales up with amount) */
    float hard=0.6f;                                  /* more dry blend = less harsh */
    float carr=200.0f+macro*macro*3000.0f;
    
    for(int i=0;i<n;i++){
        s->f2=((s->f2*999.0f)+targetA)/1000.0f;        /* incrementA (rate) */
        s->f3=((s->f3*999.0f)+targetB)/1000.0f;        /* incrementB (bits) */
        s->f1+=s->f2;                                  /* position */
        float in[2]={l[i],r[i]};
        { float m=0.5f*(fabsf(in[0])+fabsf(in[1])); s->env+=(m>s->env?0.01f:0.00008f)*(m-s->env)+DENORM; }
        float out[2]={s->z2l,s->z2r};                  /* outputSample = heldSample */
        if(s->f1>1.0f){
            s->f1-=1.0f;
            float last[2]={s->z1l,s->z1r};
            float h0=last[0]*s->f1+in[0]*(1.0f-s->f1);
            float h1=last[1]*s->f1+in[1]*(1.0f-s->f1);
            out[0]=out[0]*(1.0f-soften)+h0*soften;
            out[1]=out[1]*(1.0f-soften)+h1*soften;
            s->z2l=h0; s->z2r=h1;                      /* heldSample */
        }
        s->z1l=in[0]; s->z1r=in[1];                    /* lastSample */
        float lastOut[2]={s->z4l,s->z4r};
        float lastDry[2]={s->z3l,s->z3r};
        for(int c=0;c<2;c++){
            float x=out[c];
            if(x!=lastOut[c]){ float t=x; x=x*hard+lastDry[c]*(1.0f-hard); lastOut[c]=t; }
            else lastOut[c]=x;
            float temp=x;
            if(x>1.0f)x=1.0f; else if(x<-1.0f)x=-1.0f;  /* µ-law encode */
            if(x>0.0f) x= muLaw(x);
            else if(x<0.0f) x=-muLaw(-x);
            x=temp*hard + x*(1.0f-hard);
            if(s->f3>0.0005f)                           /* bit-depth quantize (O(1)), ROUNDED: */
                x=floorf(x/s->f3+0.5f)*s->f3;           /* ceil-away-from-0 blew any hiss up to a full step */
            x=lerpf(x, x*sinf(s->lfo*TWO_PI), macro*0.3f);  /* ring-mod radio */
            /* static bursts ride the input level (fade ~0.3 s after it stops) and are
             * ~12 dB lower: at -29 dBFS they crackled even with no signal at all */
            if(drift>0.0f && frand(&s->seed)<drift*0.04f) x+=(frand(&s->seed)-0.5f)*drift*0.25f*clampf(s->env*8.0f,0.0f,1.0f);
            out[c]=x;
        }
        s->z3l=in[0]; s->z3r=in[1];                    /* lastDry = dry input */
        s->z4l=lastOut[0]; s->z4r=lastOut[1];
        s->lfo+=carr/SR; if(s->lfo>=1.0f)s->lfo-=1.0f;
        l[i]=lerpf(in[0], out[0]*0.6f, amount);        /* amount=0 → dry; trim µ-law boost */
        r[i]=lerpf(in[1], out[1]*0.6f, amount);
    }
}

/* HALO — ethereal harmonic resonator pad (Walrus Qi / OBNE Dark-Star vibe).
 * A 6-voice Karplus-Strong / tuned-comb bank: the chain audio sympathetically
 * excites delay lines tuned to a chord (root, 5th, oct, +oct-3rd, +oct-5th, 2-oct),
 * each with a damping one-pole and soft-clipped feedback so they ring/bloom into a
 * frozen harmonic drone. Pure C (replaces the FFT FREEZE). amount = resonance/
 * sustain (feedback), macro = root pitch + brightness, drift = per-voice detune.
 * Comb buffers reuse dl_l/dl_r (6 regions × 1024); shared region index = i1;
 * per-voice damping = halo_lp_l/r; drift walk = f1. */
#define HALO_VLEN 1024
static void fx_halo(slot_dsp_t *s, float *l, float *r, int n,
                    float amount, float macro, float drift){
    static const float inverseRatio[6]={1.0f,.6674186f,.5f,.3968506f,.3337104f,.25f}; /* R 5 8ve +M3 +5 +2-8ve */
    float rtarget=55.0f*powf(2.0f,macro*2.0f);       /* 55..220 Hz root (A1..A3) */
    if(s->f2<=0.0f) s->f2=rtarget;
    float fb=0.90f+amount*0.099f;                     /* 0.90..0.999 ring time */
    float damp=0.12f+macro*0.55f;                     /* brighter as macro rises */
    float exc=amount*0.5f;                            /* excitation into the bank */
    for(int i=0;i<n;i++){
        float drf=(drift>0.0f)? wander(&s->f1,&s->seed,0.0006f)*drift*0.012f : 0.0f;
        s->f2 += 0.0015f*(rtarget-s->f2);              /* root glides: comb taps never jump */
        float root=s->f2;
        if(!(s->control++ & 15)) s->coefficients[0]=SR/root;
        for(int ch=0;ch<s->channels;ch++){
            History& buf=ch?s->dl_r:s->dl_l;
            float *lp =ch?s->halo_lp_r:s->halo_lp_l;
            float in=(ch?r:l)[i];
            float sum=0.0f;
            for(int v=0;v<(s->quality==0?6:s->quality==1?4:3);v++){
                float dl=(s->coefficients[0]*inverseRatio[v])*(1.0f+drf*(float)v);
                if(dl>HALO_VLEN-2) dl=HALO_VLEN-2; if(dl<2.0f) dl=2.0f;
                int baseIndex=v*HALO_VLEN;
                float rp=(float)s->i1-dl; while(rp<0.0f) rp+=HALO_VLEN;
                int i0=(int)rp; float fr=rp-(float)i0; int i1n=i0+1; if(i1n>=HALO_VLEN)i1n=0;
                float y=float(buf[baseIndex+i0])+(float(buf[baseIndex+i1n])-float(buf[baseIndex+i0]))*fr;
                lp[v]+=damp*(y-lp[v])+DENORM;          /* loop damping */
                buf[baseIndex+s->i1]=in*exc + sb_tanh(lp[v]*fb);  /* excite + soft-clipped regen */
                sum+=y;
            }
            (ch?r:l)[i]=lerpf(in, sum*0.32f, amount);  /* amount=0 → dry */
        }
        s->i1++; if(s->i1>=HALO_VLEN) s->i1=0;
    }
}


Kernel texture(unsigned type) { static const Kernel kernels[]={fx_filter,fx_squash,fx_cassette,fx_broken,fx_interference,fx_halo}; return type<6?kernels[type]:nullptr; }
} }
