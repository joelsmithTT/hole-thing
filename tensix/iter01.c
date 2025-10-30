// Iteration 01: Counter with host-controlled stop
// Tensix increments a counter and polls a control word

#include <stdint.h>

#define COUNTER_ADDR  0x1000  // Tensix writes, host reads
#define CONTROL_ADDR  0x1004  // Host writes, Tensix reads
#define MARKER_ADDR   0x1008  // Debug marker

#define CONTROL_RUN   0x00000000
#define CONTROL_STOP  0xDEADC0DE

void _start(void) __attribute__((section(".start")));

void _start(void)
{
    volatile uint32_t* counter = (volatile uint32_t*)COUNTER_ADDR;
    volatile uint32_t* control = (volatile uint32_t*)CONTROL_ADDR;
    volatile uint32_t* marker = (volatile uint32_t*)MARKER_ADDR;
    
    // Write marker to show we started
    *marker = 0xABCD1234;
    
    // Initialize
    uint32_t count = 0;
    
    // Loop: increment counter and check control word
    while (*control == CONTROL_RUN) {
        *counter = count;
        __asm__ volatile ("fence" ::: "memory");  // Ensure write is visible
        count++;
    }
    
    // Write final counter value and done marker
    *counter = count;
    *marker = 0xDEADBEEF;
    
    // Spin forever
    while (1) {
        // Just spin
    }
}

