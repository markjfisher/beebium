# Shared RFC 2217 code (Telnet IAC + COM-PORT-OPTION codec), linked by both the
# rfc2217-client-serial and rfc2217-server-serial plugins. Pure byte
# manipulation, no beebium_core dependency. Built PIC so it can be linked into
# the SHARED plugin modules.

add_library(beebium_ext_rfc2217_common STATIC
    Rfc2217Codec.cpp
)

target_include_directories(beebium_ext_rfc2217_common
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

set_target_properties(beebium_ext_rfc2217_common PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
