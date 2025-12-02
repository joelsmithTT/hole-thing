// Iteration 06: Free-running counter to GDDR
// Continuously writes cycle counter to GDDR (17, 12) at address 0x0

#include <stdint.h>

// GDDR target
#define GDDR_X      17
#define GDDR_Y      12
#define GDDR_ADDR   0x0

// L1 staging address for counter
#define L1_COUNTER_ADDR  0x2000

// NOC registers
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

#define NOC_CMD_WR         0x2
#define NOC_CMD_RESP_MARKED (1 << 4)

// CSR addresses
#define CSR_MCYCLE  0xb00

void _start(void) __attribute__((section(".start"), naked));
void main(void) __attribute__((noreturn));

// Read mcycle CSR
static inline uint32_t read_mcycle(void)
{
    uint32_t val;
    __asm__ volatile ("csrr %0, 0xb00" : "=r"(val));
    return val;
}

static inline void noc_wait_ready(void)
{
    volatile uint32_t* cmd_ctrl = (volatile uint32_t*)NOC_CMD_CTRL;
    while (*cmd_ctrl & 1);
}

// NOC write: local L1 -> remote (4 bytes)
static void noc_write32(uint32_t src_local_addr,
                        uint64_t dst_addr, uint32_t dst_x, uint32_t dst_y,
                        uint32_t local_coord)
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

    *targ_lo = src_local_addr;
    *targ_mid = 0;
    *targ_hi = local_coord;

    *ret_lo = (uint32_t)(dst_addr & 0xFFFFFFFF);
    *ret_mid = (uint32_t)(dst_addr >> 32);
    *ret_hi = (dst_y << 6) | dst_x;

    *len = 4;  // 4 bytes
    *len_1 = 0;
    *pkt_tag = 0;
    *brcst = 0;

    *ctrl = NOC_CMD_WR | NOC_CMD_RESP_MARKED;
    *cmd_ctrl = 1;
}

void _start(void)
{
    __asm__ volatile (
        "lui sp, 0x180\n"
        "j main\n"
        : : : "sp"
    );
    __builtin_unreachable();
}

void main(void)
{
    // Get local coordinates
    volatile uint32_t* node_id_reg = (volatile uint32_t*)NOC_NODE_ID;
    uint32_t node_id = *node_id_reg;
    uint32_t local_coord = node_id & 0xFFF;

    // L1 address where we stage the counter value
    volatile uint32_t* counter_l1 = (volatile uint32_t*)L1_COUNTER_ADDR;

    // Run forever: read cycle counter, write to GDDR
    while (1) {
        // Read hardware cycle counter
        uint32_t cycles = read_mcycle();

        // Write to L1 staging area
        *counter_l1 = cycles;
        __asm__ volatile ("fence" ::: "memory");

        // NOC write to GDDR
        noc_write32(L1_COUNTER_ADDR, GDDR_ADDR, GDDR_X, GDDR_Y, local_coord);
        noc_wait_ready();
    }
}

