cmake_minimum_required(VERSION 3.16)
project(FTInterface LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)

add_executable(assi_tests tests/assi_ami/test_assi_logic.cpp)
target_include_directories(assi_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ino/assi_controller)

find_package(GTest REQUIRED)
target_link_libraries(assi_tests PRIVATE GTest::gtest_main)
