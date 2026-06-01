find_package(Qt6 6.5 COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets QUIET)

if(NOT Qt6_FOUND)
    message(STATUS "Qt6 not found; skipping Linux frontend build")
    return()
endif()

add_subdirectory(beebium-linux)
