#include "holething.hpp"

#include <iostream>
#include <string>

using namespace tt;

// Pasted from tt-zephyr-platforms telemetry.h
#define TAG_BOARD_ID_HIGH        1
#define TAG_BOARD_ID_LOW         2
#define TAG_ASIC_ID              3
#define TAG_HARVESTING_STATE     4
#define TAG_UPDATE_TELEM_SPEED   5
#define TAG_VCORE                6
#define TAG_TDP                  7
#define TAG_TDC                  8
#define TAG_VDD_LIMITS           9
#define TAG_THM_LIMITS           10
#define TAG_ASIC_TEMPERATURE     11
#define TAG_VREG_TEMPERATURE     12
#define TAG_BOARD_TEMPERATURE    13
#define TAG_AICLK                14
#define TAG_AXICLK               15
#define TAG_ARCCLK               16
#define TAG_L2CPUCLK0            17
#define TAG_L2CPUCLK1            18
#define TAG_L2CPUCLK2            19
#define TAG_L2CPUCLK3            20
#define TAG_ETH_LIVE_STATUS      21
#define TAG_GDDR_STATUS          22
#define TAG_GDDR_SPEED           23
#define TAG_ETH_FW_VERSION       24
#define TAG_GDDR_FW_VERSION      25
#define TAG_DM_APP_FW_VERSION    26
#define TAG_DM_BL_FW_VERSION     27
#define TAG_FLASH_BUNDLE_VERSION 28
#define TAG_CM_FW_VERSION        29
#define TAG_L2CPU_FW_VERSION     30
#define TAG_FAN_SPEED            31
#define TAG_TIMER_HEARTBEAT      32
#define TAG_TELEM_ENUM_COUNT     33
#define TAG_ENABLED_TENSIX_COL   34
#define TAG_ENABLED_ETH          35
#define TAG_ENABLED_GDDR         36
#define TAG_ENABLED_L2CPU        37
#define TAG_PCIE_USAGE           38
#define TAG_INPUT_CURRENT        39
#define TAG_NOC_TRANSLATION      40
#define TAG_FAN_RPM              41
#define TAG_GDDR_0_1_TEMP        42
#define TAG_GDDR_2_3_TEMP        43
#define TAG_GDDR_4_5_TEMP        44
#define TAG_GDDR_6_7_TEMP        45
#define TAG_GDDR_0_1_CORR_ERRS   46
#define TAG_GDDR_2_3_CORR_ERRS   47
#define TAG_GDDR_4_5_CORR_ERRS   48
#define TAG_GDDR_6_7_CORR_ERRS   49
#define TAG_GDDR_UNCORR_ERRS     50
#define TAG_MAX_GDDR_TEMP        51
#define TAG_ASIC_LOCATION        52
#define TAG_BOARD_POWER_LIMIT    53
#define TAG_INPUT_POWER          54
#define TAG_THERM_TRIP_COUNT     60
#define TAG_ASIC_ID_HIGH         61
#define TAG_ASIC_ID_LOW          62


// Define a structure to hold the tag ID and its string name
struct TelemTag {
    uint32_t id;
    const char* name;
};

// Use a macro to create an entry for each tag
#define ADD_TELEMETRY_TAG(id) { id, #id }

