MACRO(CONFIGURE_GCU_FRONTEND _NAME_TARGET)

    # 3.10 required for CURSES_NEED_WIDE
    CMAKE_MINIMUM_REQUIRED(VERSION 3.10...3.26 FATAL_ERROR)
    SET(CURSES_NEED_WIDE TRUE)
    # Only ncurses provides wide character support so require that as well.
    SET(CURSES_NEED_NCURSES TRUE)

    # Only call find_package if we don't already have it from a toolchain
    IF (CURSES_INCLUDE_DIRS)
        # Check if toolchain variables  are valid
        IF (EXISTS "${CURSES_INCLUDE_DIRS}")
            MESSAGE(STATUS "Using ncurses from toolchain: ${CURSES_INCLUDE_DIRS}, ${CURSES_LIBRARIES}")
            SET(CURSES_FOUND TRUE)
        ELSE()
            MESSAGE(WARNING "CURSES_INCLUDE_DIRS from toolchain is invalid: ${CURSES_INCLUDE_DIRS}")
        ENDIF()
    ELSE()
        FIND_PACKAGE(Curses)
    ENDIF()


    IF (CURSES_FOUND)
        message(STATUS "CURSES_INCLUDE_DIRS=${CURSES_INCLUDE_DIRS}")
        message(STATUS "CURSES_LIBRARIES=${CURSES_LIBRARIES}")
        message(STATUS "CURSES_LIBRARY=${CURSES_LIBRARY}")

        TARGET_LINK_LIBRARIES(${_NAME_TARGET} PRIVATE ${CURSES_LIBRARIES})
        TARGET_INCLUDE_DIRECTORIES(${_NAME_TARGET} PRIVATE ${CURSES_INCLUDE_DIRS})
        TARGET_COMPILE_DEFINITIONS(${_NAME_TARGET} PRIVATE -D USE_GCU)
        TARGET_COMPILE_DEFINITIONS(${_NAME_TARGET} PRIVATE -D USE_NCURSES)

        include(CheckLibraryExists)
        CHECK_LIBRARY_EXISTS(${CURSES_LIBRARY} use_default_colors "" ANGBAND_CURSES_NCURSES_HAS_USE_DEFAULT_COLORS)
        IF (ANGBAND_CURSES_NCURSES_HAS_USE_DEFAULT_COLORS)
            TARGET_COMPILE_DEFINITIONS(${_NAME_TARGET} PRIVATE -D HAVE_USE_DEFAULT_COLORS)
            MESSAGE(STATUS "Using use_default_colors() with GCU front end")
        ENDIF()

        MESSAGE(STATUS "Support for GCU front end - Ready")

    ELSE()

        MESSAGE(FATAL_ERROR "Support for GCU front end - Failed")

    ENDIF()

ENDMACRO()
