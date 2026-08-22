include_guard(GLOBAL)

function(keels2_add_plugin target)
    if(ARGC LESS 2)
        message(FATAL_ERROR "keels2_add_plugin requires a target and at least one source")
    endif()
    if(NOT TARGET KeelS2::SDK)
        message(FATAL_ERROR "KeelS2::SDK is unavailable")
    endif()

    add_library(${target} SHARED ${ARGN})
    target_link_libraries(${target} PRIVATE KeelS2::SDK)
    target_compile_definitions(${target} PRIVATE KEELS2_PLUGIN_BUILD=1)
    set_target_properties(${target} PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        PREFIX ""
    )

    if(MSVC)
        set_target_properties(${target} PROPERTIES
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
        )
        target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc)
        target_link_options(${target} PRIVATE /INCREMENTAL:NO)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
        )
        target_link_options(${target} PRIVATE
            "LINKER:-z,defs"
            "LINKER:-z,relro,-z,now"
        )
    endif()
endfunction()
