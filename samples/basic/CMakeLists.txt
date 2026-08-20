cmake_minimum_required(VERSION 3.25)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(KeelS2BasicPlugin VERSION 0.1.0 LANGUAGES CXX)
    find_package(KeelS2 CONFIG REQUIRED)
endif()

keels2_add_plugin(keels2_basic plugin.cpp)
set_target_properties(keels2_basic PROPERTIES OUTPUT_NAME keels2_basic)
