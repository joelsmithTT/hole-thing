// X280 Hello World - bare metal firmware for L3 LIM
//
// This runs from L3 LIM (scratchpad) starting at 0x0800_0000.
// The host loads this binary, sets the reset vector, and brings us out of reset.
// We write magic values to known locations so the host can verify we ran.

#include <stdint.h>

// Communication area at end of code - host can read/write these
// We put them at a fixed offset from LIM base so the host knows where to look
#define MAILBOX_BASE    0x08001000  // 4K into LIM

struct mailbox {
    volatile uint64_t magic;        // 0x00: Set to MAGIC when firmware is alive
    volatile uint64_t heartbeat;    // 0x08: Incremented in main loop
    volatile uint64_t command;      // 0x10: Host writes commands here
    volatile uint64_t response;     // 0x18: Firmware writes responses here
    volatile uint64_t hart_id;      // 0x20: Which hart is running
    volatile uint64_t scratch[3];   // 0x28-0x3F: General purpose
};

#define MAILBOX     ((struct mailbox *)MAILBOX_BASE)
#define MAGIC       0x5846696E694C4548ULL  // "HeLiFinX" (Hello L2CPU Firmware X280)

// Read hart ID from mhartid CSR
static inline uint64_t read_mhartid(void) {
    uint64_t val;
    __asm__ volatile ("csrr %0, mhartid" : "=r"(val));
    return val;
}

// Entry point - must be at 0x0800_0000
void _start(void) __attribute__((section(".start"), naked));

void _start(void) {
    // Set up stack pointer (sp = top of stack region)
    // We're in machine mode, no need for fancy initialization
    __asm__ volatile (
        "la sp, __stack_top\n"
        "j main\n"
    );
}

void main(void) {
    // Record which hart we are
    MAILBOX->hart_id = read_mhartid();
    
    // Clear command/response
    MAILBOX->command = 0;
    MAILBOX->response = 0;
    
    // Initialize heartbeat
    MAILBOX->heartbeat = 0;
    
    // Memory barrier to ensure above writes are visible
    __asm__ volatile ("fence w, w" ::: "memory");
    
    // Write magic to signal we're alive (host polls for this)
    MAILBOX->magic = MAGIC;
    __asm__ volatile ("fence w, w" ::: "memory");
    
    // Main loop - just increment heartbeat forever
    while (1) {
        MAILBOX->heartbeat++;
        
        // Check for command from host
        uint64_t cmd = MAILBOX->command;
        if (cmd != 0) {
            // Simple echo for now - just copy command to response
            MAILBOX->response = cmd;
            MAILBOX->command = 0;
            __asm__ volatile ("fence w, w" ::: "memory");
        }
        
        // Small delay to avoid burning too hot
        for (volatile int i = 0; i < 1000; i++) {
            __asm__ volatile ("nop");
        }
    }
}

