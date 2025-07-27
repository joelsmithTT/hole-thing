CXX := g++
CXXFLAGS := -I./include -I/usr/include/tenstorrent -Wall -Wextra -std=c++17
LDFLAGS := -L/usr/local/lib -lttkmd
BIN_DIR := bin
SRC_DIR := src

TARGETS := \
	$(BIN_DIR)/ttkmd_test \
	$(BIN_DIR)/telemetry \
	$(BIN_DIR)/pin_pages \
	$(BIN_DIR)/use_tlbs

HEADERS := $(wildcard include/*.hpp)

all: $(TARGETS)

test: $(BIN_DIR)/ttkmd_test
	@echo "--- Running ttkmd_test ---"
	./$(BIN_DIR)/ttkmd_test

telemetry: $(BIN_DIR)/telemetry
	@echo "--- Running telemetry ---"
	./$(BIN_DIR)/telemetry


# This is a pattern rule that defines how to build any target in the BIN_DIR
# from a corresponding .cpp file in the SRC_DIR.
# It also depends on all the header files found above.
$(BIN_DIR)/%: $(SRC_DIR)/%.cpp $(HEADERS)
	@echo "Compiling and linking $< to create $@"
	@mkdir -p $(BIN_DIR) # Ensure the bin directory exists
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

clean:
	@echo "Cleaning up generated files..."
	rm -rf $(BIN_DIR)

# Phony targets: Declares targets that are not actual files.
.PHONY: all clean test
