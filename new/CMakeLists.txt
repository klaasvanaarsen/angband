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
option(BUILD_TESTS "Build unit tests" OFF)
option(BUILD_DOCS "Build documentation" OFF)
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_COVERAGE "Enable code coverage (GCC/Clang only)" OFF)
option(FRONTEND_SDL "Use SDL frontend" OFF)
option(FRONTEND_SDL2 "Use SDL2 frontend" OFF)
option(FRONTEND_CLI "Use CLI frontend" OFF)
option(SUPPORT_WINDOWS_FRONTEND "Use Windows frontend" OFF)
option(WITH_FEATURE_X "Enable experimental feature X" OFF)

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
