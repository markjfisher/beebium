set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
# Dynamic library linkage so gRPC/protobuf/abseil exist as a single shared copy
# in the process. The server executable and every dlopened extension plugin link
# that one copy, rather than each statically embedding its own -- which on macOS
# produced duplicate gRPC/absl runtimes and a SIGSEGV in grpc_core::ExecCtx::Run
# when a gRPC call crossed the exe<->plugin boundary. The dylibs are bundled
# alongside the server, so the build remains self-contained.
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)
set(VCPKG_BUILD_TYPE release)
