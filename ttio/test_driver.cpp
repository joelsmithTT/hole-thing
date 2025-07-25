#include "ttio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ttio.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <random>

#include <thread>
// The read and write pointers wrap at 2x the queue size.
// Each request or response is an array of 8 32-bit words.

struct Message
{
    uint32_t type;
    uint32_t data[7];
};
static_assert(sizeof(Message) == 32);

uint32_t QUEUE_HEADER_SIZE = 32;
uint16_t ARC_X = 8;
uint16_t ARC_Y = 0;

#define req_w 0x00
#define res_r 0x04
#define req_r 0x10
#define res_w 0x14


void push_arc_msg(tt::Device& device, const Message& message, uint64_t queue_base_addr, uint32_t num_entries)
{
    uint64_t request_base = queue_base_addr + QUEUE_HEADER_SIZE;
    // 1. Read request wptr and rptr, loop reading the rptr until queue has space. 

    uint32_t wptr = device.noc_read32(ARC_X, ARC_Y, queue_base_addr + req_w);
    for (;;) {
        uint32_t rptr = device.noc_read32(ARC_X, ARC_Y, queue_base_addr + req_r);
        uint32_t num_occupied = (wptr - rptr) % (2 * num_entries);
        if (num_occupied < num_entries)
            break;
    }

    // 2. Write the request queue at index wptr % size. 
    uint32_t slot = wptr % num_entries;
    uint32_t request_offset = slot * sizeof(Message);
    const uint32_t *ptr = reinterpret_cast<const uint32_t*>(&message);
    for (size_t i = 0; i < 8; ++i) {
        auto addr = request_base + request_offset + i * sizeof(uint32_t);
        device.noc_write32(ARC_X, ARC_Y, addr, ptr[i]);
    }

    // 3. Update the request wptr by (request wptr + 1) % (2*size) 
    wptr = (wptr + 1) % (2 * num_entries);
    device.noc_write32(ARC_X, ARC_Y, queue_base_addr + 0x0, wptr);
}

void pop_arc_msg(tt::Device& device, Message& message, uint64_t queue_base_addr, uint32_t num_entries)
{
    uint32_t response_base = queue_base_addr + QUEUE_HEADER_SIZE + (num_entries * sizeof(Message));

    // 1. Read response wptr and rptr, loop reading the wptr until the queue has an entry. 
    uint32_t rptr = device.noc_read32(ARC_X, ARC_Y, queue_base_addr + res_r);

    for (;;) {
        uint32_t wptr = device.noc_read32(ARC_X, ARC_Y, queue_base_addr + res_w);
        uint32_t num_occupied = (wptr - rptr) % (2 * num_entries);
        if (num_occupied > 0)
            break;
    }

    uint32_t slot = rptr % num_entries;
    uint32_t response_offset = slot * sizeof(Message);
    uint32_t* ptr = reinterpret_cast<uint32_t*>(&message);
    for (size_t i = 0; i < 8; ++i) {
        auto addr = response_base + response_offset + i * sizeof(uint32_t);
        ptr[i] = device.noc_read32(ARC_X, ARC_Y, addr);
    }

    rptr = (rptr + 1) % (2 * num_entries);
    device.noc_write32(ARC_X, ARC_Y, queue_base_addr + res_r, rptr);
}

void blackhole_arc_message(tt::Device& device, uint32_t message_type)
{
    if (device.is_wormhole())
        return;

    constexpr uint64_t SCRATCH_RAM_11 = 0x8003042C;
    constexpr uint8_t queue_index = 0;
    uint16_t ARC_X = 8;
    uint16_t ARC_Y = 0;

    // qcb = queue control block
    uint32_t qcb_addr = device.noc_read32(ARC_X, ARC_Y, SCRATCH_RAM_11);
    uint64_t q_base = device.noc_read32(ARC_X, ARC_Y, qcb_addr);
    uint64_t q_dims = device.noc_read32(ARC_X, ARC_Y, qcb_addr + 4);
    uint32_t num_entries = q_dims & 0xFF;   // per queue
    uint32_t num_queues = (q_dims >> 8) & 0xFF;
    uint32_t q_size = (2 * num_entries * sizeof(Message)) + QUEUE_HEADER_SIZE;
    uint32_t msg_queue_base = q_base + queue_index * q_size;


    // printf("ARC Message Queue Base Address: 0x%x\n", msg_queue_base);
    // printf("Number of Entries per Queue: %u\n", num_entries);
    // printf("Number of Queues: %u\n", num_queues);


    push_arc_msg(device, {message_type, 0, 0, 0, 0, 0, 0, 0}, msg_queue_base, num_entries);
    constexpr uint32_t ARC_FW_INT_ADDR = 0x80030100;
    constexpr uint32_t ARC_FW_INT_VAL = 65536;
    device.noc_write32(ARC_X, ARC_Y, ARC_FW_INT_ADDR, ARC_FW_INT_VAL);

    Message message;
    pop_arc_msg(device, message, msg_queue_base, num_entries);

    printf("ARC Message: %u (0x%x)\n", message.type, message.type);
    printf("ARC Message Data[0]: %u (0x%x)\n", message.data[0], message.data[0]);
    printf("ARC Message Data[1]: %u (0x%x)\n", message.data[1], message.data[1]);
    printf("ARC Message Data[2]: %u (0x%x)\n", message.data[2], message.data[2]);
    printf("ARC Message Data[3]: %u (0x%x)\n", message.data[3], message.data[3]);
    printf("ARC Message Data[4]: %u (0x%x)\n", message.data[4], message.data[4]);
    printf("ARC Message Data[5]: %u (0x%x)\n", message.data[5], message.data[5]);
    printf("ARC Message Data[6]: %u (0x%x)\n", message.data[6], message.data[6]);
}

