include_guard(GLOBAL)

set(KEELS2_SOURCE_SDK_REPOSITORY "https://github.com/alliedmodders/hl2sdk")
set(KEELS2_SOURCE_SDK_BRANCH "cs2")
set(KEELS2_SOURCE_SDK_REVISION "e5a8acad99ab33143be68761384dbf9fc5fe166a")
set(KEELS2_SOURCE_SDK_ARCHIVE_SHA256 "b1bb801698b0efb068c9dc2f6c3c75a020db23784c6047f58348eba02ec1cb57")
set(KEELS2_SOURCE_SDK_MANIFEST_REPOSITORY "https://github.com/alliedmodders/hl2sdk-manifests")
set(KEELS2_SOURCE_SDK_MANIFEST_REVISION "20b3a014264b38908c4a5d4eb263ba2c488f3dc1")
set(KEELS2_SOURCE_SDK_MANIFEST_PATH "manifests/cs2.json")
set(KEELS2_SOURCE_SDK_ROOT "" CACHE PATH "Existing checkout of the pinned CS2 HL2SDK revision")

function(keels2_provide_source_sdk)
    if(TARGET KeelS2::SourceSDK)
        return()
    endif()

    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "KeelS2::SourceSDK supports only 64-bit targets")
    endif()

    if(KEELS2_SOURCE_SDK_ROOT)
        get_filename_component(_keels2_source_sdk_root "${KEELS2_SOURCE_SDK_ROOT}" ABSOLUTE)
    else()
        include(FetchContent)
        FetchContent_Declare(
            keels2_hl2sdk_cs2
            URL "${KEELS2_SOURCE_SDK_REPOSITORY}/archive/${KEELS2_SOURCE_SDK_REVISION}.tar.gz"
            URL_HASH "SHA256=${KEELS2_SOURCE_SDK_ARCHIVE_SHA256}"
            DOWNLOAD_EXTRACT_TIMESTAMP FALSE
        )
        FetchContent_GetProperties(keels2_hl2sdk_cs2)
        if(NOT keels2_hl2sdk_cs2_POPULATED)
            FetchContent_Populate(keels2_hl2sdk_cs2)
        endif()
        set(_keels2_source_sdk_root "${keels2_hl2sdk_cs2_SOURCE_DIR}")
    endif()

    set(_keels2_source_sdk_checks
        "public/tier1/convar.h|26442a42314cbce02d76349dbc1cbba81edc2245f8e8008c715bc903b5d79912"
        "public/eiface.h|96eb1b99fb6e0639536cbb2485930e301b37d85b490906037550868183358db6"
        "public/playerslot.h|866edce8a35340e78b58c68d07c671c27ecdd77752e6a73ef4ded8e0c394d825"
        "common/network_connection.proto|5ad59894e7024061f8eb53c53e75039975e432122d0cd363734f64e9ba7bc7cc"
    )
    foreach(_keels2_source_sdk_check IN LISTS _keels2_source_sdk_checks)
        string(REPLACE "|" ";" _keels2_source_sdk_check_parts "${_keels2_source_sdk_check}")
        list(GET _keels2_source_sdk_check_parts 0 _keels2_source_sdk_path)
        list(GET _keels2_source_sdk_check_parts 1 _keels2_source_sdk_expected)
        set(_keels2_source_sdk_file "${_keels2_source_sdk_root}/${_keels2_source_sdk_path}")
        if(NOT EXISTS "${_keels2_source_sdk_file}")
            message(FATAL_ERROR "Pinned Source 2 SDK file is missing: ${_keels2_source_sdk_path}")
        endif()
        file(SHA256 "${_keels2_source_sdk_file}" _keels2_source_sdk_actual)
        if(NOT _keels2_source_sdk_actual STREQUAL _keels2_source_sdk_expected)
            message(FATAL_ERROR "Pinned Source 2 SDK file differs: ${_keels2_source_sdk_path}")
        endif()
    endforeach()

    if(MSVC)
        set(_keels2_source_sdk_protoc "${_keels2_source_sdk_root}/devtools/bin/protoc.exe")
        set(_keels2_source_sdk_protoc_sha256 "97fe5336ecb49c3dcb459e2f96f84735512e2b1eabca3556348dec9df315bcea")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(_keels2_source_sdk_protoc "${_keels2_source_sdk_root}/devtools/bin/linux/protoc")
        set(_keels2_source_sdk_protoc_sha256 "ec91bba0f5712c98ecddc4e6a30d17269b1ee705e94a073e7af46f6fb514ca05")
    else()
        message(FATAL_ERROR "KeelS2::SourceSDK supports only Windows and Linux")
    endif()

    if(NOT EXISTS "${_keels2_source_sdk_protoc}")
        message(FATAL_ERROR "Pinned Source 2 SDK protoc is missing")
    endif()
    file(SHA256 "${_keels2_source_sdk_protoc}" _keels2_source_sdk_protoc_actual)
    if(NOT _keels2_source_sdk_protoc_actual STREQUAL _keels2_source_sdk_protoc_sha256)
        message(FATAL_ERROR "Pinned Source 2 SDK protoc differs")
    endif()

    set(
        _keels2_source_sdk_generated
        "${CMAKE_BINARY_DIR}/_keels2/source-sdk/${KEELS2_SOURCE_SDK_REVISION}/generated"
    )
    file(MAKE_DIRECTORY "${_keels2_source_sdk_generated}")
    if(NOT EXISTS "${_keels2_source_sdk_generated}/network_connection.pb.h")
        file(GLOB _keels2_source_sdk_protos "${_keels2_source_sdk_root}/common/*.proto")
        list(SORT _keels2_source_sdk_protos)
        execute_process(
            COMMAND
                "${_keels2_source_sdk_protoc}"
                "--proto_path=${_keels2_source_sdk_root}/common"
                "--proto_path=${_keels2_source_sdk_root}/thirdparty/protobuf-3.21.8/src"
                "--cpp_out=${_keels2_source_sdk_generated}"
                ${_keels2_source_sdk_protos}
            RESULT_VARIABLE _keels2_source_sdk_protoc_result
            OUTPUT_VARIABLE _keels2_source_sdk_protoc_output
            ERROR_VARIABLE _keels2_source_sdk_protoc_error
        )
        if(NOT _keels2_source_sdk_protoc_result EQUAL 0)
            message(FATAL_ERROR "Pinned Source 2 SDK protobuf generation failed:\n${_keels2_source_sdk_protoc_output}\n${_keels2_source_sdk_protoc_error}")
        endif()
    endif()
    if(NOT EXISTS "${_keels2_source_sdk_generated}/network_connection.pb.h")
        message(FATAL_ERROR "Pinned Source 2 SDK protobuf generation was incomplete")
    endif()

    add_library(KeelS2::SourceSDKHeaders INTERFACE IMPORTED GLOBAL)
    target_compile_features(KeelS2::SourceSDKHeaders INTERFACE cxx_std_20)
    target_include_directories(KeelS2::SourceSDKHeaders SYSTEM INTERFACE
        "${_keels2_source_sdk_generated}"
        "${_keels2_source_sdk_root}/thirdparty/protobuf-3.21.8/src"
        "${_keels2_source_sdk_root}/public"
        "${_keels2_source_sdk_root}/public/engine"
        "${_keels2_source_sdk_root}/public/mathlib"
        "${_keels2_source_sdk_root}/public/tier0"
        "${_keels2_source_sdk_root}/public/tier1"
        "${_keels2_source_sdk_root}/public/entity2"
        "${_keels2_source_sdk_root}/game/shared"
        "${_keels2_source_sdk_root}/game/server"
        "${_keels2_source_sdk_root}/common"
    )
    target_compile_definitions(KeelS2::SourceSDKHeaders INTERFACE
        SOURCE_ENGINE=25
        GAME_DLL
        RAD_TELEMETRY_DISABLED
        X64BITS
        PLATFORM_64BITS
    )

    if(MSVC)
        target_compile_definitions(KeelS2::SourceSDKHeaders INTERFACE
            COMPILER_MSVC
            COMPILER_MSVC64
            WIN32
            WIN64
        )
        set(_keels2_source_sdk_libraries
            "${_keels2_source_sdk_root}/lib/public/win64/2015/libprotobuf.lib"
            "${_keels2_source_sdk_root}/lib/public/win64/mathlib.lib"
            "${_keels2_source_sdk_root}/lib/public/win64/tier0.lib"
            "${_keels2_source_sdk_root}/lib/public/win64/interfaces.lib"
            legacy_stdio_definitions.lib
        )
    else()
        find_package(Threads REQUIRED)
        target_compile_definitions(KeelS2::SourceSDKHeaders INTERFACE
            COMPILER_GCC
            _LINUX
            LINUX
            POSIX
            GNUC
            _GLIBCXX_USE_CXX11_ABI=0
            stricmp=strcasecmp
            _stricmp=strcasecmp
            _snprintf=snprintf
            _vsnprintf=vsnprintf
        )
        set(_keels2_source_sdk_libraries
            "${_keels2_source_sdk_root}/lib/linux64/mathlib.a"
            "${_keels2_source_sdk_root}/lib/linux64/interfaces.a"
            "${_keels2_source_sdk_root}/lib/linux64/release/libprotobuf.a"
            "-L${_keels2_source_sdk_root}/lib/linux64"
            -ltier0
            Threads::Threads
            "${CMAKE_DL_LIBS}"
            m
        )
    endif()

    add_library(keels2_source_sdk STATIC "${_keels2_source_sdk_root}/tier1/convar.cpp")
    add_library(KeelS2::SourceSDK ALIAS keels2_source_sdk)
    set_target_properties(keels2_source_sdk PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(MSVC)
        set_target_properties(keels2_source_sdk PROPERTIES
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
        )
        target_link_options(keels2_source_sdk INTERFACE
            /NODEFAULTLIB:LIBCMT
            /IGNORE:4099
            /INCREMENTAL:NO
        )
    endif()
    # ConVar calls cross into a game-owned C++ interface. Integration tests use
    # an ABI-faithful vtable fixture, which intentionally has no compatible RTTI;
    # vptr instrumentation therefore cannot validate this external boundary.
    # ASan and all other UBSan checks remain enabled.
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(keels2_source_sdk PRIVATE -fno-sanitize=vptr)
    endif()
    target_link_libraries(keels2_source_sdk PUBLIC
        KeelS2::SourceSDKHeaders
        ${_keels2_source_sdk_libraries}
    )

    set_property(
        TARGET keels2_source_sdk
        PROPERTY KEELS2_SOURCE_SDK_REVISION "${KEELS2_SOURCE_SDK_REVISION}"
    )
    set_property(
        TARGET keels2_source_sdk
        PROPERTY KEELS2_SOURCE_SDK_MANIFEST_REVISION "${KEELS2_SOURCE_SDK_MANIFEST_REVISION}"
    )
    set(KEELS2_SOURCE_SDK_RESOLVED_ROOT "${_keels2_source_sdk_root}" CACHE INTERNAL "")
endfunction()
