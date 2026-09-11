cmake_minimum_required(VERSION 3.25)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(KeelS2StubPlugin VERSION 1.0.0 LANGUAGES CXX)
    find_package(KeelS2 CONFIG REQUIRED)
endif()

keels2_add_plugin(keels2_stub plugin.cpp)
