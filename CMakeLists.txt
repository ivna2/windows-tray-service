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

add_subdirectory(ipc)
add_subdirectory(tray-app)
add_subdirectory(service)
