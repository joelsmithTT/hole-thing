// Iteration 04: NOC Read/Write with host DMA buffer
// Reads two uint16_t vectors from host, computes uint32_t sum, writes back

#include <stdint.h>

// Memory layout
#define HOST_BUF_ADDR_LO   0x1000  // Host writes NOC address (low 32 bits)
#define HOST_BUF_ADDR_MID  0x1004  // Host writes NOC address (mid 32 bits)
#define HOST_BUF_ADDR_HI   0x1008  // Host writes NOC address (hi 32 bits - coords)
#define READY_ADDR         0x100C  // Signal completion

// L1 working buffers
#define L1_V0_BASE   0x2000
#define L1_V1_BASE   0x2400
#define L1_SUM_BASE  0x2800

// Buffer sizes
#define NUM_ELEMENTS 512
#define V0_SIZE      (NUM_ELEMENTS * 2)  // 1024 bytes (uint16_t)
#define V1_SIZE      (NUM_ELEMENTS * 2)  // 1024 bytes (uint16_t)
#define SUM_SIZE     (NUM_ELEMENTS * 4)  // 2048 bytes (uint32_t)

// Host buffer offsets
#define HOST_V0_OFFSET   0x000
#define HOST_V1_OFFSET   0x400
#define HOST_SUM_OFFSET  0x800

// NOC registers (NoC 0, command buffer 0)
#define NOC0_BASE          0xFFB20000
#define NOC_TARG_ADDR_LO   (NOC0_BASE + 0x00)
#define NOC_TARG_ADDR_MID  (NOC0_BASE + 0x04)
#define NOC_TARG_ADDR_HI   (NOC0_BASE + 0x08)
#define NOC_RET_ADDR_LO    (NOC0_BASE + 0x0C)
#define NOC_RET_ADDR_MID   (NOC0_BASE + 0x10)
#define NOC_RET_ADDR_HI    (NOC0_BASE + 0x14)
#define NOC_PACKET_TAG     (NOC0_BASE + 0x18)
#define NOC_CTRL           (NOC0_BASE + 0x1C)
#define NOC_AT_LEN_BE      (NOC0_BASE + 0x20)
#define NOC_AT_LEN_BE_1    (NOC0_BASE + 0x24)
#define NOC_BRCST_EXCLUDE  (NOC0_BASE + 0x2C)
#define NOC_CMD_CTRL       (NOC0_BASE + 0x40)
#define NOC_NODE_ID        (NOC0_BASE + 0x44)

// NOC command bits
#define NOC_CMD_RD         0x0
#define NOC_CMD_WR         0x2
#define NOC_CMD_RESP_MARKED (1 << 4)

void _start(void) __attribute__((section(".start")));

// Wait for NOC command buffer to be ready
static inline void noc_wait_ready(void)
{
    volatile uint32_t* cmd_ctrl = (volatile uint32_t*)NOC_CMD_CTRL;
    while (*cmd_ctrl & 1) {
        // Spin until bit 0 clears
    }
}

// NOC read: remote -> local L1
static void noc_read(uint64_t src_addr, uint32_t src_x, uint32_t src_y,
                     uint32_t dst_local_addr, uint32_t size, uint32_t local_node_coord)
{
    noc_wait_ready();

    volatile uint32_t* targ_lo = (volatile uint32_t*)NOC_TARG_ADDR_LO;
    volatile uint32_t* targ_mid = (volatile uint32_t*)NOC_TARG_ADDR_MID;
    volatile uint32_t* targ_hi = (volatile uint32_t*)NOC_TARG_ADDR_HI;
    volatile uint32_t* ret_lo = (volatile uint32_t*)NOC_RET_ADDR_LO;
    volatile uint32_t* ret_mid = (volatile uint32_t*)NOC_RET_ADDR_MID;
    volatile uint32_t* ret_hi = (volatile uint32_t*)NOC_RET_ADDR_HI;
    volatile uint32_t* pkt_tag = (volatile uint32_t*)NOC_PACKET_TAG;
    volatile uint32_t* ctrl = (volatile uint32_t*)NOC_CTRL;
    volatile uint32_t* len = (volatile uint32_t*)NOC_AT_LEN_BE;
    volatile uint32_t* len_1 = (volatile uint32_t*)NOC_AT_LEN_BE_1;
    volatile uint32_t* cmd_ctrl = (volatile uint32_t*)NOC_CMD_CTRL;

    // Source: remote address + coordinates
    *targ_lo = (uint32_t)(src_addr & 0xFFFFFFFF);
    *targ_mid = (uint32_t)(src_addr >> 32);
    *targ_hi = (src_y << 6) | src_x;

    // Destination: local L1 address
    *ret_lo = dst_local_addr;
    *ret_mid = 0;
    *ret_hi = local_node_coord;

    // Length and tag
    *len = size;
    *len_1 = 0;
    *pkt_tag = 0;

    // Control: Read request with response marked
    *ctrl = NOC_CMD_RD | NOC_CMD_RESP_MARKED;

    // Issue command
    *cmd_ctrl = 1;
}

