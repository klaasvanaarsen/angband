macro(configure_gcu_frontend _NAME_TARGET)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CURSES REQUIRED IMPORTED_TARGET ncursesw)

    target_link_libraries(${_NAME_TARGET} PRIVATE PkgConfig::CURSES)
    target_compile_definitions(${_NAME_TARGET} PRIVATE USE_GCU USE_NCURSES)

    include(CheckSymbolExists)
    set(CMAKE_REQUIRED_LIBRARIES PkgConfig::CURSES)
    set(CMAKE_REQUIRED_INCLUDES ${CURSES_INCLUDE_DIRS})
    check_symbol_exists(use_default_colors "curses.h"
        ANGBAND_CURSES_HAS_USE_DEFAULT_COLORS
    )
    unset(CMAKE_REQUIRED_LIBRARIES)
    unset(CMAKE_REQUIRED_INCLUDES)

    if(ANGBAND_CURSES_HAS_USE_DEFAULT_COLORS)
        target_compile_definitions(${_NAME_TARGET} PRIVATE HAVE_USE_DEFAULT_COLORS)
        message(STATUS "Using use_default_colors() with GCU front end")
    else()
        message(STATUS "use_default_colors() not supported")
    endif()

    message(STATUS "Support for GCU front end - Ready")
endmacro()
