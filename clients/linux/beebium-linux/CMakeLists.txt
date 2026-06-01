cmake_minimum_required(VERSION 3.21)

set(BEEBIUM_LINUX_STANDALONE OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(BEEBIUM_LINUX_STANDALONE ON)
endif()

if(BEEBIUM_LINUX_STANDALONE)
    project(beebium-linux
        VERSION 0.1.0
        DESCRIPTION "Native Linux frontend for Beebium"
        LANGUAGES CXX
    )

    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    get_filename_component(BEEBIUM_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

    find_package(Protobuf CONFIG QUIET)
    if(NOT Protobuf_FOUND)
        find_package(Protobuf REQUIRED)
    endif()

    find_package(gRPC CONFIG QUIET)
    if(NOT gRPC_FOUND)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GRPC REQUIRED grpc++ grpc)

        if(NOT TARGET gRPC::grpc++)
            add_library(gRPC::grpc++ INTERFACE IMPORTED)
            set_target_properties(gRPC::grpc++ PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${GRPC_INCLUDE_DIRS}"
                INTERFACE_LINK_LIBRARIES "${GRPC_LIBRARIES}"
            )
        endif()
    endif()

    if(TARGET protobuf::protoc)
        get_target_property(PROTOC_EXECUTABLE protobuf::protoc LOCATION)
    else()
        find_program(PROTOC_EXECUTABLE protoc REQUIRED)
    endif()

    if(TARGET gRPC::grpc_cpp_plugin)
        get_target_property(GRPC_CPP_PLUGIN gRPC::grpc_cpp_plugin LOCATION)
    else()
        find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin REQUIRED)
    endif()

    include("${BEEBIUM_REPO_ROOT}/cmake/BeebiumProto.cmake")
endif()

find_package(Qt6 6.5 COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Multimedia REQUIRED)
find_package(KDDockWidgets-qt6 QUIET)

if(BEEBIUM_LINUX_STANDALONE)
    set(BEEBIUM_REPO_ROOT "${BEEBIUM_REPO_ROOT}")
else()
    set(BEEBIUM_REPO_ROOT "${CMAKE_SOURCE_DIR}")
endif()

set(BEEBIUM_LINUX_PROTO_FILES
    ${BEEBIUM_REPO_ROOT}/src/service/proto/audio.proto
    ${BEEBIUM_REPO_ROOT}/src/service/proto/video.proto
    ${BEEBIUM_REPO_ROOT}/src/service/proto/keyboard.proto
    ${BEEBIUM_REPO_ROOT}/src/service/proto/system.proto
    ${BEEBIUM_REPO_ROOT}/src/service/proto/disc.proto
    ${BEEBIUM_REPO_ROOT}/src/service/proto/indicator.proto
    ${BEEBIUM_REPO_ROOT}/src/service/proto/serial.proto
    ${BEEBIUM_REPO_ROOT}/src/service/proto/sideways.proto
    ${BEEBIUM_REPO_ROOT}/src/service/proto/peripheral_extension.proto
    ${BEEBIUM_REPO_ROOT}/src/service/proto/econet_transport.proto
)

beebium_compile_proto(
    TARGET beebium_linux_client
    PROTO_FILES ${BEEBIUM_LINUX_PROTO_FILES}
    PROTO_PATH ${BEEBIUM_REPO_ROOT}/src/service/proto
    OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated
)

beebium_compile_proto(
    TARGET beebium_linux_extension_ui
    PROTO_FILES ${BEEBIUM_REPO_ROOT}/src/core/extension-api/proto/extension_ui.proto
    PROTO_PATH ${BEEBIUM_REPO_ROOT}/src/core/extension-api/proto
    OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated
)

qt_standard_project_setup()

qt_add_executable(beebium-linux
    src/ClientTypes.hpp
    src/ConfigProfiles.hpp
    src/ConfigProfiles.cpp
    src/ConfigsDialog.hpp
    src/ConfigsDialog.cpp
    src/ConnectionTarget.hpp
    src/ExtensionUiPanel.hpp
    src/ExtensionUiPanel.cpp
    src/main.cpp
    src/MainWindow.hpp
    src/MainWindow.cpp
    src/HostKeyMap.hpp
    src/HostKeyMap.cpp
    src/VideoWidget.hpp
    src/VideoWidget.cpp
    src/GrpcVideoClient.hpp
    src/GrpcVideoClient.cpp
    src/GrpcAudioClient.hpp
    src/GrpcAudioClient.cpp
    src/GrpcDiscClient.hpp
    src/GrpcDiscClient.cpp
    src/GrpcIndicatorClient.hpp
    src/GrpcIndicatorClient.cpp
    src/LocalServerManager.hpp
    src/LocalServerManager.cpp
    src/GrpcKeyboardClient.hpp
    src/GrpcKeyboardClient.cpp
    src/GrpcSerialClient.hpp
    src/GrpcSerialClient.cpp
    src/GrpcSidewaysClient.hpp
    src/GrpcSidewaysClient.cpp
    src/GrpcSystemClient.hpp
    src/GrpcSystemClient.cpp
    src/FrameGeometry.hpp
    src/ConnectionTarget.cpp
    src/FrameGeometry.cpp
)

target_include_directories(beebium-linux PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${beebium_linux_client_OUT_DIR}
)

target_link_libraries(beebium-linux PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Multimedia
    gRPC::grpc++
    protobuf::libprotobuf
)

if(KDDockWidgets-qt6_FOUND)
    target_compile_definitions(beebium-linux PRIVATE BEEBIUM_HAVE_KDDOCKWIDGETS=1)
    target_link_libraries(beebium-linux PRIVATE KDAB::kddockwidgets)
else()
    message(WARNING "KDDockWidgets-qt6 not found; building Linux frontend with Qt dock-widget fallback")
endif()

target_sources(beebium-linux PRIVATE
    ${beebium_linux_client_PROTO_SRCS}
    ${beebium_linux_client_GRPC_SRCS}
    ${beebium_linux_extension_ui_PROTO_SRCS}
    ${beebium_linux_extension_ui_GRPC_SRCS}
)

set_target_properties(beebium-linux PROPERTIES
    OUTPUT_NAME beebium-linux
)
