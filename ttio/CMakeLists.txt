add_library(ttio SHARED ttio.c)

set_target_properties(ttio PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
    OUTPUT_NAME "ttio"
    VERSION 1.0.0
    SOVERSION 1
)

add_executable(ttio_main main.cpp)
target_link_libraries(ttio_main PRIVATE ttio)
