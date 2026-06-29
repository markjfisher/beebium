set(VCPKG_TARGET_ARCHITECTURE x64)
# Static libraries (gRPC/protobuf/abseil folded into the binaries) but the
# dynamic CRT, so the only runtime dependency is the Visual C++ Redistributable.
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
# Release-only: we never ship debug deps, and skipping them roughly halves the
# vcpkg build time.
set(VCPKG_BUILD_TYPE release)
