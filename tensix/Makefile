# Makefile for Tensix test programs

# RISC-V toolchain
CC := riscv32-unknown-elf-gcc
LD := riscv32-unknown-elf-ld
OBJCOPY := riscv32-unknown-elf-objcopy
OBJDUMP := riscv32-unknown-elf-objdump
SIZE := riscv32-unknown-elf-size

# All programs we build
PROGRAMS := iter01 iter02 iter04

# All targets (ELF and BIN for each program)
ALL_ELFS := $(addsuffix .elf,$(PROGRAMS))
ALL_BINS := $(addsuffix .bin,$(PROGRAMS))

# Compiler flags
CFLAGS := -march=rv32i \
          -mabi=ilp32 \
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

# Linker flags
LDFLAGS := -T linker.ld \
           -nostartfiles \
           -nostdlib \
           -Wl,--gc-sections

# Phony targets
.PHONY: all clean disasm size

# Default target - build all programs
all: $(ALL_BINS)
	@echo ""
	@echo "Built programs:"
	@for prog in $(PROGRAMS); do \
		echo "  $$prog.bin (size: $$(wc -c < $$prog.bin) bytes)"; \
	done
	@echo ""

# Pattern rule: compile C to object
%.o: %.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Pattern rule: link object to ELF
%.elf: %.o linker.ld
	@echo "Linking $@..."
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< -Wl,-Map=$*.map

# Pattern rule: convert ELF to BIN
%.bin: %.elf
	@echo "Creating binary $@..."
	$(OBJCOPY) -O binary $< $@

# Display size for all programs
size: $(ALL_ELFS)
	@echo ""
	@for elf in $(ALL_ELFS); do \
		echo "Size of $$elf:"; \
		$(SIZE) $$elf; \
		echo ""; \
	done

# Disassemble all programs
disasm: $(ALL_ELFS)
	@for elf in $(ALL_ELFS); do \
		lst=$${elf%.elf}.lst; \
		echo "Disassembling $$elf -> $$lst"; \
		$(OBJDUMP) -d -S $$elf > $$lst; \
	done

# Clean
clean:
	rm -f *.o *.elf *.bin *.lst *.map

# Help
help:
	@echo "Targets:"
	@echo "  all     - Build ELF and BIN (default)"
	@echo "  clean   - Remove build artifacts"
	@echo "  disasm  - Generate disassembly listing"
	@echo "  size    - Display binary size"
	@echo "  help    - Show this help"

