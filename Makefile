# Makefile for hole-thing

# Compiler for C++ (CXX) and C (CC) files
CXX := g++
CC := gcc

# --- Directories ---
BIN_DIR := bin
LIB_DIR := lib
OBJ_DIR := obj
SRC_DIR := src
TOOLS_DIR := tools

# --- Library to Build (libttkmd.a) ---
# The final static library we will create and link against
TTKMD_LIB := $(LIB_DIR)/libttkmd.a
# The intermediate object file for the library
TTKMD_OBJ := $(OBJ_DIR)/ttkmd.o
# The source files needed to compile the object file
TTKMD_C_SRC   := $(SRC_DIR)/ttkmd.c
TTKMD_H_SRC   := $(SRC_DIR)/ttkmd.h
IOCTL_H_SRC   := $(SRC_DIR)/ioctl.h

# --- Compiler and Linker Flags ---
CXXFLAGS := -I./include -I$(SRC_DIR) -Wall -Wextra -std=c++17 -g
CFLAGS   := -I./include -I$(SRC_DIR) -Wall -Wextra -g
LDFLAGS  := -L$(LIB_DIR) -lttkmd

# --- Targets ---
# Tools from 'src' that depend on libttkmd.a
TARGETS := \
	$(BIN_DIR)/test \
	$(BIN_DIR)/telemetry \
	$(BIN_DIR)/scratch \
	$(BIN_DIR)/soft_hang \
	$(BIN_DIR)/hard_hang \
	$(BIN_DIR)/iter01 \
	$(BIN_DIR)/iter02 \
	$(BIN_DIR)/iter03 \
	$(BIN_DIR)/iter04 \
	$(BIN_DIR)/iter05

TOOLS_C_SOURCES := $(wildcard $(TOOLS_DIR)/*.c)
TOOLS_C_TARGETS := $(patsubst $(TOOLS_DIR)/%.c,$(BIN_DIR)/%,$(TOOLS_C_SOURCES))

TOOLS_CXX_SOURCES := $(wildcard $(TOOLS_DIR)/*.cpp)
TOOLS_CXX_TARGETS := $(patsubst $(TOOLS_DIR)/%.cpp,$(BIN_DIR)/%,$(TOOLS_CXX_SOURCES))

# Your project's own header files
HEADERS := $(wildcard include/*.hpp)

# The default 'all' target now builds both the main tools and the standalone tools
all: tensix $(TARGETS) $(TOOLS_C_TARGETS) $(TOOLS_CXX_TARGETS)

# --- Build Rules for Main Executables (from src/) ---

# Pattern rule for C++ files (.cpp) that link with libttkmd.a
$(BIN_DIR)/%: $(SRC_DIR)/%.cpp $(HEADERS) $(TTKMD_LIB)
	@echo "CXX $< -> $@"
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

# Pattern rule for C files (.c) that link with libttkmd.a
$(BIN_DIR)/%: $(SRC_DIR)/%.c $(TTKMD_LIB)
	@echo "CC $< -> $@"
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# Pattern rule for tools C files. Note it does NOT depend on $(TTKMD_LIB)
# and does NOT use $(LDFLAGS) for linking.
$(BIN_DIR)/%: $(TOOLS_DIR)/%.c
	@echo "CC (Standalone) $< -> $@"
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< -o $@

$(BIN_DIR)/%: $(TOOLS_DIR)/%.cpp
	@echo "CXX (Standalone) $< -> $@"
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $< -o $@

# --- Build Rules for the Static Library ---

# Rule to create the static library from its object file
$(TTKMD_LIB): $(TTKMD_OBJ)
	@echo "AR $< -> $@"
	@mkdir -p $(LIB_DIR)
	ar rcs $@ $<

# Rule to compile the dependency's C source into an object file
$(TTKMD_OBJ): $(TTKMD_C_SRC) $(TTKMD_H_SRC) $(IOCTL_H_SRC)
	@echo "CC $< -> $@"
	@mkdir -p $(OBJ_DIR)
	$(CC) -I$(SRC_DIR) -c $< -o $@

# --- Utility Targets ---

# Build Tensix firmware
tensix:
	@echo "Building Tensix firmware..."
	@$(MAKE) -C tensix

test: $(BIN_DIR)/test
	@echo "--- Running tests on all devices ---"
	./$(BIN_DIR)/test -1

telemetry: $(BIN_DIR)/telemetry
	@echo "--- Running telemetry ---"
	./$(BIN_DIR)/telemetry

# The clean target removes all generated directories and their contents
clean:
	@echo "Cleaning up generated files..."
	rm -rf $(BIN_DIR) $(LIB_DIR) $(OBJ_DIR)
	@$(MAKE) -C tensix clean

# .PHONY declares targets that are not files, preventing conflicts
.PHONY: all clean test telemetry tensix