void blackhole_noc_sanity_check(tt::Device& device)
{
    static constexpr uint64_t NOC_NODE_ID_LOGICAL = 0xffb20148ULL;

    if (!device.is_blackhole())
        return;

    auto is_tensix_bh = [](uint32_t x, uint32_t y) -> bool {
        return (y >= 2 && y <= 11) &&   // Valid y range
            ((x >= 1 && x <= 7) ||      // Left block
            (x >= 10 && x <= 16));      // Right block
    };

    auto [size_x, size_y] = device.get_noc_grid_size();
    for (uint32_t x = 0; x < size_x; ++x) {
        for (uint32_t y = 0; y < size_y; ++y) {

            if (!is_tensix_bh(x, y))
                continue;

            uint32_t node_id = device.noc_read32(x, y, NOC_NODE_ID_LOGICAL);
            uint32_t node_id_x = (node_id >> 0x0) & 0x3f;
            uint32_t node_id_y = (node_id >> 0x6) & 0x3f;

            if (node_id_x != x || node_id_y != y) {
                printf("Node ID mismatch, expected (%u, %u), got (%u, %u)\n", x, y, node_id_x, node_id_y);
                exit(1);
            }
        }
    }
    printf("Blackhole NOC sanity test PASSED\n");
    uint32_t pcie_enum_time = device.noc_read32(8, 0, 0x80030438ULL);
    float in_ms = pcie_enum_time / 50000.0f;
    printf("PCIE enum time: %f ms\n", in_ms);
}

void wormhole_noc_sanity_check(tt::Device& device)
{
    if (!device.is_wormhole())
        return;

    {
        constexpr uint32_t ARC_X = 0;
        constexpr uint32_t ARC_Y = 10;
        constexpr uint64_t ARC_NOC_NODE_ID = 0xFFFB2002CULL;

        auto node_id = device.noc_read32(ARC_X, ARC_Y, ARC_NOC_NODE_ID);
        auto x = (node_id >> 0x0) & 0x3f;
        auto y = (node_id >> 0x6) & 0x3f;
        if (x != ARC_X || y != ARC_Y) {
            printf("ARC node ID mismatch, expected (%u, %u), got (%u, %u)\n", ARC_X, ARC_Y, x, y);
            exit(1);
        }
    }

    {
        constexpr uint32_t DDR_X = 0;
        constexpr uint32_t DDR_Y = 11;
        constexpr uint64_t DDR_NOC_NODE_ID = 0x10009002CULL;

        auto node_id = device.noc_read32(DDR_X, DDR_Y, DDR_NOC_NODE_ID);
        auto x = (node_id >> 0x0) & 0x3f;
        auto y = (node_id >> 0x6) & 0x3f;
        if (x != DDR_X || y != DDR_Y) {
            printf("DDR node ID mismatch, expected (%u, %u), got (%u, %u)\n", DDR_X, DDR_Y, x, y);
            exit(1);
        }
    }

    constexpr uint64_t TENSIX_NOC_NODE_ID = 0xffb2002cULL;
    auto is_tensix_wh = [](uint32_t x, uint32_t y) -> bool {
        return (((y != 6) && (y >= 1) && (y <= 11)) && // valid Y
                ((x != 5) && (x >= 1) && (x <= 9)));   // valid X
    };
    for (uint32_t x = 0; x < 12; ++x) {
        for (uint32_t y = 0; y < 12; ++y) {
            if (!is_tensix_wh(x, y)) {
                continue;
            }
            auto node_id = device.noc_read32(x, y, TENSIX_NOC_NODE_ID);
            auto node_id_x = (node_id >> 0x0) & 0x3f;
            auto node_id_y = (node_id >> 0x6) & 0x3f;

            if (node_id_x != x || node_id_y != y) {
                printf("Expected x: %u, y: %u, got x: %u, y: %u\n", x, y, node_id_x, node_id_y);
            }
        }
    }

    printf("Wormhole NOC sanity test PASSED\n");
}

