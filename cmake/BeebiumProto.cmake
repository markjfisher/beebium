# BeebiumProto.cmake - Reusable proto compilation for Beebium
#
# Usage:
#   beebium_compile_proto(
#       TARGET my_target
#       PROTO_FILES file1.proto file2.proto
#       PROTO_PATH /path/to/proto/directory
#       OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated
#   )
#
# Sets in parent scope:
#   ${TARGET}_PROTO_SRCS  - list of generated .pb.cc files
#   ${TARGET}_GRPC_SRCS   - list of generated .grpc.pb.cc files
#   ${TARGET}_PROTO_HDRS  - list of generated .pb.h files
#   ${TARGET}_GRPC_HDRS   - list of generated .grpc.pb.h files
#   ${TARGET}_OUT_DIR     - the output directory containing generated files
#
# Requires PROTOC_EXECUTABLE and GRPC_CPP_PLUGIN to be set (done in top-level CMakeLists.txt).

function(beebium_compile_proto)
    cmake_parse_arguments(ARG "" "TARGET;PROTO_PATH;OUTPUT_DIR" "PROTO_FILES" ${ARGN})

    if(NOT ARG_TARGET)
        message(FATAL_ERROR "beebium_compile_proto: TARGET is required")
    endif()
    if(NOT ARG_PROTO_FILES)
        message(FATAL_ERROR "beebium_compile_proto: PROTO_FILES is required")
    endif()
    if(NOT ARG_PROTO_PATH)
        message(FATAL_ERROR "beebium_compile_proto: PROTO_PATH is required")
    endif()
    if(NOT ARG_OUTPUT_DIR)
        set(ARG_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
    endif()

    file(MAKE_DIRECTORY ${ARG_OUTPUT_DIR})

    set(_proto_srcs)
    set(_proto_hdrs)
    set(_grpc_srcs)
    set(_grpc_hdrs)

    foreach(PROTO_FILE ${ARG_PROTO_FILES})
        get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)

        list(APPEND _proto_srcs ${ARG_OUTPUT_DIR}/${PROTO_NAME}.pb.cc)
        list(APPEND _proto_hdrs ${ARG_OUTPUT_DIR}/${PROTO_NAME}.pb.h)
        list(APPEND _grpc_srcs ${ARG_OUTPUT_DIR}/${PROTO_NAME}.grpc.pb.cc)
        list(APPEND _grpc_hdrs ${ARG_OUTPUT_DIR}/${PROTO_NAME}.grpc.pb.h)

        add_custom_command(
            OUTPUT
                ${ARG_OUTPUT_DIR}/${PROTO_NAME}.pb.cc
                ${ARG_OUTPUT_DIR}/${PROTO_NAME}.pb.h
                ${ARG_OUTPUT_DIR}/${PROTO_NAME}.grpc.pb.cc
                ${ARG_OUTPUT_DIR}/${PROTO_NAME}.grpc.pb.h
            COMMAND ${PROTOC_EXECUTABLE}
                --cpp_out=${ARG_OUTPUT_DIR}
                --grpc_out=${ARG_OUTPUT_DIR}
                --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
                -I${ARG_PROTO_PATH}
                ${PROTO_FILE}
            DEPENDS ${PROTO_FILE}
            COMMENT "Generating gRPC/Protobuf sources for ${PROTO_NAME}.proto"
        )
    endforeach()

    set(${ARG_TARGET}_PROTO_SRCS ${_proto_srcs} PARENT_SCOPE)
    set(${ARG_TARGET}_PROTO_HDRS ${_proto_hdrs} PARENT_SCOPE)
    set(${ARG_TARGET}_GRPC_SRCS ${_grpc_srcs} PARENT_SCOPE)
    set(${ARG_TARGET}_GRPC_HDRS ${_grpc_hdrs} PARENT_SCOPE)
    set(${ARG_TARGET}_OUT_DIR ${ARG_OUTPUT_DIR} PARENT_SCOPE)

    # Suppress warnings in generated code
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        set_source_files_properties(
            ${_proto_srcs} ${_grpc_srcs}
            PROPERTIES COMPILE_FLAGS "-Wno-unused-parameter -Wno-shadow"
        )
    elseif(MSVC)
        set_source_files_properties(
            ${_proto_srcs} ${_grpc_srcs}
            PROPERTIES COMPILE_FLAGS "/wd4100 /wd4127 /wd4244 /wd4267"
        )
    endif()
endfunction()


# Apply the gRPC/protobuf linkage for a plugin's proto OBJECT library.
#
# On macOS, a dlopened gRPC-service plugin must NOT embed its own
# gRPC/protobuf/abseil runtime -- two runtimes in one process crash when a call
# crosses the exe<->plugin boundary (project_macos_static_grpc_duplicate_runtime
# / gRPC #39198). So compile against the headers only and resolve the runtime
# from the host executable at load (beebium_finalize_plugin adds -undefined
# dynamic_lookup; the server is built with -rdynamic).
#
# Elsewhere, link the libraries normally: Linux shares one system libgrpc++.so
# (and a shared lib may carry undefined symbols resolved from the host at load),
# while Windows DLLs must resolve every symbol at link time via the import
# library, so headers-only is not an option there.
function(beebium_proto_object_grpc_linkage target)
    if(APPLE)
        target_include_directories(${target}
            PUBLIC
                $<TARGET_PROPERTY:gRPC::grpc++,INTERFACE_INCLUDE_DIRECTORIES>
                $<TARGET_PROPERTY:protobuf::libprotobuf,INTERFACE_INCLUDE_DIRECTORIES>
        )
    else()
        target_link_libraries(${target}
            PUBLIC
                gRPC::grpc++
                protobuf::libprotobuf
        )
    endif()
endfunction()
