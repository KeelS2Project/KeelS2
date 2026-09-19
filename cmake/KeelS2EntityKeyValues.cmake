include(${CMAKE_CURRENT_LIST_DIR}/KeelS2KeyValuesSdk.cmake)

add_library(keels2_cs2_keyvalues_sdk OBJECT
    "${keels2_keyvalues_source}"
    "${KEELS2_SOURCE_SDK_RESOLVED_ROOT}/entity2/entitykeyvalues.cpp"
    "${KEELS2_SOURCE_SDK_RESOLVED_ROOT}/entity2/entitysystem.cpp"
    "${KEELS2_SOURCE_SDK_RESOLVED_ROOT}/entity2/entityidentity.cpp"
    games/cs2/src/keyvalues_allocator.cpp
)
get_target_property(keels2_keyvalues_sdk_sources keels2_cs2_keyvalues_sdk SOURCES)
set_property(SOURCE ${keels2_keyvalues_sdk_sources} games/cs2/src/entity_keyvalues.cpp games/cs2/src/entity_variant.cpp
    tests/entity_keyvalues_fixture.cpp tests/entity_variant_fixture.cpp APPEND PROPERTY OBJECT_DEPENDS "${keels2_keyvalues_patched_headers}")
target_include_directories(keels2_cs2_keyvalues_sdk SYSTEM BEFORE PRIVATE
    "${keels2_keyvalues_headers}" "${keels2_keyvalues_headers}/tier0")
target_link_libraries(keels2_cs2_keyvalues_sdk PRIVATE KeelS2::SourceSDK)
set_target_properties(keels2_cs2_keyvalues_sdk PROPERTIES
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
if(MSVC)
    target_compile_options(keels2_cs2_keyvalues_sdk PRIVATE /Zc:sizedDealloc-)
else()
    target_compile_options(keels2_cs2_keyvalues_sdk PRIVATE -fno-sized-deallocation -fno-sanitize=vptr)
endif()

add_library(keels2_cs2_keyvalues SHARED games/cs2/src/entity_keyvalues.cpp games/cs2/src/entity_variant.cpp
    $<TARGET_OBJECTS:keels2_cs2_keyvalues_sdk>)
target_include_directories(keels2_cs2_keyvalues PRIVATE games/cs2/include sdk/include)
target_include_directories(keels2_cs2_keyvalues SYSTEM BEFORE PRIVATE
    "${keels2_keyvalues_headers}" "${keels2_keyvalues_headers}/tier0")
target_link_libraries(keels2_cs2_keyvalues PRIVATE KeelS2::SourceSDK)
target_compile_definitions(keels2_cs2_keyvalues PRIVATE KEELS2_CS2_KEYVALUES_BUILD=1)
set_target_properties(keels2_cs2_keyvalues PROPERTIES
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
keels2_set_output(keels2_cs2_keyvalues "bin/${KEELS2_PLATFORM_DIR}")
keels2_enable_warnings(keels2_cs2_keyvalues)
if(MSVC)
    target_compile_options(keels2_cs2_keyvalues PRIVATE /Zc:sizedDealloc-)
else()
    target_compile_options(keels2_cs2_keyvalues PRIVATE -fno-sized-deallocation -fno-sanitize=vptr)
    target_link_options(keels2_cs2_keyvalues PRIVATE
        "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/games/cs2/keyvalues_exports.map"
        "LINKER:-Bsymbolic-functions" "LINKER:-z,defs" "LINKER:-z,relro,-z,now")
    set_property(TARGET keels2_cs2_keyvalues APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/games/cs2/keyvalues_exports.map")
endif()

add_library(keels2_cs2_constructions STATIC games/cs2/src/owned_construction.cpp games/cs2/src/native_construction.cpp games/cs2/src/native_input.cpp)
target_include_directories(keels2_cs2_constructions PUBLIC games/cs2/include sdk/include)
target_link_libraries(keels2_cs2_constructions PUBLIC keels2_cs2_keyvalues keels2_cs2_native_bridge)
keels2_enable_warnings(keels2_cs2_constructions)

if(BUILD_TESTING)
    add_library(keels2_keyvalues_fixture SHARED tests/entity_keyvalues_fixture.cpp tests/entity_variant_fixture.cpp
        $<TARGET_OBJECTS:keels2_cs2_keyvalues_sdk>)
    target_include_directories(keels2_keyvalues_fixture PRIVATE games/cs2/include sdk/include)
    target_include_directories(keels2_keyvalues_fixture SYSTEM BEFORE PRIVATE
        "${keels2_keyvalues_headers}" "${keels2_keyvalues_headers}/tier0")
    target_link_libraries(keels2_keyvalues_fixture PRIVATE KeelS2::SourceSDK keels2_fake_tier0)
    set_target_properties(keels2_keyvalues_fixture PROPERTIES
        CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
    keels2_enable_warnings(keels2_keyvalues_fixture)
    if(MSVC)
        target_compile_options(keels2_keyvalues_fixture PRIVATE /Zc:sizedDealloc-)
    else()
        target_compile_options(keels2_keyvalues_fixture PRIVATE -fno-sized-deallocation -fno-sanitize=vptr)
        target_link_options(keels2_keyvalues_fixture PRIVATE
            "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/tests/entity_keyvalues_fixture.map"
            "LINKER:-Bsymbolic-functions" "LINKER:-z,defs")
        set_property(TARGET keels2_keyvalues_fixture APPEND PROPERTY LINK_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/entity_keyvalues_fixture.map")
    endif()
    add_executable(keels2_entity_keyvalues_test tests/entity_keyvalues_test.cpp)
    target_include_directories(keels2_entity_keyvalues_test PRIVATE games/cs2/include sdk/include)
    target_link_libraries(keels2_entity_keyvalues_test PRIVATE
        keels2_cs2_keyvalues keels2_keyvalues_fixture keels2_fake_tier0)
    keels2_enable_warnings(keels2_entity_keyvalues_test)
    add_test(NAME entity_keyvalues COMMAND keels2_entity_keyvalues_test)
    add_executable(keels2_entity_variant_test tests/entity_variant_test.cpp)
    target_include_directories(keels2_entity_variant_test PRIVATE games/cs2/include sdk/include)
    target_link_libraries(keels2_entity_variant_test PRIVATE
        keels2_cs2_keyvalues keels2_keyvalues_fixture keels2_fake_tier0)
    keels2_enable_warnings(keels2_entity_variant_test)
    add_test(NAME entity_variant COMMAND keels2_entity_variant_test)
    add_executable(keels2_entity_variant_snapshot tests/entity_variant_snapshot.cpp)
    target_include_directories(keels2_entity_variant_snapshot PRIVATE games/cs2/include sdk/include)
    target_link_libraries(keels2_entity_variant_snapshot PRIVATE keels2_cs2_keyvalues KeelS2::SourceSDK keels2_fake_tier0)
    keels2_enable_warnings(keels2_entity_variant_snapshot)
    add_test(NAME entity_variant_snapshot COMMAND keels2_entity_variant_snapshot)
    add_executable(keels2_owned_construction_test tests/owned_construction_test.cpp)
    target_link_libraries(keels2_owned_construction_test PRIVATE
        keels2_cs2_constructions keels2_keyvalues_fixture keels2_fake_tier0)
    keels2_enable_warnings(keels2_owned_construction_test)
    add_test(NAME owned_construction COMMAND keels2_owned_construction_test)
    if(WIN32)
        set_property(TEST entity_keyvalues entity_variant entity_variant_snapshot owned_construction APPEND PROPERTY ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:keels2_cs2_keyvalues>")
    endif()
endif()
