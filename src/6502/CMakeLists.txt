# 6502 CPU emulator library
# Ported from B2 (https://github.com/tom-seddon/b2)
# Pure C library for maximum FFI portability

# --- Code generator (from B2, third-party) ---

add_subdirectory(third_party/b2)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/6502_internal.inl
    COMMAND $<TARGET_FILE:6502_gen> -o ${CMAKE_CURRENT_BINARY_DIR}/6502_internal.inl -c ${CMAKE_CURRENT_SOURCE_DIR}/src/6502.c
    DEPENDS 6502_gen ${CMAKE_CURRENT_SOURCE_DIR}/src/6502.c
    COMMENT "Generating 6502_internal.inl from 6502.c"
)

# --- 6502 library ---

add_library(6502_lib STATIC
    src/6502.c
    include/6502/6502.h
    ${CMAKE_CURRENT_BINARY_DIR}/6502_internal.inl
)

# Include the generated .inl file from the build directory
target_include_directories(6502_lib PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

# Public header
target_include_directories(6502_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Use C11 standard (inherited from root, but be explicit)
set_target_properties(6502_lib PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)
