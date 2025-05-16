# SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
# SPDX-License-Identifier: GPL-2.0-only

cmake_minimum_required(VERSION 3.10)
project(hole-thing)

set(CMAKE_CXX_STANDARD 20)

find_package(Threads REQUIRED)

add_subdirectory(src)
include_directories(src)

add_subdirectory(tools)
