CXX = g++
CC = gcc
CFLAGS = -Wall -fPIC
CXXFLAGS = -Wall
LDFLAGS = -L. -Wl,-rpath,.  # Keep this for easier running of test_driver
LDLIBS = -lttio

LIB_TTIO = libttio.so
TEST_DRIVER = test_driver

TTIO_OBJ = ttio.o
TEST_DRIVER_OBJS = test_driver.o

.PHONY: all clean test

all: $(LIB_TTIO) $(TEST_DRIVER)

$(LIB_TTIO): $(TTIO_OBJ)
	$(CC) $(CFLAGS) -shared -o $@ $<

$(TTIO_OBJ): ttio.c ttio.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(TEST_DRIVER): $(TEST_DRIVER_OBJS) $(LIB_TTIO)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $< -o $@ $(LDLIBS)

$(TEST_DRIVER_OBJS): test_driver.cpp ttio.hpp ttio.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

test: $(TEST_DRIVER)
	@echo "Running test_driver..."
	./$(TEST_DRIVER)

clean:
	rm -f $(LIB_TTIO) $(TEST_DRIVER) $(TTIO_OBJ) $(TEST_DRIVER_OBJS)