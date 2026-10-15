#pragma once

// Shared constants for the service <-> tray-app IPC (Task 2.2).

// ALPC endpoint name used with the "ncalrpc" protocol sequence.
#define IPC_PROTSEQ   L"ncalrpc"
#define IPC_ENDPOINT  L"tray-service-alpc"

// Service registration name.
#define SERVICE_NAME  L"TrayControlService"
