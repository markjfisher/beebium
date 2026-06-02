# CPack configuration for the self-contained Linux server bundle.
#
# Produces, from the same install() rules the bundle is built on:
#   - a .deb that lays the relocatable tree under /opt/beebium and symlinks the
#     four server binaries into /usr/bin (via the maintainer scripts), and
#   - a plain .tar.gz of the same tree for non-dpkg distros (Arch, Fedora, ...).
#
# Intended for the Linux bundle build; the TGZ generator is also configured
# elsewhere (e.g. macOS) so the layout can be verified, but the DEB generator is
# only added on Linux (it needs dpkg).

set(CPACK_PACKAGE_NAME "beebium-server")
set(CPACK_PACKAGE_VENDOR "sixty-north")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Beebium headless BBC Micro emulator server (self-contained)")
set(CPACK_PACKAGE_CONTACT "Robert Smallshire <rob@sixty-north.com>")

# Lay the relocatable tree under /opt/beebium. The install() DESTINATIONs are
# relative (bin/, lib/, share/), so this prefix yields /opt/beebium/{bin,lib,share}.
set(CPACK_PACKAGING_INSTALL_PREFIX "/opt/beebium")

# Normalise the architecture to the Debian label (amd64/arm64) so the .deb and
# the .tar.gz carry the same arch name and match the .deb's auto-detected
# Architecture field.
set(_beebium_pkg_arch "${CMAKE_SYSTEM_PROCESSOR}")
if(_beebium_pkg_arch STREQUAL "x86_64")
    set(_beebium_pkg_arch "amd64")
elseif(_beebium_pkg_arch STREQUAL "aarch64")
    set(_beebium_pkg_arch "arm64")
endif()

# Base package file name used by all generators: beebium-server-<ver>-linux-<arch>.
# The DEB generator overrides this with the canonical Debian name below. Setting
# CPACK_PACKAGE_FILE_NAME (rather than CPACK_ARCHIVE_FILE_NAME) is what reliably
# names the archive across CPack versions.
set(CPACK_PACKAGE_FILE_NAME
    "beebium-server-${PROJECT_VERSION}-linux-${_beebium_pkg_arch}")

# Archive (TGZ): root at the install prefix (opt/beebium/...) with no extra
# <name>-<ver>-<sys> wrapper directory, so it extracts cleanly to /.
set(CPACK_GENERATOR "TGZ")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND CPACK_GENERATOR "DEB")

    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Robert Smallshire <rob@sixty-north.com>")
    set(CPACK_DEBIAN_PACKAGE_SECTION "otherosfs")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/sixty-north/beebium")
    # Derive runtime dependencies from the binaries' actual dynamic links
    # (libc6, libstdc++6, libgcc-s1). gRPC/protobuf are statically linked and
    # contribute no package dependencies.
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    # Canonical Debian file name: beebium-server_<version>_<arch>.deb, with the
    # architecture auto-detected via dpkg --print-architecture.
    set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
    # postinst/prerm create and remove the /usr/bin -> /opt/beebium/bin symlinks.
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
        "${CMAKE_SOURCE_DIR}/packaging/debian/postinst"
        "${CMAKE_SOURCE_DIR}/packaging/debian/prerm")
endif()

include(CPack)
