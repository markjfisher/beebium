# Beebium Discovery Library
#
# Provides DNS-SD (mDNS) service advertisement for network discovery.
# Platform-specific implementations:
# - macOS: Bonjour (dns_sd.h)
# - Windows: windns.h (Windows 10 1903+)
# - Linux: Avahi (libavahi-client, loaded at runtime via dlopen)

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
    # Windows 10 1903+ (SDK 10.0.18362.0) required for the DnsService* mDNS
    # APIs. The advertiser uses DnsServiceRegister; the browser uses
    # DnsServiceBrowse + DnsServiceResolve for AUN peer discovery.
    target_sources(beebium_discovery PRIVATE
        src/WindowsAdvertiser.cpp
        src/WindowsBrowser.cpp
    )
    target_link_libraries(beebium_discovery PRIVATE dnsapi)
    target_compile_definitions(beebium_discovery PRIVATE
        BEEBIUM_HAS_WINDOWS_MDNS
        BEEBIUM_HAS_WINDOWS_MDNS_BROWSE
        _WIN32_WINNT=0x0A00
        WINVER=0x0A00
    )
elseif(UNIX AND NOT APPLE)
    # Linux uses Avahi for mDNS. The Avahi client library is loaded at runtime
    # via dlopen (see AvahiAdvertiser.cpp), so the .deb/.tar.gz bundle keeps its
    # runtime dependencies limited to the base C/C++ libraries. The dev headers
    # are only needed at build time for the type and enum definitions; a build
    # host without them simply falls back to NullAdvertiser.
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(AVAHI avahi-client)
    endif()
    if(AVAHI_FOUND)
        target_sources(beebium_discovery PRIVATE
            src/AvahiAdvertiser.cpp
        )
        target_include_directories(beebium_discovery PRIVATE ${AVAHI_INCLUDE_DIRS})
        # Link the dynamic loader (dlopen/dlsym), not libavahi-client itself.
        target_link_libraries(beebium_discovery PRIVATE ${CMAKE_DL_LIBS})
        target_compile_definitions(beebium_discovery PRIVATE BEEBIUM_HAS_AVAHI)
    endif()
endif()