// NOC write: local L1 -> remote
static void noc_write(uint32_t src_local_addr,
                      uint64_t dst_addr, uint32_t dst_x, uint32_t dst_y,
                      uint32_t size, uint32_t local_node_coord)
{
    noc_wait_ready();

    volatile uint32_t* targ_lo = (volatile uint32_t*)NOC_TARG_ADDR_LO;
    volatile uint32_t* targ_mid = (volatile uint32_t*)NOC_TARG_ADDR_MID;
    volatile uint32_t* targ_hi = (volatile uint32_t*)NOC_TARG_ADDR_HI;
    volatile uint32_t* ret_lo = (volatile uint32_t*)NOC_RET_ADDR_LO;
    volatile uint32_t* ret_mid = (volatile uint32_t*)NOC_RET_ADDR_MID;
    volatile uint32_t* ret_hi = (volatile uint32_t*)NOC_RET_ADDR_HI;
    volatile uint32_t* pkt_tag = (volatile uint32_t*)NOC_PACKET_TAG;
    volatile uint32_t* ctrl = (volatile uint32_t*)NOC_CTRL;
    volatile uint32_t* len = (volatile uint32_t*)NOC_AT_LEN_BE;
    volatile uint32_t* len_1 = (volatile uint32_t*)NOC_AT_LEN_BE_1;
    volatile uint32_t* brcst = (volatile uint32_t*)NOC_BRCST_EXCLUDE;
    volatile uint32_t* cmd_ctrl = (volatile uint32_t*)NOC_CMD_CTRL;

    // Source: local L1 address
    *targ_lo = src_local_addr;
    *targ_mid = 0;
    *targ_hi = local_node_coord;

    // Destination: remote address + coordinates
    *ret_lo = (uint32_t)(dst_addr & 0xFFFFFFFF);
    *ret_mid = (uint32_t)(dst_addr >> 32);
    *ret_hi = (dst_y << 6) | dst_x;

    // Length, tag, broadcast
    *len = size;
    *len_1 = 0;
    *pkt_tag = 0;
    *brcst = 0;

    // Control: Write request (not inline) with response marked
    *ctrl = NOC_CMD_WR | NOC_CMD_RESP_MARKED;

    // Issue command
    *cmd_ctrl = 1;
}

void _start(void)
{
    volatile uint32_t* buf_addr_lo = (volatile uint32_t*)HOST_BUF_ADDR_LO;
    volatile uint32_t* buf_addr_mid = (volatile uint32_t*)HOST_BUF_ADDR_MID;
    volatile uint32_t* buf_addr_hi = (volatile uint32_t*)HOST_BUF_ADDR_HI;
    volatile uint32_t* ready = (volatile uint32_t*)READY_ADDR;

    // Clear ready flag
    *ready = 0;
    __asm__ volatile ("fence" ::: "memory");

    // Read local node coordinates
    volatile uint32_t* node_id_reg = (volatile uint32_t*)NOC_NODE_ID;
    uint32_t node_id = *node_id_reg;
    uint32_t local_x = node_id & 0x3F;
    uint32_t local_y = (node_id >> 6) & 0x3F;
    uint32_t local_node_coord = (local_y << 6) | local_x;

    // Read host buffer NOC address from L1
    uint32_t lo = *buf_addr_lo;
    uint32_t mid = *buf_addr_mid;
    uint32_t hi = *buf_addr_hi;

    uint64_t host_buf_addr = ((uint64_t)mid << 32) | lo;
    uint32_t host_x = hi & 0x3F;
    uint32_t host_y = (hi >> 6) & 0x3F;

    // Read v0 from host buffer to L1
    noc_read(host_buf_addr + HOST_V0_OFFSET, host_x, host_y, L1_V0_BASE, V0_SIZE, local_node_coord);
    noc_wait_ready();

    // Read v1 from host buffer to L1
    noc_read(host_buf_addr + HOST_V1_OFFSET, host_x, host_y, L1_V1_BASE, V1_SIZE, local_node_coord);
    noc_wait_ready();

    // Compute sum
    volatile uint16_t* v0 = (volatile uint16_t*)L1_V0_BASE;
    volatile uint16_t* v1 = (volatile uint16_t*)L1_V1_BASE;
    volatile uint32_t* sum = (volatile uint32_t*)L1_SUM_BASE;

    // Debug: write first values to a known location
    volatile uint32_t* debug = (volatile uint32_t*)0x3000;
    debug[0] = v0[0];  // Should be 0
    debug[1] = v0[1];  // Should be 1
    debug[2] = v1[0];  // Should be 0
    debug[3] = v1[1];  // Should be 2

    for (int i = 0; i < NUM_ELEMENTS; i++) {
        sum[i] = (uint32_t)v0[i] + (uint32_t)v1[i];
    }
    __asm__ volatile ("fence" ::: "memory");

    // Write sum back to host buffer
    noc_write(L1_SUM_BASE, host_buf_addr + HOST_SUM_OFFSET, host_x, host_y, SUM_SIZE, local_node_coord);
    noc_wait_ready();

    // Signal completion
    *ready = 0xC0DEC0DE;
    __asm__ volatile ("fence" ::: "memory");

    // Done - spin forever
    while (1) {
        // Just spin
    }
}

