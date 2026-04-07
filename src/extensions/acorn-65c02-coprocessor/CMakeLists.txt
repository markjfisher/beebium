# Acorn 65C02 3 MHz second processor extension

add_library(beebium_ext_acorn_65c02 STATIC
    SecondProcessor65C02Extension.cpp
)

target_include_directories(beebium_ext_acorn_65c02
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(beebium_ext_acorn_65c02
    PUBLIC beebium_core
)

if(BEEBIUM_BUILD_SERVICE)
    target_link_libraries(beebium_ext_acorn_65c02
        PUBLIC beebium_service
    )
endif()
