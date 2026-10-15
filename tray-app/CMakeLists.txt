add_executable(tray-app WIN32
    main.cpp
    resource.h
)

target_compile_definitions(tray-app PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
)

# Task 2.2: the tray app uses the RPC client stub (ipc) to stop the service.
target_link_libraries(tray-app PRIVATE
    ipc
    user32
    shell32
    advapi32
    rpcrt4
)
