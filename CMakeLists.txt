cmake_minimum_required(VERSION 3.20)
project(windows-tray-service LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Statically link the MSVC runtime so the exe files run on a clean machine
# without the Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT MSVC)
    message(FATAL_ERROR "This project targets Windows / MSVC only.")
endif()

# Treat source files as UTF-8 so the Cyrillic string literals are encoded
# correctly (otherwise MSVC assumes the system ANSI code page).
add_compile_options(/utf-8)

add_subdirectory(ipc)
add_subdirectory(tray-app)
add_subdirectory(service)
