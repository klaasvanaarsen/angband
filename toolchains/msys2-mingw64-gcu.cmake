# ----------------------------------------------
# MSYS2 MinGW64 Toolchain file for GCU frontend
# ----------------------------------------------
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Prefer static linking for console ncurses build
set(CMAKE_EXE_LINKER_FLAGS "-static")

# MSYS2 GCU build-specific definitions
add_compile_definitions(WIN32_CONSOLE_MODE NCURSES_STATIC MSYS2_ENCODING_WORKAROUND)

# ------------------------------------------------------------------
# Detect and validate MSYS2 environment
# ------------------------------------------------------------------
if(NOT DEFINED ENV{MSYSTEM_PREFIX})
    message(FATAL_ERROR
        "Environment variable MSYSTEM_PREFIX is not set.\n"
        "This toolchain must be used inside an MSYS2 MinGW64 shell "
        "(or via actions/setup-msys2 with MSYSTEM=MINGW64).")
endif()

set(MSYS2_ROOT "$ENV{MSYSTEM_PREFIX}")

if(NOT EXISTS "${MSYS2_ROOT}/include" OR NOT EXISTS "${MSYS2_ROOT}/lib")
    message(FATAL_ERROR
        "MSYS2 prefix appears invalid: ${MSYS2_ROOT}\n"
        "Expected to find include/ and lib/ directories there.")
endif()

# ------------------------------------------------------------------
# Tell CMake where to look for headers, libraries, and packages
# ------------------------------------------------------------------
set(CMAKE_FIND_ROOT_PATH "${MSYS2_ROOT}")

# Use system tools, but only look in MSYS2 prefix for libs/includes/packages
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# ------------------------------------------------------------------
# Predefine ncurses paths (used for GCU frontend)
# ------------------------------------------------------------------
set(CURSES_INCLUDE_DIRS "${MSYS2_ROOT}/include/ncursesw" CACHE PATH "Curses include directory")
set(CURSES_LIBRARY "${MSYS2_ROOT}/lib/libncursesw.a" CACHE FILEPATH "Curses library")
set(FORM_LIBRARY "${MSYS2_ROOT}/lib/libformw.a" CACHE FILEPATH "Curses form library")
set(CURSES_LIBRARIES "${CURSES_LIBRARY};${FORM_LIBRARY}" CACHE STRING "Curses libraries")

# ------------------------------------------------------------------
# Auto-detect whether ncurses is actually present
# ------------------------------------------------------------------
if(NOT DEFINED CURSES_FOUND)
    if(EXISTS "${CURSES_INCLUDE_DIRS}" AND EXISTS "${CURSES_LIBRARY}")
        set(CURSES_FOUND TRUE CACHE BOOL "Curses available in MSYS2 toolchain")
        message(STATUS "Detected ncurses in MSYS2 toolchain: ${CURSES_INCLUDE_DIRS}")
    else()
        set(CURSES_FOUND FALSE CACHE BOOL "Curses not found in MSYS2 toolchain")
        message(WARNING "ncurses not found in expected MSYS2 paths: ${MSYS2_ROOT}")
    endif()
endif()