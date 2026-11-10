#include <gtest/gtest.h>
#include "palette/Palette.h"
#include "SampleFxRealtime.h"
#include <vector>
#include <cmath>
#include <cstring>
#include "mtStructs.h"
#include "palette/dsp/Helpers.h"

static int16_t editorPool[EDIT_BUFF_SIZE * SAMPLE_CHANNELS];
int16_t* externalRamEffectsBuff=editorPool;
int16_t sin_q15(int16_t value) { return int16_t(std::clamp(std::lround(32768*std::sin(value*(6.283185307179586/32768))),-32768l,32767l)); }

using namespace samplefx;
namespace {
struct Arena {
    std::vector<uint8_t> bytes;
    size_t position=0;
    explicit Arena(size_t n=1024*1024):bytes(n){}
    static void* get(void* ptr,size_t n) {
        auto& a=*static_cast<Arena*>(ptr);
        size_t at=(a.position+31)&~size_t(31);
        if(at+n>a.bytes.size())return nullptr;
        void* p=a.bytes.data()+at; std::memset(p,0,n); a.position=at+n; return p;
    }
    palette::Memory memory(){return {this,get};}
};
std::vector<int16_t> run(Node node,unsigned channels,unsigned frames=8192) {
    Arena arena; palette::Processor p;
    if(!p.prepare(node,channels,frames,arena.memory(),1,130))return {};
    unsigned total=frames+p.tail();
    std::vector<int16_t> result(total*channels);
    int16_t input[2][128]={},output[2][128]={};
    int16_t* in[]={input[0],input[1]},*out[]={output[0],output[1]};
    for(unsigned at=0;at<total;at+=128) {
        unsigned count=std::min(128u,total-at);
        for(unsigned c=0;c<channels;++c) for(unsigned i=0;i<count;++i)
            input[c][i]=int16_t(std::sin((at+i)*(.031f+c*.013f))*7000);
        if(!p.process(in,out,count))return {};
        for(unsigned i=0;i<count;++i)for(unsigned c=0;c<channels;++c)result[(at+i)*channels+c]=out[c][i];
    }
    return result;
}
}
TEST(Palette, AllTypesAndQualitiesProduceDeterministicBoundedMonoAndStereo) {
    for(unsigned id=32;id<36;++id) for(unsigned type=0;type<6;++type)
    for(unsigned quality=0;quality<3;++quality) for(unsigned channels=1;channels<=2;++channels) {
        SCOPED_TRACE(::testing::Message()<<id<<":"<<type<<" quality="<<quality<<" channels="<<channels);
        Node n=palette::defaults(id); n.params[0]=type; n.params[1]=70; n.params[2]=60; n.params[3]=40; n.params[5]=0; n.quality=quality;
        auto first=run(n,channels,4099);
        ASSERT_EQ(first.size(),(4099+palette::latencyFrames(n))*channels);
        EXPECT_EQ(first,run(n,channels,4099));
    }
}
TEST(Palette, DefaultsVisibleParametersAndSelectedTypeOnlyMemory) {
    EXPECT_EQ(palette::defaults(35).params[1],50);
    Arena a,b; palette::Processor filter,delay;
    auto n=palette::defaults(35);
    ASSERT_TRUE(filter.prepare(n,1,1024,a.memory()));
    EXPECT_FALSE(palette::visible(n,palette::Tail));
    EXPECT_FALSE(palette::visible(n,palette::Sync));
    auto d=palette::defaults(34);
    ASSERT_TRUE(delay.prepare(d,1,1024,b.memory()));
    EXPECT_TRUE(palette::visible(d,palette::Tail));
    EXPECT_TRUE(palette::visible(d,palette::Sync));
    EXPECT_GT(b.position,a.position+100000);
    EXPECT_LE(a.position,palette::requiredMemory(n,1));
    EXPECT_LE(b.position,palette::requiredMemory(d,1));
}
TEST(Palette, InvalidTypesProfilesParametersAndInsufficientArenaFailSafely) {
    auto n=palette::defaults(32); Arena a(128); palette::Processor p;
    EXPECT_FALSE(p.prepare(n,1,100,a.memory()));
    n.params[0]=.5f; EXPECT_FALSE(palette::valid(n));
    n.params[0]=6; EXPECT_FALSE(palette::valid(n));
    n.params[0]=0; n.quality=3; EXPECT_FALSE(palette::valid(n));
    n.quality=0; n.params[4]=1.5; EXPECT_FALSE(palette::valid(n));
    n.params[4]=0; n.params[2]=NAN; EXPECT_FALSE(palette::valid(n));
}
TEST(Palette, TypeChangesRequirePreparationAndOrdinaryParametersPublish) {
    Arena a; palette::Processor p; auto n=palette::defaults(32);
    ASSERT_TRUE(p.prepare(n,1,1024,a.memory()));
    n.params[1]=80; EXPECT_TRUE(p.parameters(n));
    n.params[0]=2; EXPECT_FALSE(p.parameters(n));
    n.params[0]=0; n.quality=1; EXPECT_FALSE(p.parameters(n));
}
TEST(Palette, ChainV2KeepsCategoryTypeAndQualityWithLegacyV1Unchanged) {
    ChainDefinition r; auto n=palette::defaults(32);n.params[0]=2;n.quality=2;
    r.add(n);r.qualityResolved=true;r.mix=35;
    uint8_t bytes[kChainFileBytes]; ASSERT_TRUE(encode(r,bytes,sizeof(bytes))); EXPECT_EQ(bytes[4],2);
    ChainDefinition decoded;ASSERT_TRUE(decode(bytes,sizeof(bytes),decoded));
    EXPECT_EQ(decoded.stages[0].effect,32); EXPECT_EQ(decoded.stages[0].params[0],2);
    EXPECT_EQ(decoded.stages[0].quality,2); EXPECT_TRUE(decoded.qualityResolved);
    EXPECT_EQ(decoded.mix,35);
    r={}; n={};n.effect=3;n.count=1;n.params[0]=1;r.add(n);
    ASSERT_TRUE(encode(r,bytes,sizeof(bytes))); EXPECT_EQ(bytes[4],1);
    ASSERT_TRUE(decode(bytes,sizeof(bytes),decoded));
}
TEST(Palette, EconomyAdmissionIsDeterministicAndRespectsMinimumAndBypass) {
    ChainDefinition r;auto n=palette::defaults(32);n.params[0]=2;
    for(unsigned i=0;i<8;++i)r.add(n);
    auto other=r; ASSERT_TRUE(palette::resolve(r,1,4*1024*1024));
    ASSERT_TRUE(palette::resolve(other,1,4*1024*1024));
    bool economy=false;for(unsigned i=0;i<8;++i){EXPECT_EQ(r.stages[i].quality,other.stages[i].quality);economy|=r.stages[i].quality!=0;}
    EXPECT_TRUE(economy);
    EXPECT_FALSE(palette::resolve(r,1,1));
    for(unsigned i=0;i<8;++i)r.stages[i].bypass=true;
    EXPECT_TRUE(palette::resolve(r,1,1));
}
TEST(Palette, PreviewBudgetRequiresConsecutiveSlowBlocks) {
    RealtimeChain chain;
    chain.reportDuration(1200);EXPECT_FALSE(chain.budgetOverrun());
    chain.reportDuration(900);EXPECT_FALSE(chain.budgetOverrun());
    chain.reportDuration(1100);chain.reportDuration(1200);
    EXPECT_EQ(chain.budgetOverrun().chainUs,1200u);
    EXPECT_EQ(chain.budgetOverrun().callbackUs,0u);
    chain.reportDuration(900);EXPECT_FALSE(chain.budgetOverrun());
    chain.reportDuration(900,2200);chain.reportDuration(910,2250);
    EXPECT_EQ(chain.budgetOverrun().chainUs,910u);
    EXPECT_EQ(chain.budgetOverrun().callbackUs,2250u);
}

