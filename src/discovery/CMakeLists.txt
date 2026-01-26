# Beebium Discovery Library
#
# Provides DNS-SD (mDNS) service advertisement for network discovery.
# Platform-specific implementations:
# - macOS: Bonjour (dns_sd.h)
# - Linux: Avahi (future)
# - Windows: windns.h (future)

add_library(beebium_discovery STATIC
    src/NullAdvertiser.cpp
)

target_include_directories(beebium_discovery
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Require C++20
target_compile_features(beebium_discovery PUBLIC cxx_std_20)

# Platform-specific implementations
if(APPLE)
    target_sources(beebium_discovery PRIVATE
        src/BonjourAdvertiser.cpp
    )
    target_compile_definitions(beebium_discovery PRIVATE BEEBIUM_HAS_BONJOUR)
    # dns_sd.h is a system framework on macOS, no extra linking needed
    # The symbols are in libSystem
endif()

# Future: Linux (Avahi)
# if(UNIX AND NOT APPLE)
#     find_package(PkgConfig)
#     pkg_check_modules(AVAHI avahi-client)
#     if(AVAHI_FOUND)
#         target_sources(beebium_discovery PRIVATE
#             src/AvahiAdvertiser.cpp
#         )
#         target_link_libraries(beebium_discovery PRIVATE ${AVAHI_LIBRARIES})
#         target_include_directories(beebium_discovery PRIVATE ${AVAHI_INCLUDE_DIRS})
#         target_compile_definitions(beebium_discovery PRIVATE BEEBIUM_HAS_AVAHI)
#     endif()
# endif()

# Future: Windows (windns.h)
# if(WIN32)
#     target_sources(beebium_discovery PRIVATE
#         src/WindowsAdvertiser.cpp
#     )
#     target_link_libraries(beebium_discovery PRIVATE dnsapi)
#     target_compile_definitions(beebium_discovery PRIVATE BEEBIUM_HAS_WINDOWS_MDNS)
# endif()
