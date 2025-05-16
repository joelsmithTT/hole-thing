# SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
# SPDX-License-Identifier: GPL-2.0-only

add_executable(blackhole_tests blackhole_tests.cpp)
add_executable(iatu_discover iatu_discover.cpp)
add_executable(noc_dma_tests noc_dma_tests.cpp)
add_executable(noc_perf noc_perf.cpp)
add_executable(pin_hugepages_greedy pin_hugepages_greedy.cpp)
add_executable(pin_test pin_test.cpp)
add_executable(wormhole_tests wormhole_tests.cpp)

target_link_libraries(blackhole_tests hole-thing)
target_link_libraries(iatu_discover hole-thing)
target_link_libraries(noc_dma_tests hole-thing)
target_link_libraries(noc_perf hole-thing)
target_link_libraries(pin_hugepages_greedy hole-thing)
target_link_libraries(pin_test hole-thing)
target_link_libraries(wormhole_tests hole-thing)
