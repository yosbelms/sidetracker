#include <stdint.h>
#include <stdlib.h>

extern uint32_t _stack_top, _data_load, _data_start, _data_end, _bss_start, _bss_end;
extern void __libc_init_array(void);
extern void initialise_monitor_handles(void);
extern int main(void);
void* __dso_handle;

void _init(void) {}
void _fini(void) {}

void reset(void)
{
    // Enable the Cortex-M4's single-precision FPU before calling C++ code.
    *(volatile uint32_t*)0xe000ed88 |= 0x00f00000;
    __asm__ volatile("dsb\nisb" ::: "memory");
    uint32_t* source = &_data_load;
    for (uint32_t* dest = &_data_start; dest < &_data_end;) *dest++ = *source++;
    for (uint32_t* dest = &_bss_start; dest < &_bss_end;) *dest++ = 0;
    initialise_monitor_handles();
    __libc_init_array();
    exit(main());
}

__attribute__((section(".isr_vector"), used))
const uintptr_t vectors[] = {(uintptr_t)&_stack_top, (uintptr_t)reset};
