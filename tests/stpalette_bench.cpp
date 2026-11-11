#include "stpalette/Profiling.h"
#include <cstdio>
#include <cstring>
#ifndef CE_BENCH_M4
#include <chrono>
#endif
namespace {
alignas(32) unsigned char storage[768*1024];
size_t used=0;
void* allocate(void*,size_t bytes) {
    size_t at=(used+31)&~size_t(31);
    if(at+bytes>sizeof(storage))return nullptr;
    used=at+bytes;std::memset(storage+at,0,bytes);return storage+at;
}
uint32_t clock(void*) {
#ifdef CE_BENCH_M4
    return 0xffffffffu-*reinterpret_cast<volatile uint32_t*>(0x40000004);
#else
    return uint32_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}
}
int main() {
#ifdef CE_BENCH_M4
    *reinterpret_cast<volatile uint32_t*>(0x40000008)=0xffffffffu;
    *reinterpret_cast<volatile uint32_t*>(0x40000000)=1;
    const char* unit="qemu_timer_ticks";
#else
    const char* unit="host_ns";
#endif
    bool ok=true;
    for(unsigned id=32;id<=35;++id)for(unsigned type=0;type<6;++type)for(unsigned quality=0;quality<3;++quality) {
        used=0;auto n=samplefx::stpalette::defaults(id);n.params[0]=type;n.quality=quality;
        auto result=samplefx::stpalette::profile(n,1,{nullptr,allocate},nullptr,clock);
        ok&=result.success;
        printf("{\"category\":%u,\"type\":%u,\"quality\":%u,\"unit\":\"%s\",\"max\":%lu,\"average\":%lu,\"arena\":%lu,\"signature\":%lu,\"ok\":%s}\n",id,type,quality,unit,(unsigned long)result.maximumTicks,(unsigned long)(result.blocks?result.totalTicks/result.blocks:0),(unsigned long)used,(unsigned long)result.signature,result.success?"true":"false");
    }
    return ok?0:1;
}
