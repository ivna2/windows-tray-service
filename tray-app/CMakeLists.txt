add_executable(tray-app WIN32
    main.cpp
    resource.h
)

target_compile_definitions(tray-app PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(tray-app PRIVATE
    user32
    shell32
    advapi32
)
