cmake_minimum_required(VERSION 3.20)

project(project-describer VERSION 0.1.0 LANGUAGES CXX)

option(PROJECT_DESCRIBER_BUILD_TESTS "Build project-describer tests" ON)

# ===========================================================================
# spdlog — mature, fast, header-only-capable C++ logging library
# ===========================================================================
include(FetchContent)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY  https://github.com/gabime/spdlog.git
    GIT_TAG         v1.14.1
    GIT_SHALLOW     TRUE
)
set(spdlog_BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spdlog)

# ===========================================================================
# Core library
# ===========================================================================
add_library(project_describer_core
    src/application/command_line.cpp
    src/application/project_describer.cpp
    src/core/report_writer.cpp
    src/core/scanner.cpp
    src/model/text_encoding.cpp
    src/utils/encoding.cpp
    src/utils/file_reader.cpp
    src/utils/logging.cpp
    src/utils/user_notifier.cpp
)

target_include_directories(project_describer_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cxxopts"
)
target_link_libraries(project_describer_core
    PUBLIC
        spdlog::spdlog_header_only
)
target_compile_features(project_describer_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(project_describer_core PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(project_describer_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ===========================================================================
# CLI executable
# ===========================================================================
add_executable(project-describer src/main.cpp)
target_link_libraries(project-describer PRIVATE project_describer_core)

# ===========================================================================
# Tests (optional)
# ===========================================================================
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

# ===========================================================================
# GUI executable (Qt6 Widgets, optional)
# ===========================================================================
set(PROJECT_DESCRIBER_BUILD_GUI ON CACHE BOOL "Build the Qt6 GUI application")

if(PROJECT_DESCRIBER_BUILD_GUI)
    set(Qt6_DIR "D:/Qt6/6.9.1/msvc2022_64/lib/cmake/Qt6" CACHE PATH "Qt6 cmake package directory" FORCE)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)

    # --- windeployqt: auto-deploy Qt DLLs after each build ---
    set(_qt_bin_dir "${Qt6_DIR}/../../../bin")
    find_program(_windeployqt_executable
        NAMES windeployqt windeployqt.exe
        HINTS "${_qt_bin_dir}"
        NO_DEFAULT_PATH
    )
    if(_windeployqt_executable)
        message(STATUS "windeployqt: ${_windeployqt_executable}")
    else()
        message(WARNING "windeployqt not found; Qt DLLs will NOT be auto-deployed")
    endif()

    # --- GUI target ---
    set(CMAKE_AUTOMOC ON)

    add_executable(project-describer-gui WIN32
        src/gui_main.cpp
        src/gui/main_window.cpp
    )
    target_link_libraries(project-describer-gui
        PRIVATE
            project_describer_core
            Qt6::Widgets
    )

    # --- windeployqt post-build step ---
    if(_windeployqt_executable)
        add_custom_command(TARGET project-describer-gui POST_BUILD
            COMMAND "${_windeployqt_executable}"
                --no-compiler-runtime
                "$<TARGET_FILE:project-describer-gui>"
            COMMENT "Deploying Qt runtime files (DLLs, plugins, platform) via windeployqt..."
            VERBATIM
        )
    endif()
endif()
