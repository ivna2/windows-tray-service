cmake_minimum_required(VERSION 3.20)
project(windows-tray-service LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT MSVC)
    message(FATAL_ERROR "This project targets Windows / MSVC only.")
endif()

add_subdirectory(ipc)
add_subdirectory(tray-app)
add_subdirectory(service)
