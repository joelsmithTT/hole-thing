#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <errno.h>

#include "ioctl.h"

#define NOC_ID_OFFSET 0x4044
#define NOC2AXI_CFG_START 0x1FD00000
#define PCIE_COORD_OFFSET (NOC2AXI_CFG_START + NOC_ID_OFFSET)

// Helper struct for TENSTORRENT_IOCTL_QUERY_MAPPINGS
struct tenstorrent_query_mappings_flex {
	struct tenstorrent_query_mappings_in in;
	struct tenstorrent_mapping out_mappings[16]; // Allocate space for up to 16 mappings
};

int main(int argc, char *argv[]) {
    const char *device_path = "/dev/tenstorrent/0";
    if (argc > 1) {
        device_path = argv[1];
    }

    int fd = open(device_path, O_RDWR | O_SYNC);
    if (fd < 0) {
        fprintf(stderr, "Error opening device %s: %s\n", device_path, strerror(errno));
        return 1;
    }
    printf("Opened device: %s\n", device_path);

    // Query mappings to find BAR0
    struct tenstorrent_query_mappings_flex mappings_query;
    memset(&mappings_query, 0, sizeof(mappings_query));
    mappings_query.in.output_mapping_count = 16;

    if (ioctl(fd, TENSTORRENT_IOCTL_QUERY_MAPPINGS, &mappings_query) != 0) {
        perror("ioctl TENSTORRENT_IOCTL_QUERY_MAPPINGS");
        close(fd);
        return 1;
    }

    uint64_t bar0_offset = 0;
    uint64_t bar0_size = 0;
    int found = 0;

    // The number of mappings is dynamic, but we know BAR0 UC is first if it exists.
    for (int i = 0; i < 16; i++) {
        if (mappings_query.out_mappings[i].mapping_id == TENSTORRENT_MAPPING_RESOURCE0_UC) {
            bar0_offset = mappings_query.out_mappings[i].mapping_base;
            bar0_size = mappings_query.out_mappings[i].mapping_size;
            found = 1;
            break;
        }
    }

    if (!found || bar0_size == 0) {
        fprintf(stderr, "Could not find BAR0 (uncached) mapping for device.\n");
        close(fd);
        return 1;
    }
    printf("Found BAR0 mapping: offset=0x%llx, size=0x%llx\n", (unsigned long long)bar0_offset, (unsigned long long)bar0_size);

    void *bar0 = mmap(NULL, bar0_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, bar0_offset);
    if (bar0 == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }
    printf("BAR0 mapped at %p\n", bar0);

    volatile uint32_t *coord_reg = (uint32_t *)((char *)bar0 + PCIE_COORD_OFFSET);

    printf("Parent (PID %d): Reading PCIe coordinates...\n", getpid());
    uint32_t coords = *coord_reg;
    printf("Parent (PID %d): PCIe X coordinate: %u\n", getpid(), coords & 0x3F);

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        munmap(bar0, bar0_size);
        close(fd);
        return 1;
    }

    if (pid == 0) { // Child process
        printf("Child (PID %d): Reading PCIe coordinates...\n", getpid());
        coords = *coord_reg;
        printf("Child (PID %d): PCIe X coordinate: %u\n", getpid(), coords & 0x3F);
        for(;;) {}
    } else { // Parent process
        printf("Parent (PID %d): Reading PCIe coordinates again after fork...\n", getpid());
        coords = *coord_reg;
        printf("Parent (PID %d): PCIe X coordinate: %u\n", getpid(), coords & 0x3F);
        wait(NULL); // Wait for child to exit
    }

    munmap(bar0, bar0_size);
    close(fd);

    return 0;
}
