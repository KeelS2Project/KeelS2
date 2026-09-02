include_guard(GLOBAL)

function(keels2_add_game_adapter target game)
    if(ARGC LESS 3)
        message(FATAL_ERROR "keels2_add_game_adapter requires a target, game, and source")
    endif()
    if(NOT TARGET KeelS2::AdapterSDK)
        message(FATAL_ERROR "KeelS2::AdapterSDK is unavailable")
    endif()
    if(NOT game MATCHES "^[a-z0-9_]+$")
        message(FATAL_ERROR "invalid KeelS2 game adapter name: ${game}")
    endif()

    add_library(${target} SHARED ${ARGN})
    target_link_libraries(${target} PRIVATE KeelS2::AdapterSDK)
    target_compile_definitions(${target} PRIVATE KEELS2_GAME_ADAPTER_BUILD=1)
    set_target_properties(${target} PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        OUTPUT_NAME "keels2_game_${game}"
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
