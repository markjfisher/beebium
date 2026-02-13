add_library(stb_image_write STATIC stb_image_write.cpp)
target_include_directories(stb_image_write PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Suppress warnings in third-party code
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(stb_image_write PRIVATE -w)
elseif(MSVC)
    target_compile_options(stb_image_write PRIVATE /W0)
endif()
