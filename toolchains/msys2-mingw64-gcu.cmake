# ----------------------------------------------
# MSYS2 MinGW64 Toolchain file for GCU frontend
# ----------------------------------------------
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Linker flags (static linking for ncurses)
set(CMAKE_EXE_LINKER_FLAGS "-static")

# MSYS2-GCU specific definitions
add_compile_definitions(WIN32_CONSOLE_MODE NCURSES_STATIC MSYS2_ENCODING_WORKAROUND)

# Root for libraries and includes
set(CMAKE_FIND_ROOT_PATH "C:/msys64/mingw64")

# Only search under root
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Pre-set ncurses since find_package(Curses) does not work correctly under MSYS2
set(CURSES_INCLUDE_DIRS "/mingw64/include/ncurses")
set(CURSES_LIBRARIES "/mingw64/lib/libncursesw.a;/mingw64/lib/libformw.a")
set(CURSES_LIBRARY ncursesw)
