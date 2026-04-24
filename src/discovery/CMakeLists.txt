# Beebium Discovery Library
#
# Provides DNS-SD (mDNS) service advertisement for network discovery.
# Platform-specific implementations:
# - macOS: Bonjour (dns_sd.h)
# - Windows: windns.h (Windows 10 1903+)
# - Linux: Avahi (future)

add_library(beebium_discovery STATIC
    src/NullAdvertiser.cpp
    src/NullBrowser.cpp
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
        src/BonjourBrowser.cpp
    )
    target_compile_definitions(beebium_discovery PRIVATE
        BEEBIUM_HAS_BONJOUR
        BEEBIUM_HAS_BONJOUR_BROWSE
    )
    # dns_sd.h is a system framework on macOS, no extra linking needed
    # The symbols are in libSystem
elseif(WIN32)
    # Windows 10 1903+ (SDK 10.0.18362.0) required for DnsServiceRegister.
    # The browse-side counterpart (DnsServiceBrowse / DnsServiceResolve
    # for AUN peer discovery) is not yet implemented; Windows falls back
    # to NullBrowser. Tracked in the AUN peer-discovery work for a
    # future iteration.
    target_sources(beebium_discovery PRIVATE
        src/WindowsAdvertiser.cpp
    )
    target_link_libraries(beebium_discovery PRIVATE dnsapi)
    target_compile_definitions(beebium_discovery PRIVATE
        BEEBIUM_HAS_WINDOWS_MDNS
        _WIN32_WINNT=0x0A00
        WINVER=0x0A00
    )
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

