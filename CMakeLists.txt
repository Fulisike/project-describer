cmake_minimum_required(VERSION 3.20)

project(project-describer VERSION 0.1.0 LANGUAGES CXX)

option(PROJECT_DESCRIBER_BUILD_TESTS "Build project-describer tests" ON)

add_library(project_describer_core
    src/application/command_line.cpp
    src/application/project_describer.cpp
    src/core/report_writer.cpp
    src/core/scanner.cpp
    src/model/text_encoding.cpp
    src/utils/encoding.cpp
    src/utils/file_reader.cpp
)

target_include_directories(project_describer_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cxxopts"
)
target_compile_features(project_describer_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(project_describer_core PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(project_describer_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(project-describer src/main.cpp)
target_link_libraries(project-describer PRIVATE project_describer_core)

include(CTest)
if(BUILD_TESTING AND PROJECT_DESCRIBER_BUILD_TESTS)
    add_executable(project-describer-tests
        tests/test_main.cpp
        tests/test_command_line.cpp
        tests/test_encoding.cpp
        tests/test_scanner.cpp
        tests/test_report_writer.cpp
    )
    target_link_libraries(project-describer-tests PRIVATE project_describer_core)
    add_test(NAME project-describer-tests COMMAND project-describer-tests)
endif()
