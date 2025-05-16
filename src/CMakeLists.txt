# SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
# SPDX-License-Identifier: GPL-2.0-only

set(SOURCES utility.cpp device.cpp)

add_library(hole-thing ${SOURCES})
target_link_libraries(hole-thing)
