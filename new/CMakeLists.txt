cmake_minimum_required(VERSION 3.20)
project(angband
    VERSION 1.0
    LANGUAGES C
    DESCRIPTION "Cross-platform C99 application with optional features"
)

# Set C standard and strict warnings
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Always use folders for IDEs
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Add custom cmake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")

# --- Build options ---
# option(BUILD_TESTS "Build unit tests" OFF)
# option(BUILD_DOCS "Build documentation" OFF)
# option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
# option(ENABLE_COVERAGE "Enable code coverage (GCC/Clang only)" OFF)
# option(FRONTEND_SDL "Use SDL frontend" OFF)
# option(FRONTEND_SDL2 "Use SDL2 frontend" OFF)
# option(FRONTEND_CLI "Use CLI frontend" OFF)
# option(WITH_FEATURE_X "Enable experimental feature X" OFF)

OPTION(SUPPORT_GCU_FRONTEND "Support for GCU front end." OFF)
OPTION(SUPPORT_NCURSES_FRONTEND "Synonym for SUPPORT_GCU_FRONTEND for backwards compatibility." OFF)
OPTION(SUPPORT_SDL_FRONTEND "Support for SDL front end." OFF)
OPTION(SUPPORT_SDL2_FRONTEND "Support for SDL2 front end." OFF)
OPTION(SUPPORT_X11_FRONTEND "Support for X11 Frontend." OFF)
OPTION(SUPPORT_SDL_SOUND "Support for sound with SDL." OFF)
OPTION(SUPPORT_SDL2_SOUND "Support for sound with SDL2." OFF)
IF(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    SET(SPOIL_DEFAULT OFF)
ELSE()
    SET(SPOIL_DEFAULT ON)
ENDIF()
OPTION(SUPPORT_SPOIL_FRONTEND "Support for spoiler front end." ${SPOIL_DEFAULT})
OPTION(SUPPORT_STATS_FRONTEND "Support for statistics front end; requires sqlite3 development library." OFF)
OPTION(SUPPORT_TEST_FRONTEND "Support for test front end." OFF)
OPTION(SUPPORT_WINDOWS_FRONTEND "Support for windows front end." OFF)
OPTION(SUPPORT_STATS_BACKEND "Enable backend support for statistics and related debugging commands.  Implied by SUPPORT_STATS_FRONTEND." OFF)
OPTION(SUPPORT_BORG "Support for Borg." ON)
OPTION(SUPPORT_BORG_HIGH_SCORES "Borg characters allowed in high scores." OFF)

# --- Compiler settings ---
if(MSVC)
    add_compile_options(/W4 /utf8 /permissive-)
else()
    add_compile_options(-Wall -Wextra -pedantic -Wno-unused-parameter)
    if(ENABLE_ASAN)
        add_compile_options(-fsanitize=address)
        add_link_options(-fsanitize=address)
    endif()
    if(ENABLE_COVERAGE)
        include(CodeCoverage)
        enable_coverage()
    endif()
endif()

# --- Configure a header file with options ---
# configure_file(
#     ${CMAKE_CURRENT_SOURCE_DIR}/include/project/config.h.in
#     ${CMAKE_CURRENT_BINARY_DIR}/generated/config.h
# )
# include_directories(${CMAKE_CURRENT_BINARY_DIR}/generated)

# --- Subdirectories ---
add_subdirectory(src)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
if(BUILD_DOCS)
    find_package(Sphinx)
    if(SPHINX_FOUND)
        add_subdirectory(docs)
    endif()
endif()

# --- Installation (optional) ---
# install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
# install(DIRECTORY include/ DESTINATION include)
