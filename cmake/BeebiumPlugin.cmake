# BeebiumPlugin.cmake - Shared post-build deployment for extension plugins.
#
# Usage:
#   beebium_finalize_plugin(TARGET beebium_ext_piconet_plugin NAME piconet)
#
# What it does:
#   - Copies the plugin library and its manifest.json into
#     "<server-exe-dir>/extensions/<name>/" so the server's default
#     extension-dir search (exe_parent / "extensions") finds it on every
#     platform and every build configuration.
#   - On Windows additionally copies the plugin DLL into
#     "<build>/tests/<CONFIG>/" so test binaries that link the plugin
#     (or LoadLibrary it) can resolve it through the Windows DLL search,
#     matching the beebium_extension_api.dll / beebium_extension_ui_proto.dll
#     arrangement.
#
# Why a shared helper:
#   Every SHARED plugin (acorn-rtc, acorn-scsi, piconet, scsi-hard-disc,
#   test-scratch-ram) needs identical deployment -- the previous inline
#   manifest-copy pattern dropped manifests next to the build output,
#   which on MSBuild multi-config lands a $(Configuration) directory
#   below where the server's default extension-dir search looks. Placing
#   plugins alongside the server executable sidesteps the multi-config
#   layout mismatch entirely.
#
# TARGET   - the plugin's shared-library target name (e.g. beebium_ext_piconet_plugin)
# NAME     - the short directory name (e.g. "piconet"); also the expected
#            output name of the plugin DLL / .so / .dylib.

function(beebium_finalize_plugin)
    cmake_parse_arguments(ARG "" "TARGET;NAME" "" ${ARGN})

    if(NOT ARG_TARGET)
        message(FATAL_ERROR "beebium_finalize_plugin: TARGET is required")
    endif()
    if(NOT ARG_NAME)
        message(FATAL_ERROR "beebium_finalize_plugin: NAME is required")
    endif()

    # Server-adjacent deploy. Only meaningful when the server executables
    # are being built this configure; skip gracefully otherwise so that
    # tests-only / plugin-only configures still succeed.
    if(BEEBIUM_BUILD_SERVER)
        # $<TARGET_FILE_DIR:beebium-model-b> is evaluated at build-system
        # generation time; no dependency on subdirectory processing order.
        # All three server variants (model-b, model-b-plus, model-b-romram)
        # share the same output directory, so any of them is a valid anchor.
        set(_deploy_dir "$<TARGET_FILE_DIR:beebium-model-b>/extensions/${ARG_NAME}")
        add_custom_command(TARGET ${ARG_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_deploy_dir}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${ARG_TARGET}>
                "${_deploy_dir}/"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/manifest.json
                "${_deploy_dir}/manifest.json"
            COMMENT "Deploying ${ARG_NAME} plugin to <server>/extensions/${ARG_NAME}/"
        )
    endif()

    # Windows test runtime: any test binary that statically links the
    # plugin (or LoadLibrary's it directly) needs the DLL findable via
    # the Windows DLL search (which looks in the loading executable's
    # directory). Mirrors the existing copy steps for
    # beebium_extension_api.dll / beebium_extension_ui_proto.dll.
    if(WIN32 AND BEEBIUM_BUILD_TESTS)
        add_custom_command(TARGET ${ARG_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "${CMAKE_BINARY_DIR}/tests/$<CONFIG>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${ARG_TARGET}>
                "${CMAKE_BINARY_DIR}/tests/$<CONFIG>/"
            COMMENT "Copying ${ARG_NAME}.dll to tests/$<CONFIG>/ for runtime loading"
        )
    endif()
endfunction()
