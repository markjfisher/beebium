# TestScratchRam extension - 8 bytes of scratch RAM at 0xFC50 for framework testing

add_library(beebium_ext_test_scratch_ram INTERFACE)
target_include_directories(beebium_ext_test_scratch_ram
    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(beebium_ext_test_scratch_ram
    INTERFACE beebium_core
)