TEST(Palette, AntialiasedSaturatorMatchesHighPrecisionReferenceAndHandlesSmallDifferences) {
    for(float a : {-12.f,-3.f,-.01f,0.f,.1f,1.f,7.f,12.f})
    for(float b : {-11.f,-2.f,-.00999f,.000001f,.100001f,.5f,8.f,11.f}) {
        float previous=a;
        double reference=std::fabs(b-a)<1e-4 ? std::tanh((double(a)+b)*.5) :
            (std::log(std::cosh(double(b)))-std::log(std::cosh(double(a))))/(b-a);
        EXPECT_NEAR(palette::tanh_adaa(b,&previous),reference,.003) << a << " -> " << b;
    }
}

TEST(Palette, AntialiasedWaveshaperReducesFoldedHighHarmonicEnergy) {
    constexpr unsigned count=1024, fundamental=87;
    std::vector<float> naive(count),antialiased(count);
    float previous=5*std::sin(-6.283185307179586f*fundamental/count);
    for(unsigned i=0;i<count;++i) {
        float x=5*std::sin(6.283185307179586f*fundamental*i/count);
        naive[i]=palette::fastTanh(x);antialiased[i]=palette::tanh_adaa(x,&previous);
    }
    auto aliasRatio=[&](const std::vector<float>& signal) {
        double energy=0,wanted=0,base=0;
        for(float x:signal)energy+=x*x/double(count);
        for(unsigned harmonic=1;harmonic*fundamental<count/2;harmonic+=2) {
            double re=0,im=0;
            for(unsigned i=0;i<count;++i) {
                double phase=6.283185307179586*harmonic*fundamental*i/count;
                re+=signal[i]*std::cos(phase)/count;im+=signal[i]*std::sin(phase)/count;
            }
            double power=2*(re*re+im*im);wanted+=power;if(harmonic==1)base=power;
        }
        return (energy-wanted)/base;
    };
    EXPECT_LT(aliasRatio(antialiased),aliasRatio(naive));
}