int main()
{
    std::vector<TelemTag> telemetry_tags = {
        ADD_TELEMETRY_TAG(TAG_BOARD_ID_HIGH),
        ADD_TELEMETRY_TAG(TAG_BOARD_ID_LOW),
        ADD_TELEMETRY_TAG(TAG_ASIC_ID),
        ADD_TELEMETRY_TAG(TAG_HARVESTING_STATE),
        ADD_TELEMETRY_TAG(TAG_UPDATE_TELEM_SPEED),
        ADD_TELEMETRY_TAG(TAG_VCORE),
        ADD_TELEMETRY_TAG(TAG_TDP),
        ADD_TELEMETRY_TAG(TAG_TDC),
        ADD_TELEMETRY_TAG(TAG_VDD_LIMITS),
        ADD_TELEMETRY_TAG(TAG_THM_LIMITS),
        ADD_TELEMETRY_TAG(TAG_ASIC_TEMPERATURE),
        ADD_TELEMETRY_TAG(TAG_VREG_TEMPERATURE),
        ADD_TELEMETRY_TAG(TAG_BOARD_TEMPERATURE),
        ADD_TELEMETRY_TAG(TAG_AICLK),
        ADD_TELEMETRY_TAG(TAG_AXICLK),
        ADD_TELEMETRY_TAG(TAG_ARCCLK),
        ADD_TELEMETRY_TAG(TAG_L2CPUCLK0),
        ADD_TELEMETRY_TAG(TAG_L2CPUCLK1),
        ADD_TELEMETRY_TAG(TAG_L2CPUCLK2),
        ADD_TELEMETRY_TAG(TAG_L2CPUCLK3),
        ADD_TELEMETRY_TAG(TAG_ETH_LIVE_STATUS),
        ADD_TELEMETRY_TAG(TAG_GDDR_STATUS),
        ADD_TELEMETRY_TAG(TAG_GDDR_SPEED),
        ADD_TELEMETRY_TAG(TAG_ETH_FW_VERSION),
        ADD_TELEMETRY_TAG(TAG_GDDR_FW_VERSION),
        ADD_TELEMETRY_TAG(TAG_DM_APP_FW_VERSION),
        ADD_TELEMETRY_TAG(TAG_DM_BL_FW_VERSION),
        ADD_TELEMETRY_TAG(TAG_FLASH_BUNDLE_VERSION),
        ADD_TELEMETRY_TAG(TAG_CM_FW_VERSION),
        ADD_TELEMETRY_TAG(TAG_L2CPU_FW_VERSION),
        ADD_TELEMETRY_TAG(TAG_FAN_SPEED),
        ADD_TELEMETRY_TAG(TAG_TIMER_HEARTBEAT),
        ADD_TELEMETRY_TAG(TAG_TELEM_ENUM_COUNT),
        ADD_TELEMETRY_TAG(TAG_ENABLED_TENSIX_COL),
        ADD_TELEMETRY_TAG(TAG_ENABLED_ETH),
        ADD_TELEMETRY_TAG(TAG_ENABLED_GDDR),
        ADD_TELEMETRY_TAG(TAG_ENABLED_L2CPU),
        ADD_TELEMETRY_TAG(TAG_PCIE_USAGE),
        ADD_TELEMETRY_TAG(TAG_INPUT_CURRENT),
        ADD_TELEMETRY_TAG(TAG_NOC_TRANSLATION),
        ADD_TELEMETRY_TAG(TAG_FAN_RPM),
        ADD_TELEMETRY_TAG(TAG_GDDR_0_1_TEMP),
        ADD_TELEMETRY_TAG(TAG_GDDR_2_3_TEMP),
        ADD_TELEMETRY_TAG(TAG_GDDR_4_5_TEMP),
        ADD_TELEMETRY_TAG(TAG_GDDR_6_7_TEMP),
        ADD_TELEMETRY_TAG(TAG_GDDR_0_1_CORR_ERRS),
        ADD_TELEMETRY_TAG(TAG_GDDR_2_3_CORR_ERRS),
        ADD_TELEMETRY_TAG(TAG_GDDR_4_5_CORR_ERRS),
        ADD_TELEMETRY_TAG(TAG_GDDR_6_7_CORR_ERRS),
        ADD_TELEMETRY_TAG(TAG_GDDR_UNCORR_ERRS),
        ADD_TELEMETRY_TAG(TAG_MAX_GDDR_TEMP),
        ADD_TELEMETRY_TAG(TAG_ASIC_LOCATION),
        ADD_TELEMETRY_TAG(TAG_BOARD_POWER_LIMIT),
        ADD_TELEMETRY_TAG(TAG_INPUT_POWER),
        ADD_TELEMETRY_TAG(TAG_THERM_TRIP_COUNT),
        ADD_TELEMETRY_TAG(TAG_ASIC_ID_HIGH),
        ADD_TELEMETRY_TAG(TAG_ASIC_ID_LOW)
    };

    // Determine the maximum length of tag names for alignment
    size_t max_name_len = 0;
    for (const auto& tag_entry : telemetry_tags) {
        if (strlen(tag_entry.name) > max_name_len) {
            max_name_len = strlen(tag_entry.name);
        }
    }

    for (auto device_path : DeviceUtils::enumerate_devices()) {
        Device device(device_path.c_str());

        DeviceUtils::print_device_info(device);

        for (const auto& tag_entry : telemetry_tags) {
            uint32_t value = device.read_telemetry(tag_entry.id);

            std::cout << std::setfill(' ');

            std::cout << std::dec << std::setw(3) << std::left << tag_entry.id << " " // Tag ID, left-aligned
                        << std::setw(max_name_len) << std::left << tag_entry.name << " " // Tag Name, left-aligned
                        << "0x" << std::hex << std::setw(8) << std::setfill('0') << std::right << value // Hex value, right-aligned, zero-filled
                        << " : " << std::dec << value << std::endl; // Decimal value
        }
    }
    return 0;
}
