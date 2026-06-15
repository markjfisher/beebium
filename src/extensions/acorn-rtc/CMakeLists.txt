# Acorn User Port RTC extension (SAF3019P)

# Core library: chip emulation + time parsing, no gRPC dependencies.
# Used by unit tests and linked into the full extension.
add_library(beebium_rtc_core STATIC
    Saf3019p.cpp
    TimeParser.cpp
)

target_include_directories(beebium_rtc_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

# Full extension with gRPC service (depends on service layer)
if(BEEBIUM_BUILD_SERVICE)
    # Compile proto -- use an OBJECT library so the generated sources are
    # built exactly once, avoiding MSBuild parallel-build races on Windows
    # where both the static and shared targets would try to generate the
    # same .pb.cc file simultaneously.
    beebium_compile_proto(
        TARGET beebium_ext_acorn_rtc
        PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/acorn_rtc.proto
        PROTO_PATH ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_library(beebium_ext_acorn_rtc_proto OBJECT
        ${beebium_ext_acorn_rtc_PROTO_SRCS}
        ${beebium_ext_acorn_rtc_GRPC_SRCS}
    )

    target_include_directories(beebium_ext_acorn_rtc_proto
        PUBLIC ${beebium_ext_acorn_rtc_OUT_DIR}
    )

    # gRPC/protobuf linkage: headers-only on macOS so the plugin resolves the
    # runtime from the host executable; linked normally elsewhere. Test/built-in
    # consumers link gRPC explicitly (below).
    beebium_proto_object_grpc_linkage(beebium_ext_acorn_rtc_proto)

    set_target_properties(beebium_ext_acorn_rtc_proto PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )

    # Static library (built-in extension)
    add_library(beebium_ext_acorn_rtc STATIC
        AcornRtcExtension.cpp
        AcornRtcUi.cpp
    )

    target_include_directories(beebium_ext_acorn_rtc
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    )

    # The static (built-in / unit-test) lib links gRPC for real: its consumers
    # are the executable and standalone test binaries, not the plugin.
    target_link_libraries(beebium_ext_acorn_rtc
        PUBLIC
            beebium_rtc_core
            beebium_core
            beebium_ext_acorn_rtc_proto
            gRPC::grpc++
            protobuf::libprotobuf
            # AcornRtcUi.cpp pulls in extension_ui.pb.h for the
            # View / DispatchRequest types it composes.
            beebium_extension_ui_proto
    )

    # Shared library (plugin extension)
    add_library(beebium_ext_acorn_rtc_plugin SHARED
        AcornRtcExtension.cpp
        AcornRtcUi.cpp
        plugin_entry.cpp
    )

    target_include_directories(beebium_ext_acorn_rtc_plugin
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(beebium_ext_acorn_rtc_plugin
        PRIVATE
            beebium_rtc_core
            beebium_extension_api
            beebium_ext_acorn_rtc_proto
            # AcornRtcUi.cpp pulls in extension_ui.pb.h for the
            # View / DispatchRequest types it composes.
            beebium_extension_ui_proto
    )

    set_target_properties(beebium_ext_acorn_rtc_plugin PROPERTIES
        PREFIX ""
        OUTPUT_NAME "acorn-rtc"
    )

    beebium_finalize_plugin(TARGET beebium_ext_acorn_rtc_plugin NAME acorn-rtc)
endif()
