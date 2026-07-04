# Per-generator CPack overrides. CPack evaluates this file once per generator,
# with CPACK_GENERATOR set to the generator being built, so the same install()
# rules can be laid out differently per package format.
#
#   - DEB / RPM: a system package whose payload lives under /opt/beebium, with
#     the maintainer scripts symlinking the servers into /usr/bin. Extracted by
#     the package manager, never by the user, so an absolute prefix is correct.
#   - TGZ: a relocatable bundle. The user downloads it from the Releases page and
#     extracts it ANYWHERE, then puts <dir>/bin on PATH. So it must be a single
#     self-named top-level directory (beebium-server-<ver>-linux-<arch>/) with no
#     /opt prefix and no `tar -C /` root extraction.

if(CPACK_GENERATOR STREQUAL "DEB" OR CPACK_GENERATOR STREQUAL "RPM")
  set(CPACK_PACKAGING_INSTALL_PREFIX "/opt/beebium")
  set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
elseif(CPACK_GENERATOR STREQUAL "TGZ")
  set(CPACK_PACKAGING_INSTALL_PREFIX "/")
  set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
endif()
