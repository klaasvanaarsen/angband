function(DETERMINE_PNG PNG_TARGET PNG_DLLS USE_BUNDLED)
    if(USE_BUNDLED)
        message(STATUS "Using bundled PNG and ZLIB")

        add_library(BundledZLib SHARED IMPORTED)
        set_target_properties(BundledZLib PROPERTIES
            IMPORTED_LOCATION   "${CMAKE_CURRENT_SOURCE_DIR}/src/win/dll/zlib1.dll"
            IMPORTED_IMPLIB     "${CMAKE_CURRENT_SOURCE_DIR}/src/win/lib/zlib.lib"
            INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/src/win/include"
        )

        add_library(BundledPNG SHARED IMPORTED)
        set_target_properties(BundledPNG PROPERTIES
            IMPORTED_LOCATION   "${CMAKE_CURRENT_SOURCE_DIR}/src/win/dll/libpng12.dll"
            IMPORTED_IMPLIB     "${CMAKE_CURRENT_SOURCE_DIR}/src/win/lib/libpng.lib"
            INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/src/win/include"
            INTERFACE_LINK_LIBRARIES BundledZLib
        )

        set(${PNG_TARGET} BundledPNG PARENT_SCOPE)
        set(${PNG_DLLS}
            "${CMAKE_CURRENT_SOURCE_DIR}/src/win/dll/libpng12.dll"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/win/dll/zlib1.dll"
            PARENT_SCOPE
        )
        return()
    endif()

    # --- SYSTEM STATIC VERSION ---
    find_package(PNG QUIET)
    if(NOT PNG_FOUND)
        message(FATAL_ERROR
            "System PNG not found. If you are building a 32-bit x86 Windows binary, "
            "enable -DSUPPORT_BUNDLED_PNG=ON")
    endif()

    # Temporary override for static search
    set(_old_suffixes "${CMAKE_FIND_LIBRARY_SUFFIXES}")
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")

    find_library(PNG_STATIC_LIB  NAMES png  libpng   REQUIRED)
    find_library(ZLIB_STATIC_LIB NAMES z    zlib     REQUIRED)

    # Restore global variable
    set(CMAKE_FIND_LIBRARY_SUFFIXES "${_old_suffixes}")
    unset(_old_suffixes)

    add_library(ZLIB::StaticZlib STATIC IMPORTED)
    set_target_properties(ZLIB::StaticZlib PROPERTIES IMPORTED_LOCATION "${ZLIB_STATIC_LIB}")

    add_library(PNG::StaticPNG STATIC IMPORTED)
    set_target_properties(PNG::StaticPNG PROPERTIES
        IMPORTED_LOCATION             "${PNG_STATIC_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${PNG_INCLUDE_DIRS}"
        INTERFACE_LINK_LIBRARIES      ZLIB::StaticZlib
        INTERFACE_COMPILE_DEFINITIONS PNG_STATIC
    )

    message(STATUS "Using system PNG and ZLIB:")
    message(STATUS "  PNG include dirs  : ${PNG_INCLUDE_DIRS}")
    message(STATUS "  PNG static lib    : ${PNG_STATIC_LIB}")
    message(STATUS "  ZLIB static lib   : ${ZLIB_STATIC_LIB}")

    set(${PNG_TARGET} PNG::StaticPNG PARENT_SCOPE)
    set(${PNG_DLLS}   ""             PARENT_SCOPE)
endfunction()
