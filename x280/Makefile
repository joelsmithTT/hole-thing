# Makefile for X280 bare-metal programs
#
# Uses riscv64-unknown-elf-gcc (bare-metal toolchain)
# Target: RV64GC (what X280 actually is, minus the vector extension for simplicity)

CC := riscv64-unknown-elf-gcc
LD := riscv64-unknown-elf-ld
OBJCOPY := riscv64-unknown-elf-objcopy
OBJDUMP := riscv64-unknown-elf-objdump
SIZE := riscv64-unknown-elf-size

# Programs to build
PROGRAMS := hello minimal

ALL_ELFS := $(addsuffix .elf,$(PROGRAMS))
ALL_BINS := $(addsuffix .bin,$(PROGRAMS))

# Compiler flags - RV64GC, machine mode, no OS
# -mcmodel=medany for position-independent code that works with any address
CFLAGS := -march=rv64gc \
          -mabi=lp64d \
          -mcmodel=medany \
          -O2 \
          -g \
          -Wall \
          -Wextra \
          -fno-builtin \
          -fno-exceptions \
          -ffreestanding \
          -ffunction-sections \
          -fdata-sections \
          -nostdlib

LDFLAGS := -T linker.ld \
           -nostartfiles \
           -nostdlib \
           -Wl,--gc-sections

.PHONY: all clean disasm size

all: $(ALL_BINS)
	@echo ""
	@echo "Built X280 programs:"
	@for prog in $(PROGRAMS); do \
		echo "  $$prog.bin ($$(wc -c < $$prog.bin) bytes)"; \
	done
	@echo ""

%.o: %.c
	@echo "CC $<"
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	@echo "AS $<"
	$(CC) $(CFLAGS) -c $< -o $@

%.elf: %.o linker.ld
	@echo "LD $@"
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< -Wl,-Map=$*.map

%.bin: %.elf
	@echo "OBJCOPY $@"
	$(OBJCOPY) -O binary $< $@

size: $(ALL_ELFS)
	@for elf in $(ALL_ELFS); do \
		echo "Size of $$elf:"; \
		$(SIZE) $$elf; \
		echo ""; \
	done

disasm: $(ALL_ELFS)
	@for elf in $(ALL_ELFS); do \
		lst=$${elf%.elf}.lst; \
		echo "Disassembling $$elf -> $$lst"; \
		$(OBJDUMP) -d -S $$elf > $$lst; \
	done

clean:
	rm -f *.o *.elf *.bin *.lst *.map

