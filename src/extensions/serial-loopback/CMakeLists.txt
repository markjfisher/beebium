# Serial loopback (built-in extension): a TX->RX echo plug on the BBC serial
# port. Builds as a STATIC library linked directly into the server. Built-in
# for the same CLI-test ergonomics reason as the other serial extensions (see
# BuiltinExtensions.hpp). Zero config, no proto/gRPC service.

add_library(beebium_ext_serial_loopback STATIC
    SerialLoopbackExtension.cpp
)

target_include_directories(beebium_ext_serial_loopback
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(beebium_ext_serial_loopback
    PUBLIC beebium_core
)
