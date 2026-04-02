# User Port RTC extension (SAF3019P)

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
    # Compile proto
    beebium_compile_proto(
        TARGET beebium_ext_user_port_rtc
        PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/user_port_rtc.proto
        PROTO_PATH ${CMAKE_CURRENT_SOURCE_DIR}
    )

    # Static library (built-in extension)
    add_library(beebium_ext_user_port_rtc STATIC
        UserPortRtcExtension.cpp
        ${beebium_ext_user_port_rtc_PROTO_SRCS}
        ${beebium_ext_user_port_rtc_GRPC_SRCS}
    )

    target_include_directories(beebium_ext_user_port_rtc
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${beebium_ext_user_port_rtc_OUT_DIR}
    )

    target_link_libraries(beebium_ext_user_port_rtc
        PUBLIC
            beebium_rtc_core
            beebium_core
            gRPC::grpc++
            protobuf::libprotobuf
    )

    # Shared library (plugin extension)
    add_library(beebium_ext_user_port_rtc_plugin SHARED
        UserPortRtcExtension.cpp
        plugin_entry.cpp
        ${beebium_ext_user_port_rtc_PROTO_SRCS}
        ${beebium_ext_user_port_rtc_GRPC_SRCS}
    )

    target_include_directories(beebium_ext_user_port_rtc_plugin
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${beebium_ext_user_port_rtc_OUT_DIR}
    )

    target_link_libraries(beebium_ext_user_port_rtc_plugin
        PRIVATE
            beebium_rtc_core
            beebium_core
            gRPC::grpc++
            protobuf::libprotobuf
    )

    set_target_properties(beebium_ext_user_port_rtc_plugin PROPERTIES
        PREFIX ""
        OUTPUT_NAME "user-port-rtc"
    )

    add_custom_command(TARGET beebium_ext_user_port_rtc_plugin POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/manifest.json
            $<TARGET_FILE_DIR:beebium_ext_user_port_rtc_plugin>/manifest.json
        COMMENT "Copying manifest.json for user-port-rtc plugin"
    )
endif()
