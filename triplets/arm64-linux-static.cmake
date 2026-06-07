set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
# Release-only: we never link vcpkg debug builds of gRPC/protobuf into the
# shipped bundle, and skipping them roughly halves vcpkg build time.
set(VCPKG_BUILD_TYPE release)
