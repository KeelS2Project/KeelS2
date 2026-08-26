cmake_minimum_required(VERSION 3.25)

project(KeelS2 VERSION 0.1.0 LANGUAGES C CXX)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    enable_language(ASM_MASM)
else()
    enable_language(ASM)
endif()


include(CTest)
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

set(KEELS2_GAME "cs2" CACHE STRING "Source 2 game adapter")
set_property(CACHE KEELS2_GAME PROPERTY STRINGS cs2)

if(NOT KEELS2_GAME STREQUAL "cs2")
    message(FATAL_ERROR "Unsupported game adapter: ${KEELS2_GAME}")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "KeelS2 supports only 64-bit targets")
endif()

if(WIN32)
    set(KEELS2_PLATFORM_DIR "win64")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(KEELS2_PLATFORM_DIR "linuxsteamrt64")
else()
    message(FATAL_ERROR "KeelS2 supports only Windows and Linux")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(KEELS2_PACKAGE_ROOT "${CMAKE_BINARY_DIR}/package/addons/keels2")
file(MAKE_DIRECTORY "${KEELS2_PACKAGE_ROOT}")
file(MAKE_DIRECTORY "${KEELS2_PACKAGE_ROOT}/compatibility")
configure_file(
    games/cs2/compatibility/cs2-2000884-linuxsteamrt64.accepted.tsv
    "${KEELS2_PACKAGE_ROOT}/compatibility/cs2-2000884-linuxsteamrt64.accepted.tsv"
    COPYONLY
)
configure_file(
    games/cs2/compatibility/cs2-2000884-win64.accepted.tsv
    "${KEELS2_PACKAGE_ROOT}/compatibility/cs2-2000884-win64.accepted.tsv"
    COPYONLY
)
configure_file(
    games/cs2/compatibility/cs2-2000885-linuxsteamrt64.accepted.tsv
    "${KEELS2_PACKAGE_ROOT}/compatibility/cs2-2000885-linuxsteamrt64.accepted.tsv"
    COPYONLY
)
configure_file(
    games/cs2/compatibility/cs2-2000885-win64.accepted.tsv
    "${KEELS2_PACKAGE_ROOT}/compatibility/cs2-2000885-win64.accepted.tsv"
    COPYONLY
)
configure_file(LICENSE "${KEELS2_PACKAGE_ROOT}/LICENSE" COPYONLY)
configure_file(THIRD_PARTY_NOTICES.md "${KEELS2_PACKAGE_ROOT}/THIRD_PARTY_NOTICES.md" COPYONLY)

function(keels2_set_output target relative_directory)
    set(output_directory "${KEELS2_PACKAGE_ROOT}/${relative_directory}")
    set(symbol_directory "${CMAKE_BINARY_DIR}/symbols/${relative_directory}")
    set(link_directory "${CMAKE_BINARY_DIR}/link/${relative_directory}/${target}")
    set_target_properties(${target} PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${link_directory}"
        LIBRARY_OUTPUT_DIRECTORY "${output_directory}"
        RUNTIME_OUTPUT_DIRECTORY "${output_directory}"
        PDB_OUTPUT_DIRECTORY "${symbol_directory}"
        COMPILE_PDB_OUTPUT_DIRECTORY "${symbol_directory}"
    )

    foreach(configuration DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        set_target_properties(${target} PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY_${configuration} "${link_directory}"
            LIBRARY_OUTPUT_DIRECTORY_${configuration} "${output_directory}"
            RUNTIME_OUTPUT_DIRECTORY_${configuration} "${output_directory}"
            PDB_OUTPUT_DIRECTORY_${configuration} "${symbol_directory}"
            COMPILE_PDB_OUTPUT_DIRECTORY_${configuration} "${symbol_directory}"
        )
    endforeach()
endfunction()

function(keels2_enable_warnings target)
    if(WIN32)
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:/W4>
            $<$<COMPILE_LANGUAGE:CXX>:/permissive->
            $<$<COMPILE_LANGUAGE:CXX>:/EHsc>
        )
    else()
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:-Wall>
            $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
            $<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>
            $<$<COMPILE_LANGUAGE:CXX>:-Wconversion>
            $<$<COMPILE_LANGUAGE:CXX>:-Wsign-conversion>
        )
    endif()
endfunction()

