# AUN UDP Econet transport (built-in extension).
#
# Mirrors the acorn-65c02-coprocessor pattern: a static library that the
# server links in directly. There is no on-disk plugin SHARED library
# because AUN ships built-in -- the user gets it without configuring
# an extension directory.

add_library(beebium_ext_aun STATIC
    AunEconetTransportExtension.cpp
)

target_include_directories(beebium_ext_aun
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(beebium_ext_aun
    PUBLIC beebium_core
)