void fill_with_random_data(void* ptr, size_t bytes)
{
    if (bytes == 0)
        return;

    static std::mt19937_64 eng(std::random_device{}());

    size_t num_uint64 = bytes / sizeof(uint64_t);
    size_t rem_bytes = bytes % sizeof(uint64_t);

    uint64_t* ptr64 = static_cast<uint64_t*>(ptr);
    for (size_t i = 0; i < num_uint64; ++i) {
        ptr64[i] = eng();
    }

    if (rem_bytes > 0) {
        uint8_t* ptr8 = reinterpret_cast<uint8_t*>(ptr64 + num_uint64);
        uint64_t last_random_chunk = eng();
        unsigned char* random_bytes_for_remainder = reinterpret_cast<unsigned char*>(&last_random_chunk);
        for (size_t i = 0; i < rem_bytes; ++i) {
            ptr8[i] = random_bytes_for_remainder[i];
        }
    }
}

void test_noc_dma(tt::Device& device, size_t magnitude)
{
    size_t buffer_size = 1ULL << magnitude;
    tt::DmaBuffer buffer(device.handle(), buffer_size);
    uint8_t* data = buffer.get_mem<uint8_t>();
    uint64_t noc_addr = buffer.get_noc_addr();

    std::vector<uint8_t> pattern(buffer_size);
    fill_with_random_data(pattern.data(), buffer_size);

    auto [x, y] = device.get_pcie_coordinates();
    printf("Writing to x=%u, y=%u, noc_addr=0x%lx\n", x, y, noc_addr);
    device.noc_write(x, y, noc_addr, pattern.data(), buffer_size);

    if (memcmp(data, pattern.data(), buffer_size) != 0) {
        printf("Data mismatch\n");
        return;
    }

    printf("NOC DMA test PASSED (size=0x%lx)\n", buffer_size);
}

void test_telemetry(tt::Device& device)
{
    if (device.is_blackhole()) {
        std::map<const char*, uint32_t> telemetry_tags = {
            {" AI Clock (MHz)", 14},
            {"Fan Speed (RPM)", 41},
        };
        for (auto [name, tag] : telemetry_tags) {
            uint32_t telemetry = device.read_bh_telemetry(tag);
            printf("Blackhole telemetry: %s = %u\n", name, telemetry);
        }
    }
}


void run_tests(tt::Device& device)
{
    uint32_t MSG_TYPE_SEND_PCIE_MSI = 0x17;
    uint32_t MSG_TYPE_GET_AICLK = 0x34;
    uint32_t MSG_TYPE_AICLK_GO_BUSY = 0x52;
    uint32_t MSG_TYPE_AICLK_GO_LONG_IDLE = 0x54;
    blackhole_arc_message(device, MSG_TYPE_GET_AICLK);
    blackhole_arc_message(device, MSG_TYPE_SEND_PCIE_MSI);
    blackhole_arc_message(device, MSG_TYPE_AICLK_GO_BUSY);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    blackhole_arc_message(device, MSG_TYPE_GET_AICLK);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    blackhole_arc_message(device, MSG_TYPE_AICLK_GO_LONG_IDLE);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    blackhole_arc_message(device, MSG_TYPE_GET_AICLK);

    // blackhole_noc_sanity_check(device);
    // wormhole_noc_sanity_check(device);

    // test_telemetry(device);

    // test_noc_dma(device, 21);
    // test_noc_dma(device, 28);
    // test_noc_dma(device, 30);
}

#include <fstream>
void bh_debug(tt::Device& device)
{
    std::vector<uint8_t> buffer(3364);
    device.noc_read(8, 3, 0x400030100000ULL, buffer.data(), buffer.size());

    std::ofstream file("wh_debug.bin", std::ios::binary);
    file.write(reinterpret_cast<const char*>(buffer.data()), buffer.size()-1);
}

void bh_debug(tt::Device& device, uint64_t address, size_t size, std::string name)
{
    std::vector<uint8_t> buffer(size);
    device.noc_read(8, 3, address, buffer.data(), buffer.size());

    std::ofstream file(name, std::ios::binary);
    file.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
    printf("Wrote %s (size=0x%lx)\n", name.c_str(), size);
}

std::vector<std::string> enumerate_devices()
{
    std::vector<std::string> devices;
    for (const auto& entry : std::filesystem::directory_iterator("/dev/tenstorrent/")) {
        if (std::filesystem::is_character_file(entry.path()) || std::filesystem::is_block_file(entry.path())) {
            devices.push_back(entry.path().string());
        }
    }

    std::sort(devices.begin(), devices.end());
    return devices;
}

int main()
{
    for (auto device_path : enumerate_devices()) {
        tt::Device device(device_path.c_str());
        if (device.is_blackhole()) {
            // bh_debug(device, 0x400030000000ULL, 272552, "opensbi.bin");
            bh_debug(device, 0x400030200000ULL, 21934592, "kernel.bin");
            // bh_debug(device, 0x400030100000ULL, 3364, "dtb.bin");
        }
    }

    return 0;
}