add_library(keels2_sdk INTERFACE)
add_library(KeelS2::SDK ALIAS keels2_sdk)
set_target_properties(keels2_sdk PROPERTIES EXPORT_NAME SDK)
target_compile_features(keels2_sdk INTERFACE cxx_std_20)
target_include_directories(keels2_sdk INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
include(cmake/KeelS2SourceSDK.cmake)
keels2_provide_source_sdk()
target_link_libraries(keels2_sdk INTERFACE KeelS2::SourceSDKHeaders)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_options(keels2_sdk INTERFACE -ffunction-sections -fdata-sections)
    target_link_options(keels2_sdk INTERFACE "LINKER:--gc-sections")
elseif(MSVC)
    target_compile_options(keels2_sdk INTERFACE /Gy /Gw)
    target_link_options(keels2_sdk INTERFACE /OPT:REF /INCREMENTAL:NO)
endif()
include(cmake/KeelS2Plugin.cmake)

set(KEELS2_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/KeelS2")
configure_package_config_file(
    cmake/KeelS2Config.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/KeelS2Config.cmake"
    INSTALL_DESTINATION "${KEELS2_CMAKE_INSTALL_DIR}"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/KeelS2ConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)
install(TARGETS keels2_sdk EXPORT KeelS2SDKTargets)
install(DIRECTORY sdk/include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(
    EXPORT KeelS2SDKTargets
    FILE KeelS2SDKTargets.cmake
    NAMESPACE KeelS2::
    DESTINATION "${KEELS2_CMAKE_INSTALL_DIR}"
)
install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/KeelS2Config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/KeelS2ConfigVersion.cmake"
        cmake/KeelS2Plugin.cmake
        cmake/KeelS2SourceSDK.cmake
    DESTINATION "${KEELS2_CMAKE_INSTALL_DIR}"
)

set(ZYDIS_BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_MAN OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_DOXYGEN OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZYDIS_FEATURE_ENCODER OFF CACHE BOOL "" FORCE)
set(ZYDIS_FEATURE_FORMATTER OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/zydis EXCLUDE_FROM_ALL)

set(KEELS2_DYNCALL_SOURCES
    third_party/dyncall/dyncall/dyncall_vector.c
    third_party/dyncall/dyncall/dyncall_aggregate.c
    third_party/dyncall/dyncall/dyncall_api.c
    third_party/dyncall/dyncall/dyncall_callvm.c
    third_party/dyncall/dyncall/dyncall_callvm_base.c
    third_party/dyncall/dyncall/dyncall_callf.c
)
set(KEELS2_DYNCALLBACK_SOURCES
    third_party/dyncall/dyncallback/dyncall_alloc_wx.c
    third_party/dyncall/dyncallback/dyncall_args.c
    third_party/dyncall/dyncallback/dyncall_callback.c
    third_party/dyncall/dyncallback/dyncall_thunk.c
)
if(MSVC)
    list(APPEND KEELS2_DYNCALL_SOURCES
        third_party/dyncall/dyncall/dyncall_call_x64_generic_masm.asm
    )
    list(APPEND KEELS2_DYNCALLBACK_SOURCES
        third_party/dyncall/dyncallback/dyncall_callback_x64_masm.asm
    )
else()
    list(APPEND KEELS2_DYNCALL_SOURCES
        third_party/dyncall/dyncall/dyncall_call.S
    )
    list(APPEND KEELS2_DYNCALLBACK_SOURCES
        third_party/dyncall/dyncallback/dyncall_callback_arch.S
    )
endif()

add_library(keels2_dyncall STATIC ${KEELS2_DYNCALL_SOURCES})
target_include_directories(keels2_dyncall PUBLIC third_party/dyncall/dyncall)
set_target_properties(keels2_dyncall PROPERTIES C_VISIBILITY_PRESET hidden)

add_library(keels2_dyncallback STATIC ${KEELS2_DYNCALLBACK_SOURCES})
target_include_directories(keels2_dyncallback PUBLIC
    third_party/dyncall/dyncall
    third_party/dyncall/dyncallback
)
set_target_properties(keels2_dyncallback PROPERTIES C_VISIBILITY_PRESET hidden)

add_library(keels2_detour STATIC
    hooking/src/vtable.cpp
    third_party/safetyhook/src/allocator.cpp
    third_party/safetyhook/src/inline_hook.cpp
    third_party/safetyhook/src/os.linux.cpp
    third_party/safetyhook/src/os.windows.cpp
    third_party/safetyhook/src/utility.cpp
)
target_compile_features(keels2_detour PUBLIC cxx_std_23)
target_include_directories(keels2_detour PUBLIC
    hooking/include
    third_party/safetyhook/include
)
target_link_libraries(keels2_detour PUBLIC Zydis)
target_compile_definitions(keels2_detour PRIVATE NOMINMAX=1)
set_target_properties(keels2_detour PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

add_library(keels2_platform STATIC
    platform/src/console.cpp
    platform/src/diagnostic_trace.cpp
    platform/src/dynamic_library.cpp
    platform/src/file_fingerprint.cpp
    platform/src/loaded_module.cpp
)
target_include_directories(keels2_platform PUBLIC platform/include)
set_target_properties(keels2_platform PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)
if(CMAKE_DL_LIBS)
    target_link_libraries(keels2_platform PUBLIC ${CMAKE_DL_LIBS})
endif()
keels2_enable_warnings(keels2_platform)

add_executable(keels2_compatibility_fingerprint
    tools/compatibility_fingerprint.cpp
)
target_include_directories(keels2_compatibility_fingerprint PRIVATE platform/include)
target_link_libraries(keels2_compatibility_fingerprint PRIVATE keels2_platform)
keels2_enable_warnings(keels2_compatibility_fingerprint)

add_library(keels2_compatibility_review_lib STATIC
    tools/compatibility_review.cpp
)
target_include_directories(keels2_compatibility_review_lib PUBLIC
    tools
    platform/include
)
target_link_libraries(keels2_compatibility_review_lib PUBLIC keels2_platform)
set_target_properties(keels2_compatibility_review_lib PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)
keels2_enable_warnings(keels2_compatibility_review_lib)

add_executable(keels2_compatibility_review
    tools/compatibility_review_main.cpp
)
target_link_libraries(keels2_compatibility_review PRIVATE keels2_compatibility_review_lib)
keels2_set_output(keels2_compatibility_review "tools/${KEELS2_PLATFORM_DIR}")
keels2_enable_warnings(keels2_compatibility_review)

add_library(keels2_cs2_compatibility STATIC
    games/cs2/src/compatibility.cpp
)
target_include_directories(keels2_cs2_compatibility PUBLIC
    games/cs2/include
    platform/include
)
set_target_properties(keels2_cs2_compatibility PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)
keels2_enable_warnings(keels2_cs2_compatibility)

add_library(keels2_cs2_native_bridge STATIC
    games/cs2/src/native_bridge.cpp
)
target_include_directories(keels2_cs2_native_bridge PUBLIC
    games/cs2/include
    sdk/include
)
target_link_libraries(keels2_cs2_native_bridge PRIVATE KeelS2::SourceSDK)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # The native bridge is the other intentional game-owned C++ ABI boundary;
    # test fixtures provide the verified vtable layout without engine RTTI.
    target_compile_options(keels2_cs2_native_bridge PRIVATE -fno-sanitize=vptr)
endif()
set_target_properties(keels2_cs2_native_bridge PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)
keels2_enable_warnings(keels2_cs2_native_bridge)

add_library(keels2_bootstrap SHARED
    bootstrap/src/bootstrap.cpp
)
target_include_directories(keels2_bootstrap PRIVATE
    bootstrap/include
    games/cs2/include
    platform/include
)
target_link_libraries(keels2_bootstrap PRIVATE keels2_cs2_compatibility keels2_platform)
set_target_properties(keels2_bootstrap PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    OUTPUT_NAME server
)
keels2_set_output(keels2_bootstrap "bin/${KEELS2_PLATFORM_DIR}")
keels2_enable_warnings(keels2_bootstrap)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(keels2_bootstrap PRIVATE
        "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/bootstrap/exports.map"
        "LINKER:-z,defs"
        "LINKER:-z,relro,-z,now"
    )
endif()

add_library(keels2_host SHARED
    host/src/convar_service.cpp
    host/src/core_command.cpp
    host/src/host.cpp
    host/src/keelhook_service.cpp
    host/src/lifecycle_service.cpp
    host/src/plugin_service.cpp
    host/src/plugin_manager.cpp
    host/src/schema_entity_service.cpp
    host/src/source2_callbacks_service.cpp
    games/cs2/src/cs2_adapter.cpp
)
target_include_directories(keels2_host PRIVATE
    bootstrap/include
    games/cs2/include
    host/src
    platform/include
    sdk/include
)
target_link_libraries(keels2_host PRIVATE
    keels2_cs2_compatibility
    keels2_cs2_native_bridge
    keels2_detour
    keels2_dyncall
    keels2_dyncallback
    keels2_platform
)
if(MSVC)
    target_link_options(keels2_host PRIVATE /INCREMENTAL:NO)
endif()
set_target_properties(keels2_host PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    OUTPUT_NAME keels2_host
)
keels2_set_output(keels2_host "bin/${KEELS2_PLATFORM_DIR}")
keels2_enable_warnings(keels2_host)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(keels2_host PRIVATE
        "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/host/exports.map"
        "LINKER:-z,defs"
        "LINKER:-z,relro,-z,now"
    )
endif()

add_subdirectory(samples/basic)
keels2_set_output(keels2_basic "plugins/${KEELS2_PLATFORM_DIR}")
add_subdirectory(samples/lifecycle)
keels2_set_output(keels2_lifecycle "plugins/${KEELS2_PLATFORM_DIR}")
add_subdirectory(samples/sample)
keels2_set_output(keels2_sample "plugins/${KEELS2_PLATFORM_DIR}")
add_subdirectory(samples/runtime)
keels2_set_output(keels2_runtime "plugins/${KEELS2_PLATFORM_DIR}")
add_subdirectory(samples/callbacks)
keels2_set_output(keels2_callbacks "plugins/${KEELS2_PLATFORM_DIR}")
add_subdirectory(samples/entities)
keels2_set_output(keels2_entities "plugins/${KEELS2_PLATFORM_DIR}")

if(BUILD_TESTING)
    if(WIN32)
        add_executable(keels2_windows_crash_monitor tools/windows_crash_monitor.cpp)
        target_link_libraries(keels2_windows_crash_monitor PRIVATE Dbghelp)
        keels2_enable_warnings(keels2_windows_crash_monitor)

        add_executable(keels2_windows_crash_fixture tests/windows_crash_fixture.cpp)
        keels2_enable_warnings(keels2_windows_crash_fixture)
        add_test(
            NAME windows_crash_monitor
            COMMAND
                ${CMAKE_COMMAND}
                "-DMONITOR=$<TARGET_FILE:keels2_windows_crash_monitor>"
                "-DFIXTURE=$<TARGET_FILE:keels2_windows_crash_fixture>"
                "-DOUTPUT_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/windows-crash-monitor-contract"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/windows_crash_monitor_contract.cmake"
        )
    endif()

    if(MSVC)
        set(KEELS2_KEELHOOK_BRANCH_TARGET tests/keelhook_branch_target.asm)
    else()
        set(KEELS2_KEELHOOK_BRANCH_TARGET tests/keelhook_branch_target.S)
    endif()
    add_library(keels2_keelhook_backend_target SHARED
        tests/keelhook_backend_target.cpp
    )
    set_target_properties(keels2_keelhook_backend_target PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
    )
    keels2_enable_warnings(keels2_keelhook_backend_target)

    add_executable(keels2_keelhook_backend
        tests/dyncallback_bool.c
        tests/keelhook_backend.cpp
        ${KEELS2_KEELHOOK_BRANCH_TARGET}
    )
    target_link_libraries(keels2_keelhook_backend PRIVATE
        keels2_detour
        keels2_dyncall
        keels2_dyncallback
        keels2_keelhook_backend_target
    )
    if(MSVC)
        target_link_options(keels2_keelhook_backend PRIVATE /INCREMENTAL:NO)
    endif()
    keels2_enable_warnings(keels2_keelhook_backend)
    add_test(NAME keelhook_backend COMMAND keels2_keelhook_backend)

    add_executable(keels2_aggregate_call
        tests/aggregate_call.cpp
        tests/keelhook_virtual_fixture.cpp
    )
    target_include_directories(keels2_aggregate_call PRIVATE tests)
    target_link_libraries(keels2_aggregate_call PRIVATE
        keels2_dyncall
        keels2_dyncallback
    )
    keels2_enable_warnings(keels2_aggregate_call)
    add_test(NAME aggregate_call COMMAND keels2_aggregate_call)

    add_executable(keels2_vtable_hook
        tests/vtable_hook.cpp
        tests/keelhook_virtual_fixture.cpp
    )
    target_include_directories(keels2_vtable_hook PRIVATE tests)
    target_link_libraries(keels2_vtable_hook PRIVATE keels2_detour)
    keels2_enable_warnings(keels2_vtable_hook)
    add_test(NAME vtable_hook COMMAND keels2_vtable_hook)

    add_library(keels2_bootstrap_test SHARED
        bootstrap/src/bootstrap.cpp
    )
    target_include_directories(keels2_bootstrap_test PRIVATE
        bootstrap/include
        games/cs2/include
        platform/include
    )
    target_compile_definitions(keels2_bootstrap_test PRIVATE KEELS2_TEST_COMPATIBILITY=1)
    target_link_libraries(keels2_bootstrap_test PRIVATE keels2_cs2_compatibility keels2_platform)
    set_target_properties(keels2_bootstrap_test PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        OUTPUT_NAME keels2_bootstrap_test
    )
    keels2_enable_warnings(keels2_bootstrap_test)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(keels2_bootstrap_test PRIVATE
            "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/bootstrap/exports.map"
            "LINKER:-z,defs"
            "LINKER:-z,relro,-z,now"
        )
    endif()

    add_library(keels2_fake_tier0 SHARED tests/fake_tier0.cpp)
    set_target_properties(keels2_fake_tier0 PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        OUTPUT_NAME tier0
    )
    keels2_enable_warnings(keels2_fake_tier0)

    add_executable(keels2_console_output tests/console_output.cpp)
    target_include_directories(keels2_console_output PRIVATE platform/include)
    target_link_libraries(keels2_console_output PRIVATE keels2_platform)
    keels2_enable_warnings(keels2_console_output)
    add_test(
        NAME console_output
        COMMAND keels2_console_output $<TARGET_FILE:keels2_fake_tier0>
    )

    add_executable(keels2_abi_layout tests/abi_layout.cpp)
    target_include_directories(keels2_abi_layout PRIVATE
        bootstrap/include
        games/cs2/include
        sdk/include
    )
    keels2_enable_warnings(keels2_abi_layout)
    add_test(NAME abi_layout COMMAND keels2_abi_layout)

    add_executable(keels2_plugin_logging tests/plugin_logging.cpp)
    target_include_directories(keels2_plugin_logging PRIVATE sdk/include)
    keels2_enable_warnings(keels2_plugin_logging)
    add_test(NAME plugin_logging COMMAND keels2_plugin_logging)

    add_executable(keels2_source2_c_abi tests/source2_c_abi.c)
    target_include_directories(keels2_source2_c_abi PRIVATE sdk/include)
    set_target_properties(keels2_source2_c_abi PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS OFF
    )
    add_test(NAME source2_c_abi COMMAND keels2_source2_c_abi)

    add_executable(keels2_lifecycle_c_abi tests/lifecycle_c_abi.c)
    target_include_directories(keels2_lifecycle_c_abi PRIVATE sdk/include)
    set_target_properties(keels2_lifecycle_c_abi PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS OFF
    )
    add_test(NAME lifecycle_c_abi COMMAND keels2_lifecycle_c_abi)

    add_executable(keels2_convar_c_abi tests/convar_c_abi.c)
    target_include_directories(keels2_convar_c_abi PRIVATE sdk/include)
    set_target_properties(keels2_convar_c_abi PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS OFF
    )
    add_test(NAME convar_c_abi COMMAND keels2_convar_c_abi)

    add_executable(keels2_plugins_c_abi tests/plugins_c_abi.c)
    target_include_directories(keels2_plugins_c_abi PRIVATE sdk/include)
    set_target_properties(keels2_plugins_c_abi PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS OFF
    )
    add_test(NAME plugins_c_abi COMMAND keels2_plugins_c_abi)

    add_executable(keels2_source2_callbacks_c_abi tests/source2_callbacks_c_abi.c)
    target_include_directories(keels2_source2_callbacks_c_abi PRIVATE sdk/include)
    set_target_properties(keels2_source2_callbacks_c_abi PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS OFF
    )
    add_test(NAME source2_callbacks_c_abi COMMAND keels2_source2_callbacks_c_abi)

    add_executable(keels2_schema_entities_c_abi tests/schema_entities_c_abi.c)
    target_include_directories(keels2_schema_entities_c_abi PRIVATE sdk/include)
    set_target_properties(keels2_schema_entities_c_abi PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS OFF
    )
    add_test(NAME schema_entities_c_abi COMMAND keels2_schema_entities_c_abi)

    add_executable(keels2_compatibility_test tests/compatibility.cpp)
    target_include_directories(keels2_compatibility_test PRIVATE
        games/cs2/include
        platform/include
    )
    target_link_libraries(keels2_compatibility_test PRIVATE keels2_cs2_compatibility)
    keels2_enable_warnings(keels2_compatibility_test)
    add_test(NAME compatibility COMMAND keels2_compatibility_test)

    add_executable(keels2_compatibility_review_test tests/compatibility_review.cpp)
    target_include_directories(keels2_compatibility_review_test PRIVATE
        games/cs2/include
        platform/include
        tools
    )
    target_link_libraries(keels2_compatibility_review_test PRIVATE
        keels2_compatibility_review_lib
        keels2_cs2_compatibility
    )
    keels2_enable_warnings(keels2_compatibility_review_test)
    add_test(
        NAME compatibility_review
        COMMAND keels2_compatibility_review_test
            "${CMAKE_BINARY_DIR}/compatibility-review-test"
            "${CMAKE_CURRENT_SOURCE_DIR}/games/cs2/compatibility/cs2-2000884-linuxsteamrt64.accepted.tsv"
            "${CMAKE_CURRENT_SOURCE_DIR}/games/cs2/compatibility/cs2-2000884-win64.accepted.tsv"
            "${CMAKE_CURRENT_SOURCE_DIR}/games/cs2/compatibility/cs2-2000885-linuxsteamrt64.accepted.tsv"
            "${CMAKE_CURRENT_SOURCE_DIR}/games/cs2/compatibility/cs2-2000885-win64.accepted.tsv"
    )

    add_executable(keels2_file_fingerprint tests/file_fingerprint.cpp)
    target_include_directories(keels2_file_fingerprint PRIVATE platform/include)
    target_link_libraries(keels2_file_fingerprint PRIVATE keels2_platform)
    keels2_enable_warnings(keels2_file_fingerprint)
    add_test(
        NAME file_fingerprint
        COMMAND keels2_file_fingerprint ${CMAKE_BINARY_DIR}/file-fingerprint-test.bin
    )

    add_executable(keels2_plugin_contract
        tests/plugin_contract.cpp
        tests/source2_command_fixture.cpp
    )
    target_include_directories(keels2_plugin_contract PRIVATE
        platform/include
        sdk/include
        "${KEELS2_SOURCE_SDK_RESOLVED_ROOT}/public"
        "${KEELS2_SOURCE_SDK_RESOLVED_ROOT}/public/engine"
        "${KEELS2_SOURCE_SDK_RESOLVED_ROOT}/public/mathlib"
        "${KEELS2_SOURCE_SDK_RESOLVED_ROOT}/public/tier0"
        "${KEELS2_SOURCE_SDK_RESOLVED_ROOT}/public/tier1"
    )
    target_compile_definitions(keels2_plugin_contract PRIVATE
        SOURCE_ENGINE=25
        GAME_DLL
        RAD_TELEMETRY_DISABLED
        X64BITS
        PLATFORM_64BITS
    )
    if(MSVC)
        target_compile_definitions(keels2_plugin_contract PRIVATE
            COMPILER_MSVC
            COMPILER_MSVC64
            WIN32
            WIN64
        )
        target_compile_options(keels2_plugin_contract PRIVATE /Gy /Gw)
        target_link_options(keels2_plugin_contract PRIVATE /OPT:REF /INCREMENTAL:NO)
    else()
        target_compile_definitions(keels2_plugin_contract PRIVATE
            COMPILER_GCC
            _LINUX
            LINUX
            POSIX
            GNUC
            stricmp=strcasecmp
            _stricmp=strcasecmp
            _snprintf=snprintf
            _vsnprintf=vsnprintf
        )
        target_compile_options(keels2_plugin_contract PRIVATE
            -ffunction-sections
            -fdata-sections
        )
        target_link_options(keels2_plugin_contract PRIVATE "LINKER:--gc-sections")
    endif()
    target_link_libraries(keels2_plugin_contract PRIVATE keels2_platform)
    keels2_enable_warnings(keels2_plugin_contract)
    add_test(NAME plugin_contract COMMAND keels2_plugin_contract $<TARGET_FILE:keels2_basic>)
    add_library(keels2_authoring_contract_plugin SHARED
        tests/authoring_contract_plugin.cpp
        tests/source2_command_fixture.cpp
    )
    target_include_directories(keels2_authoring_contract_plugin PRIVATE sdk/include)
    target_link_libraries(keels2_authoring_contract_plugin PRIVATE KeelS2::SDK)
    target_compile_definitions(keels2_authoring_contract_plugin PRIVATE KEELS2_PLUGIN_BUILD=1)
    set_target_properties(keels2_authoring_contract_plugin PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        PREFIX ""
        OUTPUT_NAME keels2_authoring_contract_plugin
    )
    keels2_enable_warnings(keels2_authoring_contract_plugin)
    add_executable(keels2_authoring_contract tests/authoring_contract.cpp)
    target_include_directories(keels2_authoring_contract PRIVATE platform/include sdk/include)
    target_link_libraries(keels2_authoring_contract PRIVATE keels2_platform)
    keels2_enable_warnings(keels2_authoring_contract)
    add_test(
        NAME authoring_contract
        COMMAND keels2_authoring_contract $<TARGET_FILE:keels2_authoring_contract_plugin>
    )
    add_library(keels2_authoring_api_contract_plugin SHARED
        tests/authoring_api_contract_plugin.cpp
    )
    target_include_directories(keels2_authoring_api_contract_plugin PRIVATE sdk/include)
    target_link_libraries(keels2_authoring_api_contract_plugin PRIVATE KeelS2::SDK KeelS2::SourceSDK)
    target_compile_definitions(
        keels2_authoring_api_contract_plugin PRIVATE KEELS2_PLUGIN_BUILD=1)
    set_target_properties(keels2_authoring_api_contract_plugin PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        PREFIX ""
        OUTPUT_NAME keels2_authoring_api_contract_plugin
    )
    keels2_enable_warnings(keels2_authoring_api_contract_plugin)
    add_library(keels2_source_sdk_contract OBJECT tests/source_sdk_contract.cpp)
    target_include_directories(keels2_source_sdk_contract PRIVATE sdk/include)
    target_link_libraries(keels2_source_sdk_contract PRIVATE KeelS2::SDK KeelS2::SourceSDK)
    keels2_enable_warnings(keels2_source_sdk_contract)
    add_library(keels2_source_sdk_registration_contract SHARED
        tests/source_sdk_registration_contract.cpp
    )
    target_include_directories(
        keels2_source_sdk_registration_contract PRIVATE sdk/include)
    target_link_libraries(
        keels2_source_sdk_registration_contract PRIVATE KeelS2::SourceSDK)
    set_target_properties(keels2_source_sdk_registration_contract PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        PREFIX ""
        OUTPUT_NAME keels2_source_sdk_registration_contract
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(
            keels2_source_sdk_registration_contract PRIVATE "LINKER:-z,defs")
    endif()
    keels2_enable_warnings(keels2_source_sdk_registration_contract)
    add_library(keels2_plugin_authoring_runtime_plugin SHARED
        tests/plugin_authoring_runtime_plugin.cpp
    )
    target_include_directories(keels2_plugin_authoring_runtime_plugin PRIVATE sdk/include)
    target_link_libraries(keels2_plugin_authoring_runtime_plugin PRIVATE KeelS2::SDK KeelS2::SourceSDK)
    target_compile_definitions(
        keels2_plugin_authoring_runtime_plugin PRIVATE KEELS2_PLUGIN_BUILD=1)
    set_target_properties(keels2_plugin_authoring_runtime_plugin PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        PREFIX ""
        OUTPUT_NAME keels2_plugin_authoring_runtime_plugin
    )
    keels2_enable_warnings(keels2_plugin_authoring_runtime_plugin)
    add_executable(keels2_plugin_authoring_runtime_contract
        tests/plugin_authoring_runtime_contract.cpp
    )
    target_include_directories(keels2_plugin_authoring_runtime_contract PRIVATE
        platform/include
        sdk/include
    )
    target_link_libraries(keels2_plugin_authoring_runtime_contract PRIVATE keels2_platform)
    keels2_enable_warnings(keels2_plugin_authoring_runtime_contract)
    add_test(
        NAME plugin_authoring_runtime_contract
        COMMAND keels2_plugin_authoring_runtime_contract
            $<TARGET_FILE:keels2_plugin_authoring_runtime_plugin>
    )
    add_executable(keels2_lifecycle_sample_contract tests/lifecycle_sample_contract.cpp)
    target_include_directories(keels2_lifecycle_sample_contract PRIVATE platform/include sdk/include)
    target_link_libraries(keels2_lifecycle_sample_contract PRIVATE keels2_platform)
    keels2_enable_warnings(keels2_lifecycle_sample_contract)
    add_test(
        NAME lifecycle_plugin_contract
        COMMAND keels2_lifecycle_sample_contract $<TARGET_FILE:keels2_lifecycle>
    )

    set(KEELS2_SDK_INSTALL_DIR "${CMAKE_BINARY_DIR}/sdk-install")
    set(KEELS2_STANDALONE_BUILD_DIR "${CMAKE_BINARY_DIR}/standalone-basic")
    add_test(
        NAME sdk_install
        COMMAND ${CMAKE_COMMAND}
            --install "${CMAKE_BINARY_DIR}"
            --prefix "${KEELS2_SDK_INSTALL_DIR}"
            --config $<CONFIG>
    )
    set(KEELS2_STANDALONE_CONFIGURE
        ${CMAKE_COMMAND}
        --fresh
        -S "${CMAKE_CURRENT_SOURCE_DIR}/samples/basic"
        -B "${KEELS2_STANDALONE_BUILD_DIR}"
        "-DKeelS2_DIR=${KEELS2_SDK_INSTALL_DIR}/${KEELS2_CMAKE_INSTALL_DIR}"
        "-DKEELS2_SOURCE_SDK_ROOT=${KEELS2_SOURCE_SDK_RESOLVED_ROOT}"
    )
    if(CMAKE_GENERATOR)
        list(APPEND KEELS2_STANDALONE_CONFIGURE -G "${CMAKE_GENERATOR}")
    endif()
    if(CMAKE_GENERATOR_PLATFORM)
        list(APPEND KEELS2_STANDALONE_CONFIGURE -A "${CMAKE_GENERATOR_PLATFORM}")
    endif()
    if(CMAKE_GENERATOR_TOOLSET)
        list(APPEND KEELS2_STANDALONE_CONFIGURE -T "${CMAKE_GENERATOR_TOOLSET}")
    endif()
    get_property(KEELS2_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(NOT KEELS2_MULTI_CONFIG)
        list(APPEND KEELS2_STANDALONE_CONFIGURE "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
    endif()
    add_test(NAME sdk_out_of_tree_configure COMMAND ${KEELS2_STANDALONE_CONFIGURE})
    set_tests_properties(sdk_out_of_tree_configure PROPERTIES DEPENDS sdk_install)
    add_test(
        NAME sdk_out_of_tree_build
        COMMAND ${CMAKE_COMMAND}
            --build "${KEELS2_STANDALONE_BUILD_DIR}"
            --config $<CONFIG>
            --parallel
    )
    set_tests_properties(sdk_out_of_tree_build PROPERTIES DEPENDS sdk_out_of_tree_configure)

    set(KEELS2_STANDALONE_LIFECYCLE_BUILD_DIR "${CMAKE_BINARY_DIR}/standalone-lifecycle")
    set(KEELS2_STANDALONE_LIFECYCLE_CONFIGURE
        ${CMAKE_COMMAND}
        --fresh
        -S "${CMAKE_CURRENT_SOURCE_DIR}/samples/lifecycle"
        -B "${KEELS2_STANDALONE_LIFECYCLE_BUILD_DIR}"
        "-DKeelS2_DIR=${KEELS2_SDK_INSTALL_DIR}/${KEELS2_CMAKE_INSTALL_DIR}"
        "-DKEELS2_SOURCE_SDK_ROOT=${KEELS2_SOURCE_SDK_RESOLVED_ROOT}"
    )
    if(CMAKE_GENERATOR)
        list(APPEND KEELS2_STANDALONE_LIFECYCLE_CONFIGURE -G "${CMAKE_GENERATOR}")
    endif()
    if(CMAKE_GENERATOR_PLATFORM)
        list(APPEND KEELS2_STANDALONE_LIFECYCLE_CONFIGURE -A "${CMAKE_GENERATOR_PLATFORM}")
    endif()
    if(CMAKE_GENERATOR_TOOLSET)
        list(APPEND KEELS2_STANDALONE_LIFECYCLE_CONFIGURE -T "${CMAKE_GENERATOR_TOOLSET}")
    endif()
    if(NOT KEELS2_MULTI_CONFIG)
        list(APPEND KEELS2_STANDALONE_LIFECYCLE_CONFIGURE "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
    endif()
    add_test(
        NAME sdk_lifecycle_out_of_tree_configure
        COMMAND ${KEELS2_STANDALONE_LIFECYCLE_CONFIGURE}
    )
    set_tests_properties(sdk_lifecycle_out_of_tree_configure PROPERTIES DEPENDS sdk_install)
    add_test(
        NAME sdk_lifecycle_out_of_tree_build
        COMMAND ${CMAKE_COMMAND}
            --build "${KEELS2_STANDALONE_LIFECYCLE_BUILD_DIR}"
            --config $<CONFIG>
            --parallel
    )
    set_tests_properties(
        sdk_lifecycle_out_of_tree_build
        PROPERTIES DEPENDS sdk_lifecycle_out_of_tree_configure
    )

    set(KEELS2_STANDALONE_SAMPLE_BUILD_DIR "${CMAKE_BINARY_DIR}/standalone-sample")
    set(KEELS2_STANDALONE_SAMPLE_CONFIGURE
        ${CMAKE_COMMAND}
        --fresh
        -S "${CMAKE_CURRENT_SOURCE_DIR}/samples/sample"
        -B "${KEELS2_STANDALONE_SAMPLE_BUILD_DIR}"
        "-DKeelS2_DIR=${KEELS2_SDK_INSTALL_DIR}/${KEELS2_CMAKE_INSTALL_DIR}"
        "-DKEELS2_SOURCE_SDK_ROOT=${KEELS2_SOURCE_SDK_RESOLVED_ROOT}"
    )
    if(CMAKE_GENERATOR)
        list(APPEND KEELS2_STANDALONE_SAMPLE_CONFIGURE -G "${CMAKE_GENERATOR}")
    endif()
    if(CMAKE_GENERATOR_PLATFORM)
        list(APPEND KEELS2_STANDALONE_SAMPLE_CONFIGURE -A "${CMAKE_GENERATOR_PLATFORM}")
    endif()
    if(CMAKE_GENERATOR_TOOLSET)
        list(APPEND KEELS2_STANDALONE_SAMPLE_CONFIGURE -T "${CMAKE_GENERATOR_TOOLSET}")
    endif()
    if(NOT KEELS2_MULTI_CONFIG)
        list(APPEND KEELS2_STANDALONE_SAMPLE_CONFIGURE "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
    endif()
    add_test(
        NAME sdk_sample_out_of_tree_configure
        COMMAND ${KEELS2_STANDALONE_SAMPLE_CONFIGURE}
    )
    set_tests_properties(sdk_sample_out_of_tree_configure PROPERTIES DEPENDS sdk_install)
    add_test(
        NAME sdk_sample_out_of_tree_build
        COMMAND ${CMAKE_COMMAND}
            --build "${KEELS2_STANDALONE_SAMPLE_BUILD_DIR}"
            --config $<CONFIG>
            --parallel
    )
    set_tests_properties(
        sdk_sample_out_of_tree_build
        PROPERTIES DEPENDS sdk_sample_out_of_tree_configure
    )

    set(KEELS2_STANDALONE_RUNTIME_BUILD_DIR "${CMAKE_BINARY_DIR}/standalone-runtime")
    set(KEELS2_STANDALONE_RUNTIME_CONFIGURE
        ${CMAKE_COMMAND}
        --fresh
        -S "${CMAKE_CURRENT_SOURCE_DIR}/samples/runtime"
        -B "${KEELS2_STANDALONE_RUNTIME_BUILD_DIR}"
        "-DKeelS2_DIR=${KEELS2_SDK_INSTALL_DIR}/${KEELS2_CMAKE_INSTALL_DIR}"
        "-DKEELS2_SOURCE_SDK_ROOT=${KEELS2_SOURCE_SDK_RESOLVED_ROOT}"
    )
    if(CMAKE_GENERATOR)
        list(APPEND KEELS2_STANDALONE_RUNTIME_CONFIGURE -G "${CMAKE_GENERATOR}")
    endif()
    if(CMAKE_GENERATOR_PLATFORM)
        list(APPEND KEELS2_STANDALONE_RUNTIME_CONFIGURE -A "${CMAKE_GENERATOR_PLATFORM}")
    endif()
    if(CMAKE_GENERATOR_TOOLSET)
        list(APPEND KEELS2_STANDALONE_RUNTIME_CONFIGURE -T "${CMAKE_GENERATOR_TOOLSET}")
    endif()
    if(NOT KEELS2_MULTI_CONFIG)
        list(APPEND KEELS2_STANDALONE_RUNTIME_CONFIGURE "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
    endif()
    add_test(
        NAME sdk_runtime_out_of_tree_configure
        COMMAND ${KEELS2_STANDALONE_RUNTIME_CONFIGURE}
    )
    set_tests_properties(sdk_runtime_out_of_tree_configure PROPERTIES DEPENDS sdk_install)
    add_test(
        NAME sdk_runtime_out_of_tree_build
        COMMAND ${CMAKE_COMMAND}
            --build "${KEELS2_STANDALONE_RUNTIME_BUILD_DIR}"
            --config $<CONFIG>
            --parallel
    )
    set_tests_properties(
        sdk_runtime_out_of_tree_build
        PROPERTIES DEPENDS sdk_runtime_out_of_tree_configure
    )

    set(KEELS2_STANDALONE_CALLBACKS_BUILD_DIR "${CMAKE_BINARY_DIR}/standalone-callbacks")
    set(KEELS2_STANDALONE_CALLBACKS_CONFIGURE
        ${CMAKE_COMMAND}
        --fresh
        -S "${CMAKE_CURRENT_SOURCE_DIR}/samples/callbacks"
        -B "${KEELS2_STANDALONE_CALLBACKS_BUILD_DIR}"
        "-DKeelS2_DIR=${KEELS2_SDK_INSTALL_DIR}/${KEELS2_CMAKE_INSTALL_DIR}"
        "-DKEELS2_SOURCE_SDK_ROOT=${KEELS2_SOURCE_SDK_RESOLVED_ROOT}"
    )
    if(CMAKE_GENERATOR)
        list(APPEND KEELS2_STANDALONE_CALLBACKS_CONFIGURE -G "${CMAKE_GENERATOR}")
    endif()
    if(CMAKE_GENERATOR_PLATFORM)
        list(APPEND KEELS2_STANDALONE_CALLBACKS_CONFIGURE -A "${CMAKE_GENERATOR_PLATFORM}")
    endif()
    if(CMAKE_GENERATOR_TOOLSET)
        list(APPEND KEELS2_STANDALONE_CALLBACKS_CONFIGURE -T "${CMAKE_GENERATOR_TOOLSET}")
    endif()
    if(NOT KEELS2_MULTI_CONFIG)
        list(APPEND KEELS2_STANDALONE_CALLBACKS_CONFIGURE
            "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
    endif()
    add_test(
        NAME sdk_callbacks_out_of_tree_configure
        COMMAND ${KEELS2_STANDALONE_CALLBACKS_CONFIGURE}
    )
    set_tests_properties(sdk_callbacks_out_of_tree_configure PROPERTIES DEPENDS sdk_install)
    add_test(
        NAME sdk_callbacks_out_of_tree_build
        COMMAND ${CMAKE_COMMAND}
            --build "${KEELS2_STANDALONE_CALLBACKS_BUILD_DIR}"
            --config $<CONFIG>
            --parallel
    )
    set_tests_properties(
        sdk_callbacks_out_of_tree_build
        PROPERTIES DEPENDS sdk_callbacks_out_of_tree_configure
    )

    set(KEELS2_STANDALONE_ENTITIES_BUILD_DIR "${CMAKE_BINARY_DIR}/standalone-entities")
    set(KEELS2_STANDALONE_ENTITIES_CONFIGURE
        ${CMAKE_COMMAND}
        --fresh
        -S "${CMAKE_CURRENT_SOURCE_DIR}/samples/entities"
        -B "${KEELS2_STANDALONE_ENTITIES_BUILD_DIR}"
        "-DKeelS2_DIR=${KEELS2_SDK_INSTALL_DIR}/${KEELS2_CMAKE_INSTALL_DIR}"
        "-DKEELS2_SOURCE_SDK_ROOT=${KEELS2_SOURCE_SDK_RESOLVED_ROOT}"
    )
    if(CMAKE_GENERATOR)
        list(APPEND KEELS2_STANDALONE_ENTITIES_CONFIGURE -G "${CMAKE_GENERATOR}")
    endif()
    if(CMAKE_GENERATOR_PLATFORM)
        list(APPEND KEELS2_STANDALONE_ENTITIES_CONFIGURE -A "${CMAKE_GENERATOR_PLATFORM}")
    endif()
    if(CMAKE_GENERATOR_TOOLSET)
        list(APPEND KEELS2_STANDALONE_ENTITIES_CONFIGURE -T "${CMAKE_GENERATOR_TOOLSET}")
    endif()
    if(NOT KEELS2_MULTI_CONFIG)
        list(APPEND KEELS2_STANDALONE_ENTITIES_CONFIGURE
            "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
    endif()
    add_test(
        NAME sdk_entities_out_of_tree_configure
        COMMAND ${KEELS2_STANDALONE_ENTITIES_CONFIGURE}
    )
    set_tests_properties(sdk_entities_out_of_tree_configure PROPERTIES DEPENDS sdk_install)
    add_test(
        NAME sdk_entities_out_of_tree_build
        COMMAND ${CMAKE_COMMAND}
            --build "${KEELS2_STANDALONE_ENTITIES_BUILD_DIR}"
            --config $<CONFIG>
            --parallel
    )
    set_tests_properties(
        sdk_entities_out_of_tree_build
        PROPERTIES DEPENDS sdk_entities_out_of_tree_configure
    )

    add_library(keels2_fake_server SHARED tests/fake_server.cpp)
    target_include_directories(keels2_fake_server PRIVATE bootstrap/include)
    set_target_properties(keels2_fake_server PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        OUTPUT_NAME server
    )
    keels2_enable_warnings(keels2_fake_server)

    add_library(keels2_schema_entity_fixture SHARED tests/schema_entity_fixture.cpp)
    target_link_libraries(keels2_schema_entity_fixture PRIVATE
        keels2_fake_tier0
        KeelS2::SourceSDKHeaders
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(keels2_schema_entity_fixture PRIVATE -fno-sanitize=vptr)
    endif()
    set_target_properties(keels2_schema_entity_fixture PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        PREFIX ""
        OUTPUT_NAME keels2_schema_entity_fixture
    )
    keels2_enable_warnings(keels2_schema_entity_fixture)

    add_executable(keels2_schema_entity_native_bridge tests/schema_entity_fixture.cpp)
    target_compile_definitions(keels2_schema_entity_native_bridge PRIVATE
        KEELS2_SCHEMA_FIXTURE_NATIVE_TEST=1
    )
    target_link_libraries(keels2_schema_entity_native_bridge PRIVATE
        keels2_cs2_native_bridge
        keels2_fake_tier0
        KeelS2::SourceSDKHeaders
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(keels2_schema_entity_native_bridge PRIVATE -fno-sanitize=vptr)
    endif()
    keels2_enable_warnings(keels2_schema_entity_native_bridge)
    add_test(NAME schema_entity_native_bridge COMMAND keels2_schema_entity_native_bridge)

    add_library(keels2_invalid_plugin SHARED tests/invalid_plugin.cpp)
    target_include_directories(keels2_invalid_plugin PRIVATE sdk/include)
    target_compile_definitions(keels2_invalid_plugin PRIVATE KEELS2_PLUGIN_BUILD=1)
    set_target_properties(keels2_invalid_plugin PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        PREFIX ""
        OUTPUT_NAME keels2_invalid_plugin
    )
    keels2_enable_warnings(keels2_invalid_plugin)

    add_library(keels2_failing_plugin SHARED tests/failing_plugin.cpp)
    target_include_directories(keels2_failing_plugin PRIVATE sdk/include)
    target_compile_definitions(keels2_failing_plugin PRIVATE KEELS2_PLUGIN_BUILD=1)
    set_target_properties(keels2_failing_plugin PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        PREFIX ""
        OUTPUT_NAME keels2_failing_plugin
    )
    keels2_enable_warnings(keels2_failing_plugin)

    foreach(test_plugin reserved_command duplicate_command)
        add_library(keels2_${test_plugin}_plugin SHARED tests/${test_plugin}_plugin.cpp)
        target_include_directories(keels2_${test_plugin}_plugin PRIVATE sdk/include)
        target_compile_definitions(keels2_${test_plugin}_plugin PRIVATE KEELS2_PLUGIN_BUILD=1)
        set_target_properties(keels2_${test_plugin}_plugin PROPERTIES
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN YES
            PREFIX ""
            OUTPUT_NAME keels2_${test_plugin}_plugin
        )
        keels2_enable_warnings(keels2_${test_plugin}_plugin)
    endforeach()

    foreach(position first second)
        add_library(keels2_lifecycle_${position}_plugin SHARED tests/lifecycle_plugin.cpp)
        target_include_directories(keels2_lifecycle_${position}_plugin PRIVATE sdk/include)
        target_compile_definitions(keels2_lifecycle_${position}_plugin PRIVATE KEELS2_PLUGIN_BUILD=1)
        if(position STREQUAL "first")
            target_compile_definitions(keels2_lifecycle_${position}_plugin PRIVATE KEELS2_TEST_PLUGIN_FIRST=1)
        endif()
        set_target_properties(keels2_lifecycle_${position}_plugin PROPERTIES
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN YES
            PREFIX ""
            OUTPUT_NAME keels2_lifecycle_${position}_plugin
        )
        keels2_enable_warnings(keels2_lifecycle_${position}_plugin)
    endforeach()

    find_package(Threads REQUIRED)
    if(MSVC)
        set(KEELS2_KEELHOOK_TEST_PROFILE "test-fixture-win64")
        set(KEELS2_KEELHOOK_LIVE_PROFILE "cs2-2000885-win64-2369e67d8d0e4475a49dc6f4e8c99d28-55")
        set(KEELS2_SOURCE2_TEST_CVAR_MODULE "keels2_bootstrap_integration.exe")
        set(KEELS2_SOURCE2_LIVE_CVAR_MODULE "tier0.dll")
    else()
        set(KEELS2_KEELHOOK_TEST_PROFILE "test-fixture-linuxsteamrt64")
        set(KEELS2_KEELHOOK_LIVE_PROFILE "cs2-2000885-linuxsteamrt64-d05aa2d65efa96e06e3ded6dd2a95b5220a993a8")
        set(KEELS2_SOURCE2_TEST_CVAR_MODULE "keels2_bootstrap_integration")
        set(KEELS2_SOURCE2_LIVE_CVAR_MODULE "libtier0.so")
    endif()
    add_library(keels2_keelhook_target_plugin SHARED
        tests/keelhook_target_plugin.cpp
        tests/keelhook_virtual_fixture.cpp
    )
    add_library(keels2_keelhook_live_target_plugin SHARED
        tests/keelhook_target_plugin.cpp
        tests/keelhook_virtual_fixture.cpp
    )
    foreach(target IN ITEMS keels2_keelhook_target_plugin keels2_keelhook_live_target_plugin)
        target_include_directories(${target} PRIVATE sdk/include)
        target_compile_definitions(${target} PRIVATE KEELS2_PLUGIN_BUILD=1)
        target_link_libraries(${target} PRIVATE Threads::Threads)
        set_target_properties(${target} PROPERTIES
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN YES
            PREFIX ""
        )
        keels2_enable_warnings(${target})
    endforeach()
    target_compile_definitions(keels2_keelhook_target_plugin PRIVATE
        KEELHOOK_FIXTURE_PROFILE="${KEELS2_KEELHOOK_TEST_PROFILE}"
    )
    target_compile_definitions(keels2_keelhook_live_target_plugin PRIVATE
        KEELHOOK_FIXTURE_PROFILE="${KEELS2_KEELHOOK_LIVE_PROFILE}"
    )
    set_target_properties(keels2_keelhook_target_plugin PROPERTIES OUTPUT_NAME 01_keelhook_target)
    set_target_properties(keels2_keelhook_live_target_plugin PROPERTIES OUTPUT_NAME 01_keelhook_live_target)

    add_library(keels2_keelhook_peer_plugin SHARED tests/keelhook_peer_plugin.cpp)
    target_include_directories(keels2_keelhook_peer_plugin PRIVATE sdk/include)
    target_compile_definitions(keels2_keelhook_peer_plugin PRIVATE KEELS2_PLUGIN_BUILD=1)
    set_target_properties(keels2_keelhook_peer_plugin PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        PREFIX ""
        OUTPUT_NAME 02_keelhook_peer
    )
    keels2_enable_warnings(keels2_keelhook_peer_plugin)

    add_library(keels2_source2_service_plugin SHARED tests/source2_service_plugin.cpp)
    add_library(keels2_source2_live_plugin SHARED tests/source2_service_plugin.cpp)
    foreach(target IN ITEMS keels2_source2_service_plugin keels2_source2_live_plugin)
        target_include_directories(${target} PRIVATE sdk/include)
        target_compile_definitions(${target} PRIVATE KEELS2_PLUGIN_BUILD=1)
        set_target_properties(${target} PROPERTIES
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN YES
            PREFIX ""
        )
        keels2_enable_warnings(${target})
    endforeach()
    target_compile_definitions(keels2_source2_service_plugin PRIVATE
        KEELS2_SOURCE2_EXPECTED_PROFILE="${KEELS2_KEELHOOK_TEST_PROFILE}"
        KEELS2_SOURCE2_EXPECTED_CVAR_MODULE="${KEELS2_SOURCE2_TEST_CVAR_MODULE}"
    )
    target_compile_definitions(keels2_source2_live_plugin PRIVATE
        KEELS2_SOURCE2_EXPECTED_PROFILE="${KEELS2_KEELHOOK_LIVE_PROFILE}"
        KEELS2_SOURCE2_EXPECTED_CVAR_MODULE="${KEELS2_SOURCE2_LIVE_CVAR_MODULE}"
    )
    set_target_properties(keels2_source2_service_plugin PROPERTIES
        OUTPUT_NAME keels2_source2_service_plugin
    )
    set_target_properties(keels2_source2_live_plugin PROPERTIES
        OUTPUT_NAME 01_source2_live
    )

    add_library(keels2_schema_entity_service_plugin SHARED
        tests/schema_entity_service_plugin.cpp
    )
    target_link_libraries(keels2_schema_entity_service_plugin PRIVATE
        KeelS2::SDK
        KeelS2::SourceSDK
    )
    target_compile_definitions(keels2_schema_entity_service_plugin PRIVATE
        KEELS2_PLUGIN_BUILD=1
    )
    set_target_properties(keels2_schema_entity_service_plugin PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        PREFIX ""
        OUTPUT_NAME keels2_schema_entity_service_plugin
    )
    keels2_enable_warnings(keels2_schema_entity_service_plugin)

    add_library(keels2_schema_entity_live_plugin SHARED
        tests/schema_entity_live_acceptance_plugin.cpp
    )
    target_link_libraries(keels2_schema_entity_live_plugin PRIVATE
        KeelS2::SDK
        KeelS2::SourceSDK
    )
    target_compile_definitions(keels2_schema_entity_live_plugin PRIVATE
        KEELS2_PLUGIN_BUILD=1
    )
    set_target_properties(keels2_schema_entity_live_plugin PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        PREFIX ""
        OUTPUT_NAME 01_schema_entity_live
    )
    keels2_enable_warnings(keels2_schema_entity_live_plugin)

    add_library(keels2_lifecycle_service_plugin SHARED tests/lifecycle_service_plugin.cpp)
    add_library(keels2_lifecycle_live_plugin SHARED tests/lifecycle_service_plugin.cpp)
    foreach(target IN ITEMS keels2_lifecycle_service_plugin keels2_lifecycle_live_plugin)
        target_include_directories(${target} PRIVATE sdk/include)
        target_compile_definitions(${target} PRIVATE KEELS2_PLUGIN_BUILD=1)
        set_target_properties(${target} PROPERTIES
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN YES
            PREFIX ""
        )
        keels2_enable_warnings(${target})
    endforeach()
    target_compile_definitions(keels2_lifecycle_live_plugin PRIVATE KEELS2_LIFECYCLE_LIVE=1)
    set_target_properties(keels2_lifecycle_service_plugin PROPERTIES
        OUTPUT_NAME keels2_lifecycle_service_plugin
    )
    set_target_properties(keels2_lifecycle_live_plugin PROPERTIES
        OUTPUT_NAME 01_lifecycle_live
    )

    function(keels2_add_source2_callbacks_plugin
        target output_name friendly_name marker priority rejection command_accept)
        add_library(${target} SHARED tests/source2_callbacks_plugin.cpp)
        target_include_directories(${target} PRIVATE sdk/include)
        target_link_libraries(${target} PRIVATE KeelS2::SDK KeelS2::SourceSDK)
        target_compile_definitions(${target} PRIVATE
            KEELS2_PLUGIN_BUILD=1
            "KEELS2_CALLBACK_PLUGIN_NAME=\"${friendly_name}\""
            "KEELS2_CALLBACK_PLUGIN_MARKER=\"${marker}\""
            "KEELS2_CALLBACK_PLUGIN_PRIORITY=${priority}"
            "KEELS2_CALLBACK_PLUGIN_REJECTION=${rejection}"
            "KEELS2_CALLBACK_PLUGIN_COMMAND_ACCEPT=${command_accept}"
        )
        set_target_properties(${target} PROPERTIES
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN YES
            PREFIX ""
            OUTPUT_NAME ${output_name}
        )
        keels2_enable_warnings(${target})
    endfunction()

    keels2_add_source2_callbacks_plugin(
        keels2_source2_callbacks_first_plugin
        01_source2_callbacks_first
        "Source2 Callbacks First"
        "Source2 First"
        20
        1
        1
    )
    keels2_add_source2_callbacks_plugin(
        keels2_source2_callbacks_second_plugin
        02_source2_callbacks_second
        "Source2 Callbacks Second"
        "Source2 Second"
        20
        2
        0
    )
    keels2_add_source2_callbacks_plugin(
        keels2_source2_callbacks_peer_plugin
        03_source2_callbacks_peer
        "Source2 Callbacks Peer"
        "Source2 Peer"
        50
        3
        1
    )

    function(keels2_add_live_source2_callbacks_plugin
        target output_name friendly_name marker priority rejection)
        add_library(${target} SHARED tests/source2_live_acceptance_plugin.cpp)
        target_include_directories(${target} PRIVATE sdk/include)
        target_link_libraries(${target} PRIVATE KeelS2::SDK KeelS2::SourceSDK)
        target_compile_definitions(${target} PRIVATE
            KEELS2_PLUGIN_BUILD=1
            "KEELS2_LIVE_CALLBACK_PLUGIN_NAME=\"${friendly_name}\""
            "KEELS2_LIVE_CALLBACK_MARKER=\"${marker}\""
            "KEELS2_LIVE_CALLBACK_PRIORITY=${priority}"
            "KEELS2_LIVE_CALLBACK_REJECTION=${rejection}"
        )
        set_target_properties(${target} PROPERTIES
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN YES
            PREFIX ""
            OUTPUT_NAME ${output_name}
        )
        keels2_enable_warnings(${target})
    endfunction()

    keels2_add_live_source2_callbacks_plugin(
        keels2_live_callbacks_observer
        keels2_05e_observer
        "KeelS2 0.5E Observer"
        "05E Observer"
        50
        0
    )
    keels2_add_live_source2_callbacks_plugin(
        keels2_live_callbacks_decision_a
        keels2_05e_decision_a
        "KeelS2 0.5E Decision A"
        "05E Decision A"
        20
        1
    )
    keels2_add_live_source2_callbacks_plugin(
        keels2_live_callbacks_decision_b
        keels2_05e_decision_b
        "KeelS2 0.5E Decision B"
        "05E Decision B"
        20
        2
    )

    add_library(keels2_authoring_concurrency_plugin SHARED tests/authoring_concurrency_plugin.cpp)
    target_include_directories(keels2_authoring_concurrency_plugin PRIVATE sdk/include)
    target_link_libraries(keels2_authoring_concurrency_plugin PRIVATE KeelS2::SDK KeelS2::SourceSDK)
    target_compile_definitions(keels2_authoring_concurrency_plugin PRIVATE KEELS2_PLUGIN_BUILD=1)
    set_target_properties(keels2_authoring_concurrency_plugin PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        PREFIX ""
        OUTPUT_NAME keels2_authoring_concurrency_plugin
    )
    keels2_enable_warnings(keels2_authoring_concurrency_plugin)

    add_library(keels2_convar_service_plugin SHARED tests/convar_service_plugin.cpp)
    target_include_directories(keels2_convar_service_plugin PRIVATE sdk/include)
    target_compile_definitions(keels2_convar_service_plugin PRIVATE KEELS2_PLUGIN_BUILD=1)
    set_target_properties(keels2_convar_service_plugin PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        PREFIX ""
        OUTPUT_NAME keels2_convar_service_plugin
    )
    keels2_enable_warnings(keels2_convar_service_plugin)

    add_library(keels2_convar_authoring_plugin SHARED
        tests/convar_authoring_plugin.cpp
    )
    target_include_directories(keels2_convar_authoring_plugin PRIVATE sdk/include)
    target_link_libraries(keels2_convar_authoring_plugin PRIVATE
        KeelS2::SDK
        KeelS2::SourceSDK
    )
    target_compile_definitions(
        keels2_convar_authoring_plugin PRIVATE KEELS2_PLUGIN_BUILD=1)
    set_target_properties(keels2_convar_authoring_plugin PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        PREFIX ""
        OUTPUT_NAME keels2_convar_authoring_plugin
    )
    keels2_enable_warnings(keels2_convar_authoring_plugin)

    add_library(keels2_plugin_runtime_service_plugin SHARED
        tests/plugin_runtime_service_plugin.cpp
    )
    target_include_directories(keels2_plugin_runtime_service_plugin PRIVATE sdk/include)
    target_compile_definitions(keels2_plugin_runtime_service_plugin PRIVATE KEELS2_PLUGIN_BUILD=1)
    set_target_properties(keels2_plugin_runtime_service_plugin PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        PREFIX ""
        OUTPUT_NAME keels2_plugin_runtime_service_plugin
    )
    keels2_enable_warnings(keels2_plugin_runtime_service_plugin)

    function(keels2_add_dependency_plugin target output_name friendly_name version)
        add_library(${target} SHARED tests/dependency_plugin.cpp)
        target_include_directories(${target} PRIVATE sdk/include)
        target_compile_definitions(${target} PRIVATE
            KEELS2_PLUGIN_BUILD=1
            "KEELS2_DEPENDENCY_NAME=\"${friendly_name}\""
            "KEELS2_DEPENDENCY_VERSION=\"${version}\""
        )
        if(ARGC GREATER 4)
            target_compile_definitions(${target} PRIVATE
                KEELS2_DEPENDENCY_HAS_MANIFEST=1
                "KEELS2_DEPENDENCY_TARGET=\"${ARGV4}\""
                "KEELS2_DEPENDENCY_TARGET_VERSION=\"${ARGV5}\""
                "KEELS2_DEPENDENCY_REQUIREMENT=${ARGV6}"
            )
        endif()
        set_target_properties(${target} PROPERTIES
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN YES
            PREFIX ""
            OUTPUT_NAME ${output_name}
        )
        keels2_enable_warnings(${target})
    endfunction()

    keels2_add_dependency_plugin(
        keels2_dependency_core dependency_core "Dependency Core" 1.2.3)
    keels2_add_dependency_plugin(
        keels2_dependency_middle dependency_middle "Dependency Middle" 2.0.0
        "Dependency Core" 1.2.0 KEELS2_PLUGIN_DEPENDENCY_AT_LEAST)
    keels2_add_dependency_plugin(
        keels2_dependency_leaf dependency_leaf "Dependency Leaf" 1.0.0
        "Dependency Middle" 2.0.0 KEELS2_PLUGIN_DEPENDENCY_EXACT)
    keels2_add_dependency_plugin(
        keels2_dependency_missing dependency_missing "Dependency Missing" 1.0.0
        "Not Installed" 1.0.0 KEELS2_PLUGIN_DEPENDENCY_AT_LEAST)
    keels2_add_dependency_plugin(
        keels2_dependency_mismatch dependency_mismatch "Dependency Mismatch" 1.0.0
        "Dependency Core" 9.9.9 KEELS2_PLUGIN_DEPENDENCY_EXACT)
    keels2_add_dependency_plugin(
        keels2_dependency_cycle_a dependency_cycle_a "Dependency Cycle A" 1.0.0
        "Dependency Cycle B" 1.0.0 KEELS2_PLUGIN_DEPENDENCY_EXACT)
    keels2_add_dependency_plugin(
        keels2_dependency_cycle_b dependency_cycle_b "Dependency Cycle B" 1.0.0
        "Dependency Cycle A" 1.0.0 KEELS2_PLUGIN_DEPENDENCY_EXACT)
    keels2_add_dependency_plugin(
        keels2_dependency_self dependency_self "Dependency Self" 1.0.0
        "Dependency Self" 1.0.0 KEELS2_PLUGIN_DEPENDENCY_EXACT)
    keels2_add_dependency_plugin(
        keels2_dependency_malformed dependency_malformed "Dependency Malformed" 1.0.0
        "Dependency Core" 1.2 KEELS2_PLUGIN_DEPENDENCY_AT_LEAST)

    add_executable(keels2_bootstrap_integration tests/bootstrap_integration.cpp)
    set_target_properties(keels2_bootstrap_integration PROPERTIES ENABLE_EXPORTS TRUE)
    target_include_directories(keels2_bootstrap_integration PRIVATE
        bootstrap/include
        games/cs2/include
        platform/include
        sdk/include
        "${KEELS2_SOURCE_SDK_RESOLVED_ROOT}/public"
        "${KEELS2_SOURCE_SDK_RESOLVED_ROOT}/public/engine"
        "${KEELS2_SOURCE_SDK_RESOLVED_ROOT}/public/mathlib"
        "${KEELS2_SOURCE_SDK_RESOLVED_ROOT}/public/tier0"
        "${KEELS2_SOURCE_SDK_RESOLVED_ROOT}/public/tier1"
        "${KEELS2_SOURCE_SDK_RESOLVED_ROOT}/public/entity2"
    )
    target_compile_definitions(keels2_bootstrap_integration PRIVATE
        SOURCE_ENGINE=25
        GAME_DLL
        RAD_TELEMETRY_DISABLED
        X64BITS
        PLATFORM_64BITS
    )
    if(MSVC)
        target_compile_definitions(keels2_bootstrap_integration PRIVATE
            COMPILER_MSVC
            COMPILER_MSVC64
            WIN32
            WIN64
        )
    else()
        target_compile_definitions(keels2_bootstrap_integration PRIVATE
            COMPILER_GCC
            _LINUX
            LINUX
            POSIX
            GNUC
            stricmp=strcasecmp
            _stricmp=strcasecmp
            _snprintf=snprintf
            _vsnprintf=vsnprintf
        )
    endif()
    target_link_libraries(keels2_bootstrap_integration PRIVATE keels2_platform)
    keels2_enable_warnings(keels2_bootstrap_integration)
    set(KEELS2_BOOTSTRAP_SCENARIOS
        success
        unknown_build
        missing_host
        missing_plugin
        invalid_plugin
        failed_plugin_load
        reverse_unload
        missing_cvar
        duplicate_command
        duplicate_plugin_name
        reserved_command
        core_commands
        plugin_lifecycle
        command_removal
        plugin_index_compaction
        source2_service
        schema_entity_service
        lifecycle_service
        source2_callbacks
        authoring_concurrency
        convar_service
        convar_failed_load
        convar_facade
        convar_authoring
        plugin_runtime_service
        plugin_runtime_concurrency
        plugin_transition_shutdown_retry
        plugin_dependencies
        lifecycle_pre_init
        lifecycle_failed_load
        failed_server_init
        missing_game_event_capture
        missing_source2_server
        missing_game_clients
        wrong_cvar_provenance
        keelhook
        missing_server
        invalid_layout
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        list(APPEND KEELS2_BOOTSTRAP_SCENARIOS keelhook_shutdown_retry)
    endif()
    foreach(scenario ${KEELS2_BOOTSTRAP_SCENARIOS})
        add_test(
            NAME bootstrap_${scenario}
            COMMAND keels2_bootstrap_integration
                ${scenario}
                $<TARGET_FILE:keels2_bootstrap>
                $<TARGET_FILE:keels2_bootstrap_test>
                $<TARGET_FILE:keels2_host>
                $<TARGET_FILE:keels2_basic>
                $<TARGET_FILE:keels2_invalid_plugin>
                $<TARGET_FILE:keels2_failing_plugin>
                $<TARGET_FILE:keels2_fake_server>
                $<TARGET_FILE:keels2_lifecycle_first_plugin>
                $<TARGET_FILE:keels2_lifecycle_second_plugin>
                $<TARGET_FILE:keels2_reserved_command_plugin>
                $<TARGET_FILE:keels2_duplicate_command_plugin>
                $<TARGET_FILE:keels2_fake_tier0>
                $<TARGET_FILE:keels2_keelhook_target_plugin>
                $<TARGET_FILE:keels2_keelhook_peer_plugin>
                $<TARGET_FILE:keels2_source2_service_plugin>
                $<TARGET_FILE:keels2_lifecycle_service_plugin>
                $<TARGET_FILE:keels2_authoring_concurrency_plugin>
                $<TARGET_FILE:keels2_convar_service_plugin>
                $<TARGET_FILE:keels2_sample>
                $<TARGET_FILE:keels2_plugin_runtime_service_plugin>
                $<TARGET_FILE:keels2_dependency_core>
                $<TARGET_FILE:keels2_dependency_middle>
                $<TARGET_FILE:keels2_dependency_leaf>
                $<TARGET_FILE:keels2_dependency_missing>
                $<TARGET_FILE:keels2_dependency_mismatch>
                $<TARGET_FILE:keels2_dependency_cycle_a>
                $<TARGET_FILE:keels2_dependency_cycle_b>
                $<TARGET_FILE:keels2_dependency_self>
                $<TARGET_FILE:keels2_dependency_malformed>
                $<TARGET_FILE:keels2_source2_callbacks_first_plugin>
                $<TARGET_FILE:keels2_source2_callbacks_second_plugin>
                $<TARGET_FILE:keels2_source2_callbacks_peer_plugin>
                $<TARGET_FILE:keels2_schema_entity_service_plugin>
                $<TARGET_FILE:keels2_schema_entity_fixture>
                $<TARGET_FILE:keels2_convar_authoring_plugin>
                ${CMAKE_BINARY_DIR}/integration-fixture
        )
    endforeach()
    get_property(KEELS2_REGISTERED_TESTS DIRECTORY PROPERTY TESTS)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        foreach(keels2_test ${KEELS2_REGISTERED_TESTS})
            set_property(
                TEST ${keels2_test}
                APPEND PROPERTY ENVIRONMENT_MODIFICATION
                "LD_LIBRARY_PATH=path_list_prepend:$<TARGET_FILE_DIR:keels2_fake_tier0>"
            )
        endforeach()
    elseif(WIN32)
        foreach(keels2_test ${KEELS2_REGISTERED_TESTS})
            set_property(
                TEST ${keels2_test}
                APPEND PROPERTY ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:keels2_fake_tier0>"
            )
        endforeach()
    endif()
endif()
