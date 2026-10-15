add_executable(service service.cpp)

set_target_properties(service PROPERTIES OUTPUT_NAME "TrayControlService")

target_compile_definitions(service PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(service PRIVATE
    ipc
    wtsapi32
    userenv
    rpcrt4
    advapi32
)
