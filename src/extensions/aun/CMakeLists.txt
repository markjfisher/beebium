# AUN UDP Econet transport (built-in extension).
#
# Mirrors the acorn-65c02-coprocessor pattern: a static library that the
# server links in directly. There is no on-disk plugin SHARED library
# because AUN ships built-in -- the user gets it without configuring
# an extension directory.
#
# When the service layer is being built, also compiles aun.proto and
# the AunService implementation. The service is the gRPC home for
# AUN-specific RPCs (peer table management, cable plug, port status)
# that used to live on EconetService and required dynamic_cast.

set(AUN_SOURCES
    AunEconetTransportExtension.cpp
    AunUi.cpp
)

if(BEEBIUM_BUILD_SERVICE)
    beebium_compile_proto(
        TARGET beebium_ext_aun
        PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/aun.proto
        PROTO_PATH ${CMAKE_CURRENT_SOURCE_DIR}
    )
    list(APPEND AUN_SOURCES
        AunService.cpp
        ${beebium_ext_aun_PROTO_SRCS}
        ${beebium_ext_aun_GRPC_SRCS}
    )
endif()

add_library(beebium_ext_aun STATIC ${AUN_SOURCES})

target_include_directories(beebium_ext_aun
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

if(BEEBIUM_BUILD_SERVICE)
    target_include_directories(beebium_ext_aun
        PUBLIC ${beebium_ext_aun_OUT_DIR}
    )
    target_compile_definitions(beebium_ext_aun PUBLIC BEEBIUM_BUILD_SERVICE)
endif()

target_link_libraries(beebium_ext_aun
    PUBLIC beebium_core
)

if(BEEBIUM_BUILD_SERVICE)
    target_link_libraries(beebium_ext_aun
        PUBLIC
            gRPC::grpc++
            protobuf::libprotobuf
    )
endif()