TEST(Palette, RealtimeChainMatchesTheSharedRenderProcessorForEveryAlgorithmAndQuality) {
    struct NoPlugins : RealtimePluginLoader {
        PtfxHostApi api={};
        bool load(const Node&,RealtimePlugin&,RealtimeArena&) override {return false;}
        const PtfxHostApi* hostApi() override {return &api;}
    } loader;
    constexpr unsigned frames=4099;
    std::vector<int16_t> source[2]={std::vector<int16_t>(frames),std::vector<int16_t>(frames)};
    for(unsigned c=0;c<2;++c)for(unsigned i=0;i<frames;++i)source[c][i]=int16_t(std::sin(i*(.031f+c*.013f))*7000);
    for(unsigned id=32;id<36;++id)for(unsigned type=0;type<6;++type)for(unsigned q=0;q<3;++q)
    for(unsigned channels=1;channels<=SAMPLE_CHANNELS;++channels) {
        SCOPED_TRACE(::testing::Message()<<id<<":"<<type<<" quality="<<q<<" channels="<<channels);
        ChainDefinition definition; auto n=palette::defaults(id);n.params[0]=type;n.params[1]=70;n.params[2]=60;n.params[3]=40;n.params[5]=0;n.quality=q;
        definition.add(n);Arena arena;
        RealtimeChain chain;Audio audio{{source[0].data(),source[1].data()},frames,uint8_t(channels)};
        ASSERT_TRUE(chain.prepare(definition,audio,arena.bytes.data(),arena.bytes.size(),loader,frames+64));
        std::vector<int16_t> actual;
        int16_t left[128],right[128];
        while(!chain.finished()) {
            unsigned count=chain.process(left,right,128);ASSERT_GT(count,0u);
            for(unsigned i=0;i<count;++i) {
                actual.push_back(left[i]);
                if(channels==2)actual.push_back(right[i]);
                else EXPECT_EQ(left[i],right[i]);
            }
        }
        EXPECT_EQ(actual,run(n,channels,frames));
    }
}

TEST(Palette, ProcessingAllocatesNothingAndNeutralFilterPreservesAudio) {
    Arena a;palette::Processor filter;auto n=palette::defaults(35);
    ASSERT_TRUE(filter.prepare(n,1,1024,a.memory()));size_t prepared=a.position;
    int16_t input[128],output[128];for(unsigned i=0;i<128;++i)input[i]=int16_t(i*411-25000);
    int16_t* in[]={input,nullptr};int16_t* out[]={output,nullptr};
    for(unsigned b=0;b<8;++b)ASSERT_TRUE(filter.process(in,out,128));
    EXPECT_EQ(a.position,prepared);EXPECT_EQ(std::memcmp(input,output,sizeof(input)),0);
}
