// Iteration 05: Four-hop DMA via GDDR
// PCIe -> L1 -> GDDR -> L1 -> PCIe

#include <stdint.h>

// Memory layout
#define SRC_BUF_ADDR_LO   0x1000
#define SRC_BUF_ADDR_MID  0x1004
#define SRC_BUF_ADDR_HI   0x1008
#define DST_BUF_ADDR_LO   0x100C
#define DST_BUF_ADDR_MID  0x1010
#define DST_BUF_ADDR_HI   0x1014
#define TRANSFER_SIZE     0x1018
#define READY_ADDR        0x101C
#define DEBUG_SRC_LO      0x1020
#define DEBUG_SRC_MID     0x1024
#define DEBUG_DST_LO      0x1028
#define DEBUG_DST_MID     0x102C
#define DEBUG_NODE_ID     0x1030
#define DEBUG_LOCAL_COORD 0x1034

// L1 staging buffer
#define L1_STAGING_BASE  0x20000   // Start at 128KB to avoid conflicts
#define L1_STAGING_SIZE  (512 * 1024)  // 512KB staging

// NOC transfer limit
#define NOC_MAX_TRANS_SIZE 16384

// GDDR target
#define GDDR_X      17
#define GDDR_Y      12
#define GDDR_ADDR   0x0  // Start at 0

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

#define NOC_CMD_RD         0x0
#define NOC_CMD_WR         0x2
#define NOC_CMD_RESP_MARKED (1 << 4)

void _start(void) __attribute__((section(".start"), naked));
void main(void) __attribute__((noreturn));

static inline void noc_wait_ready(void)
{
    volatile uint32_t* cmd_ctrl = (volatile uint32_t*)NOC_CMD_CTRL;
    while (*cmd_ctrl & 1);
}

// NOC read: remote -> local L1
static void noc_read(uint64_t src_addr, uint32_t src_x, uint32_t src_y,
                     uint32_t dst_local_addr, uint32_t size, uint32_t local_coord)
{
    while (size > 0) {
        uint32_t chunk = (size > NOC_MAX_TRANS_SIZE) ? NOC_MAX_TRANS_SIZE : size;

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

        *targ_lo = (uint32_t)(src_addr & 0xFFFFFFFF);
        *targ_mid = (uint32_t)(src_addr >> 32);
        *targ_hi = (src_y << 6) | src_x;

        *ret_lo = dst_local_addr;
        *ret_mid = 0;
        *ret_hi = local_coord;

        *len = chunk;
        *len_1 = 0;
        *pkt_tag = 0;

        *ctrl = NOC_CMD_RD | NOC_CMD_RESP_MARKED;
        *cmd_ctrl = 1;

        src_addr += chunk;
        dst_local_addr += chunk;
        size -= chunk;
    }
}

static void noc_wait_reads_flushed(void)
{
    // Wait for all outstanding transactions on ID 0 to complete.
    // NIU_MST_REQS_OUTSTANDING_ID(0) is at offset 0x200 + 16*4 = 0x240
    volatile uint32_t* outstanding = (volatile uint32_t*)(NOC0_BASE + 0x240);
    while (*outstanding > 0);
}

