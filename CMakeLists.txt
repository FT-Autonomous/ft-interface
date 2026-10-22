cmake_minimum_required(VERSION 3.20)
project(FTInterface LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(assi_tests
        tests/assi_ami/test_assi_logic.cpp
        ino/assi_controller/assi_controller.cpp
)
target_include_directories(assi_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ino/assi_controller)

find_package(GTest REQUIRED)
target_link_libraries(assi_tests PRIVATE GTest::gtest_main)

enable_testing()
include(GoogleTest)
gtest_discover_tests(assi_tests)