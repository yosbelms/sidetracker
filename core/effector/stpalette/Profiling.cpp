#include "Profiling.h"
#include <algorithm>
namespace samplefx { namespace stpalette {
ProfileResult profile(Node n,unsigned channels,Memory mem,void* context,uint32_t(*clock)(void*)) {
    ProfileResult result;
    if(!valid(n) || !clock || channels<1 || channels>2)return result;
    auto* buffers=static_cast<int16_t*>(mem.get(4*128*sizeof(int16_t)));
    if(!buffers)return result;
    int16_t* input[]={buffers,buffers+128};
    int16_t* output[]={buffers+256,buffers+384};
    uint32_t random=0x12345678;
    for(unsigned c=0;c<channels;++c)for(unsigned i=0;i<128;++i) {
        random^=random<<13;random^=random>>17;random^=random<<5;
        input[c][i]=int16_t((random&16383)-8192);
    }
    n.params[Tail]=0;
    Processor processor;
    if(!processor.prepare(n,channels,128*160,mem))return result;
    for(unsigned block=0;block<160;++block) {
        // Warm state before measuring and sweep the continuous controls.
        n.params[Amount]=block<64?10:block<112?60:100;
        n.params[Macro]=block<64?10:block<112?60:100;
        n.params[Drift]=block<64?0:block<112?50:100;
        if(!processor.parameters(n))return result;
        uint32_t start=clock(context);
        if(!processor.process(input,output,128))return result;
        uint32_t duration=clock(context)-start;
        if(block>=32){result.maximumTicks=std::max(result.maximumTicks,duration);result.totalTicks+=duration;++result.blocks;}
        for(unsigned c=0;c<channels;++c)for(unsigned i=0;i<128;++i) {result.signature^=uint16_t(output[c][i]);result.signature*=16777619u;}
    }
    result.success=true;
    return result;
}
} }
