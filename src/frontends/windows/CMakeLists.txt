# ---------------------------------------------------------------------------
# Windows Frontend
# ---------------------------------------------------------------------------

# Ensure we only build this for Windows or when cross-compiling to Windows
if(NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
    message(FATAL_ERROR "The Windows frontend can only be built for Windows targets.")
endif()

set(_TARGET_NAME ${PROJECT_NAME}_windows)

# Define the frontend executable
# TODO: Move all files prefixed with ../../win, including the lib/dll into this frontend directory
add_executable(${_TARGET_NAME}
    ../../main-win.c
    ../../win/readdib.c
    ../../win/readpng.c
    ../../win/scrnshot.c
    ../../win/win-layout.c

    ../../win/${PROJECT_NAME}.rc             
)

# ---------------------------------------------------------------------------
# Include directories
# ---------------------------------------------------------------------------
target_include_directories(${_TARGET_NAME} PRIVATE
    ${}
    ../../win
)

# ----------------------------------------------------------
# Imported Windows libraries (zlib, libpng)
# ----------------------------------------------------------

# Link PNG and ZLIB
find_package(PNG QUIET)
if(PNG_FOUND)
    message(STATUS "Found system PNG library.")
    target_link_libraries(${_TARGET_NAME} PRIVATE PNG::PNG)
else()
    message(STATUS "System PNG library not found; using bundled version.")

    add_library(OurWindowsZLib SHARED IMPORTED)
    set_target_properties(OurWindowsZLib PROPERTIES
        IMPORTED_IMPLIB "${CMAKE_CURRENT_SOURCE_DIR}/../../win/lib/zlib.lib"
    )
    target_include_directories(OurWindowsZLib INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../win/include"
    )

    add_library(OurWindowsPNGLib SHARED IMPORTED)
    set_target_properties(OurWindowsPNGLib PROPERTIES
        IMPORTED_IMPLIB "${CMAKE_CURRENT_SOURCE_DIR}/../../win/lib/libpng.lib"
    )
    target_include_directories(OurWindowsPNGLib INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../win/include"
    )
    target_link_libraries(${_TARGET_NAME} PRIVATE OurWindowsPNGLib OurWindowsZLib)
endif()

# ----------------------------------------------------------
# Link libraries and apply options
# ----------------------------------------------------------
target_link_libraries(${_TARGET_NAME} PRIVATE
    core        # Link with the shared core library
    msimg32
    winmm
    core
)

set_target_properties(${_TARGET_NAME} PROPERTIES
    WIN32_EXECUTABLE ON
    OUTPUT_NAME "${PROJECT_NAME}"
)

# ----------------------------------------------------------
# Compile definitions for Windows frontend
# ----------------------------------------------------------
target_compile_definitions(${_TARGET_NAME} PRIVATE
    USE_WIN
    USE_PRIVATE_PATHS
    SOUND
    WINDOWS
    _CRT_SECURE_NO_WARNINGS
)

# --------------------------------------------------------------------------
# Install executable and bundled DLLs to DIST_DIR (default is project root) for distribution
# --------------------------------------------------------------------------
set(DIST_DIR "${CMAKE_SOURCE_DIR}" CACHE PATH "Directory for distributing built executables and DLLs")

install(TARGETS ${_TARGET_NAME} RUNTIME DESTINATION "${DIST_DIR}")

if(PNG_FOUND)
    get_target_property(PNG_DLL PNG::PNG IMPORTED_LOCATION_RELEASE)
    if(PNG_DLL)
        install(FILES "${PNG_DLL}" DESTINATION "${DIST_DIR}")
    endif()
else()
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/../../win/dll/zlib1.dll"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../win/dll/libpng12.dll"
        DESTINATION "${DIST_DIR}"
    )
endif()