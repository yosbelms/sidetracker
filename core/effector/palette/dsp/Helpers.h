#pragma once
#include "State.h"
#include "../tables/Tables.h"
extern "C" const int16_t AudioWaveformSine[257];
namespace samplefx { namespace palette {
constexpr float TWO_PI=6.28318530718f, DENORM=1e-20f;
inline float clampf(float x,float a,float b) { return std::clamp(x,a,b); }
inline float lerpf(float a,float b,float t) { return a+(b-a)*t; }
inline float rateCoeff(float coefficient,float rate) {
    if(rate>40000)return coefficient;
    float remain=1-coefficient; remain*=remain;
    if(rate<15000)remain*=remain;
    return 1-remain;
}
inline float frand(uint32_t* seed) { uint32_t x=*seed; x^=x<<13; x^=x>>17; x^=x<<5; *seed=x; return (x>>8)*(1.f/16777216); }
inline float wander(float* st,uint32_t* seed,float speed) { *st+=speed*((frand(seed)-.5f)*2-*st); return *st; }
inline float fastSin(float radians) {
    float phase=radians*(256.f/TWO_PI);
    int whole=int(phase); float f=phase-whole;
    if(f<0) { --whole; f+=1; }
    unsigned i=unsigned(whole)&255;
    return lerpf(AudioWaveformSine[i],AudioWaveformSine[i+1],f)*(1.f/32768);
}
inline float fastCos(float x) { return fastSin(x+TWO_PI*.25f); }
inline float curveAt(const int16_t* curve,int count,float x) {
    x=clampf(x,0.f,float(count-1)); int i=std::min(int(x),count-2);
    return lerpf(curve[i],curve[i+1],x-i)*(1.f/32767);
}
inline float curveIntegral(const int16_t* curve,const float* integral,int count,float x) {
    if(x<0) return integral[0]+x*(curve[0]*(1.f/32767));
    if(x>=count-1) return integral[count-1]+(x-(count-1))*(curve[count-1]*(1.f/32767));
    int i=int(x); float f=x-i;
    return integral[i]+f*(curve[i]*(1.f/32767)+.5f*f*((curve[i+1]-curve[i])*(1.f/32767)));
}
inline float adaa(const int16_t* curve,const float* integral,int count,float x,float* previous) {
    float old=*previous; *previous=x; float dx=x-old;
    // Local evaluation prevents cancellation of large, nearly equal integrals.
    if(std::fabs(dx)<.05f || (x>=0 && x<count-1 && int(x)==int(old)))
        return curveAt(curve,count,(x+old)*.5f);
    return (curveIntegral(curve,integral,count,x)-curveIntegral(curve,integral,count,old))/dx;
}
inline float fastTanh(float x) { return curveAt(tanhCurve,2049,(x+8)*128); }
inline float tanh_adaa(float x,float* previous) {
    float q=(x+8)*128, p=(*previous+8)*128;
    float result=adaa(tanhCurve,tanhIntegral,2049,q,&p); *previous=x; return result;
}
inline float sweetenShape(float x) { return curveAt(sweetenCurve,2049,(x+4)*256); }
inline float muLaw(float x) { return curveAt(muLawCurve,1025,x*1024); }
inline float sb_tanh(float x) {
    x=clampf(x,-3.f,3.f); float xx=x*x;
    return x*(27+xx)/(27+9*xx);
}
inline float sb_apply_dist(float x,int mode) { return mode==0 ? fastSin(clampf(x,-1.57f,1.57f)) : mode==1 ? (x>0?sb_tanh(x):x*.55f) : clampf(x*1.4f,-.85f,.85f); }
inline float delay_saturate(float x,float drive,float asym) { return (sb_tanh(x*drive+asym)-sb_tanh(asym))/drive; }
inline float tape_cubic(float x,float drive) { float y=x*drive; y=clampf(y,-1.5f,1.5f); return y-y*y*y/6.75f; }
inline float tape_asym(float x,float drive,float asym) { return sb_tanh(x*drive+asym)-sb_tanh(asym); }
inline float dlr(const History& b,int wp,float d) {
    d=clampf(d,1.f,float(b.length-2));
    float rp=wp-d; if(rp<0)rp+=b.length;
    int i=int(rp), j=i+1; if(j==b.length)j=0;
    return lerpf(b[i],b[j],rp-i);
}
} }