// NOC write: local L1 -> remote
static void noc_write(uint32_t src_local_addr,
                      uint64_t dst_addr, uint32_t dst_x, uint32_t dst_y,
                      uint32_t size, uint32_t local_coord)
{
    while (size > 0) {
        uint32_t chunk = (size > NOC_MAX_TRANS_SIZE) ? NOC_MAX_TRANS_SIZE : size;

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

        *len = chunk;
        *len_1 = 0;
        *pkt_tag = 0;
        *brcst = 0;

        *ctrl = NOC_CMD_WR | NOC_CMD_RESP_MARKED;
        *cmd_ctrl = 1;

        src_local_addr += chunk;
        dst_addr += chunk;
        size -= chunk;
    }
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
    volatile uint32_t* src_addr_lo = (volatile uint32_t*)SRC_BUF_ADDR_LO;
    volatile uint32_t* src_addr_mid = (volatile uint32_t*)SRC_BUF_ADDR_MID;
    volatile uint32_t* src_addr_hi = (volatile uint32_t*)SRC_BUF_ADDR_HI;
    volatile uint32_t* dst_addr_lo = (volatile uint32_t*)DST_BUF_ADDR_LO;
    volatile uint32_t* dst_addr_mid = (volatile uint32_t*)DST_BUF_ADDR_MID;
    volatile uint32_t* dst_addr_hi = (volatile uint32_t*)DST_BUF_ADDR_HI;
    volatile uint32_t* xfer_size = (volatile uint32_t*)TRANSFER_SIZE;
    volatile uint32_t* ready = (volatile uint32_t*)READY_ADDR;

    *ready = 0xAAAAAAAA;
    __asm__ volatile ("fence" ::: "memory");

    // Get local coordinates
    volatile uint32_t* node_id_reg = (volatile uint32_t*)NOC_NODE_ID;
    uint32_t node_id = *node_id_reg;
    uint32_t local_x = node_id & 0x3F;
    uint32_t local_y = (node_id >> 6) & 0x3F;
    uint32_t local_coord = (local_y << 6) | local_x;

    // Read parameters
    uint64_t src_buf = ((uint64_t)*src_addr_mid << 32) | *src_addr_lo;
    uint32_t src_x = *src_addr_hi & 0x3F;
    uint32_t src_y = (*src_addr_hi >> 6) & 0x3F;

    uint64_t dst_buf = ((uint64_t)*dst_addr_mid << 32) | *dst_addr_lo;
    uint32_t dst_x = *dst_addr_hi & 0x3F;
    uint32_t dst_y = (*dst_addr_hi >> 6) & 0x3F;

    uint32_t total_size = *xfer_size;

    // Debug: write addresses for host to inspect
    volatile uint32_t* debug_src_lo = (volatile uint32_t*)DEBUG_SRC_LO;
    volatile uint32_t* debug_src_mid = (volatile uint32_t*)DEBUG_SRC_MID;
    volatile uint32_t* debug_dst_lo = (volatile uint32_t*)DEBUG_DST_LO;
    volatile uint32_t* debug_dst_mid = (volatile uint32_t*)DEBUG_DST_MID;
    volatile uint32_t* debug_node_id = (volatile uint32_t*)DEBUG_NODE_ID;
    volatile uint32_t* debug_local_coord = (volatile uint32_t*)DEBUG_LOCAL_COORD;

    // Debug: write node_id and local_coord to dedicated locations
    *debug_node_id = node_id;
    *debug_local_coord = local_coord;
    __asm__ volatile ("fence" ::: "memory");

    // Debug: write total_size
    *debug_src_lo = total_size;
    __asm__ volatile ("fence" ::: "memory");

    *ready = 0x11111111;  // Phase 1
    __asm__ volatile ("fence" ::: "memory");

    // Phase 1: PCIe -> L1 -> GDDR (in chunks)
    uint32_t transferred = 0;
    uint32_t num_chunks = 0;
    while (transferred < total_size) {
        num_chunks++;
        uint32_t chunk = total_size - transferred;
        if (chunk > L1_STAGING_SIZE) chunk = L1_STAGING_SIZE;

        // Debug: save first read address for inspection
        if (num_chunks == 1) {
            *debug_dst_mid = (uint32_t)((src_buf + transferred) >> 32);
        }

        // Read from PCIe to L1
        noc_read(src_buf + transferred, src_x, src_y, L1_STAGING_BASE, chunk, local_coord);
        noc_wait_ready();
        noc_wait_reads_flushed();

        // Write from L1 to GDDR
        noc_write(L1_STAGING_BASE, GDDR_ADDR + transferred, GDDR_X, GDDR_Y, chunk, local_coord);
        noc_wait_ready();
        noc_wait_reads_flushed();

        transferred += chunk;
    }

    // Debug: write number of chunks done in phase 1
    *debug_src_mid = num_chunks;
    noc_wait_reads_flushed();

    *ready = 0x22222222;  // Phase 2
    __asm__ volatile ("fence" ::: "memory");

    // Phase 2: GDDR -> L1 -> PCIe (in chunks)
    transferred = 0;
    num_chunks = 0;
    while (transferred < total_size) {
        num_chunks++;
        uint32_t chunk = total_size - transferred;
        if (chunk > L1_STAGING_SIZE) chunk = L1_STAGING_SIZE;

        // Read from GDDR to L1
        noc_read(GDDR_ADDR + transferred, GDDR_X, GDDR_Y, L1_STAGING_BASE, chunk, local_coord);
        noc_wait_ready();
        noc_wait_reads_flushed();

        // Write from L1 to PCIe
        noc_write(L1_STAGING_BASE, dst_buf + transferred, dst_x, dst_y, chunk, local_coord);
        noc_wait_ready();
        noc_wait_reads_flushed();

        transferred += chunk;
    }

    noc_wait_reads_flushed();
    // Debug: write number of chunks done in phase 2
    *debug_dst_mid = num_chunks;

    *ready = 0xC0DEC0DE;
    __asm__ volatile ("fence" ::: "memory");

    while (1);
}

