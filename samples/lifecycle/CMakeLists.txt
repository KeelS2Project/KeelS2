cmake_minimum_required(VERSION 3.25)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(KeelS2LifecyclePlugin VERSION 0.9.0 LANGUAGES CXX)
    find_package(KeelS2 CONFIG REQUIRED)
endif()

keels2_add_plugin(keels2_lifecycle plugin.cpp)
set_target_properties(keels2_lifecycle PROPERTIES OUTPUT_NAME keels2_lifecycle)
