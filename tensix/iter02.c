// Iteration 02: Read NOC/NIU information and write to L1
// Host will read the data from L1

#include <stdint.h>

// Memory layout
#define DATA_BASE   0x1000
#define READY_ADDR  0x1100

// NOC register addresses
#define NOC0_BASE   0xFFB20000
#define NOC1_BASE   0xFFB30000

#define NOC_NODE_ID_OFFSET      0x44
#define NOC_ENDPOINT_ID_OFFSET  0x48
#define NOC_ID_LOGICAL_OFFSET   0x148

// Data structure we'll write to L1
typedef struct {
    uint32_t noc0_node_id;
    uint32_t noc0_endpoint_id;
    uint32_t noc0_id_logical;
    uint32_t noc1_node_id;
    uint32_t noc1_endpoint_id;
    uint32_t noc1_id_logical;
} noc_info_t;

void _start(void) __attribute__((section(".start")));

void _start(void)
{
    volatile noc_info_t* data = (volatile noc_info_t*)DATA_BASE;
    volatile uint32_t* ready = (volatile uint32_t*)READY_ADDR;
    
    // Clear ready flag
    *ready = 0;
    __asm__ volatile ("fence" ::: "memory");
    
    // Read NOC 0 information
    data->noc0_node_id = *(volatile uint32_t*)(NOC0_BASE + NOC_NODE_ID_OFFSET);
    data->noc0_endpoint_id = *(volatile uint32_t*)(NOC0_BASE + NOC_ENDPOINT_ID_OFFSET);
    data->noc0_id_logical = *(volatile uint32_t*)(NOC0_BASE + NOC_ID_LOGICAL_OFFSET);
    
    // Read NOC 1 information
    data->noc1_node_id = *(volatile uint32_t*)(NOC1_BASE + NOC_NODE_ID_OFFSET);
    data->noc1_endpoint_id = *(volatile uint32_t*)(NOC1_BASE + NOC_ENDPOINT_ID_OFFSET);
    data->noc1_id_logical = *(volatile uint32_t*)(NOC1_BASE + NOC_ID_LOGICAL_OFFSET);
    
    // Fence to ensure all writes are visible
    __asm__ volatile ("fence" ::: "memory");
    
    // Set ready flag
    *ready = 0xC0DEC0DE;
    __asm__ volatile ("fence" ::: "memory");
    
    // Done - spin forever
    while (1) {
        // Just spin
    }
}

