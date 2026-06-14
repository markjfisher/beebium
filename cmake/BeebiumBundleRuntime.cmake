# Bundle the vcpkg runtime dylibs into the build tree and the install tree.
#
# On macOS the server is built against a *dynamic* vcpkg triplet so that
# gRPC/protobuf/abseil exist as a single shared copy in the process (the
# executable and every dlopened extension plugin link the same dylibs). Two
# static copies of the gRPC/absl runtime in one process -- which is what the
# old *-osx-static triplet produced -- crash with a SIGSEGV in
# grpc_core::ExecCtx::Run when a gRPC call crosses the exe<->plugin boundary.
#
# Those shared dylibs live under the vcpkg install tree, which is not shipped.
# Copy them next to the beebium ABI libraries in <build>/lib so that:
#   - the servers/plugins resolve them via their existing build rpath, making
#     the build tree self-contained for the CI integration jobs (which run the
#     downloaded server, without the vcpkg tree), and
#   - `cmake --install` lays them in <prefix>/lib, where the servers'
#     INSTALL_RPATH (@loader_path/../lib) already looks.
#
# Gated by BEEBIUM_BUNDLE_RUNTIME_DYLIBS so Linux (system libgrpc++) and any
# static build are unaffected.
option(BEEBIUM_BUNDLE_RUNTIME_DYLIBS
    "Bundle the vcpkg runtime dylibs into <build>/lib and the install tree" OFF)

if(BEEBIUM_BUNDLE_RUNTIME_DYLIBS)
    if(NOT VCPKG_INSTALLED_DIR OR NOT VCPKG_TARGET_TRIPLET)
        message(FATAL_ERROR
            "BEEBIUM_BUNDLE_RUNTIME_DYLIBS requires VCPKG_INSTALLED_DIR and "
            "VCPKG_TARGET_TRIPLET to locate the runtime dylibs.")
    endif()

    set(_beebium_vcpkg_lib_dir
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib")

    # Build tree: mirror the vcpkg dylibs (preserving the version symlinks the
    # @rpath/lib<name>.<major>.dylib install names point at) into <build>/lib.
    add_custom_target(beebium_bundle_runtime_dylibs ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/lib"
        COMMAND bash -c
            "cp -RP '${_beebium_vcpkg_lib_dir}'/*.dylib '${CMAKE_BINARY_DIR}/lib/'"
        COMMENT "Bundling vcpkg runtime dylibs into ${CMAKE_BINARY_DIR}/lib"
        VERBATIM)

    # Install tree: same dylibs under <prefix>/lib next to the ABI libraries.
    install(DIRECTORY "${_beebium_vcpkg_lib_dir}/"
        DESTINATION lib
        FILES_MATCHING PATTERN "*.dylib")
endif()
