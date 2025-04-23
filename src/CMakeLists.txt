set(SOURCES utility.cpp)

add_library(hole-thing ${SOURCES})
target_link_libraries(hole-thing Threads::Threads)
