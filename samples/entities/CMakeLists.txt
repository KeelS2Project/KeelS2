cmake_minimum_required(VERSION 3.25)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(KeelS2EntitiesPlugin VERSION 0.9.0 LANGUAGES CXX)
    find_package(KeelS2 CONFIG REQUIRED)
endif()

keels2_add_plugin(keels2_entities plugin.cpp)
set_target_properties(keels2_entities PROPERTIES OUTPUT_NAME keels2_entities)
