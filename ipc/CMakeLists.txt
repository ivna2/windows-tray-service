# Compiles the RPC interface (ipc.idl) with MIDL and wraps the generated
# client + server stubs in a static library shared by the service and the
# tray application.

find_program(MIDL_EXECUTABLE midl REQUIRED)

set(IDL_FILE ${CMAKE_CURRENT_SOURCE_DIR}/ipc.idl)
set(GEN_DIR ${CMAKE_CURRENT_BINARY_DIR})

set(GEN_HEADER ${GEN_DIR}/ipc_h.h)
set(GEN_CLIENT ${GEN_DIR}/ipc_c.c)
set(GEN_SERVER ${GEN_DIR}/ipc_s.c)

add_custom_command(
    OUTPUT ${GEN_HEADER} ${GEN_CLIENT} ${GEN_SERVER}
    COMMAND ${MIDL_EXECUTABLE}
            /nologo
            /env x64
            /h ipc_h.h
            /cstub ipc_c.c
            /sstub ipc_s.c
            /out ${GEN_DIR}
            ${IDL_FILE}
    MAIN_DEPENDENCY ${IDL_FILE}
    WORKING_DIRECTORY ${GEN_DIR}
    COMMENT "MIDL compiling ipc.idl"
    VERBATIM
)

add_library(ipc STATIC
    ${GEN_CLIENT}
    ${GEN_SERVER}
    ${GEN_HEADER}
)

target_include_directories(ipc PUBLIC
    ${GEN_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(ipc PUBLIC rpcrt4)
