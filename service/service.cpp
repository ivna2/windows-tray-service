// Windows service (Task 2.2).
//
// Responsibilities:
//   * launches the tray application in every terminal session except
//     session 0, running it as the session owner with the window hidden;
//   * watches for new logons and launches the app in the new session;
//   * does NOT accept Stop / Shutdown controls (it can only be stopped via RPC);
//   * runs an RPC server over ALPC (ncalrpc) exposing a "stop" interface;
//   * terminates every launched application when it is stopped.
//
// The service can also install / uninstall itself:
//     TrayControlService.exe install
//     TrayControlService.exe uninstall

#include <windows.h>
#include <winsvc.h>
#include <wtsapi32.h>
#include <userenv.h>
#include <rpc.h>
#include <string>
#include <vector>
#include <mutex>

#include "ipc_h.h"
#include "ipc_endpoint.h"

#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "userenv.lib")
#pragma comment(lib, "rpcrt4.lib")
#pragma comment(lib, "advapi32.lib")

namespace {

SERVICE_STATUS_HANDLE g_statusHandle = nullptr;
SERVICE_STATUS g_status = {};

std::mutex g_childrenMutex;
std::vector<HANDLE> g_children;   // handles of launched tray-app processes

// Absolute path to tray-app.exe, resolved next to the service executable.
std::wstring g_trayAppPath;

void ReportStatus(DWORD state, DWORD controlsAccepted) {
    g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_status.dwCurrentState = state;
    g_status.dwControlsAccepted = controlsAccepted;
    g_status.dwWin32ExitCode = NO_ERROR;
    g_status.dwWaitHint = 3000;
    SetServiceStatus(g_statusHandle, &g_status);
}

std::wstring ResolveTrayAppPath() {
    wchar_t buffer[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    std::wstring path = buffer;
    size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        path.erase(slash + 1);
    }
    path += L"tray-app.exe";
    return path;
}

// Launches the tray application inside a single terminal session, under the
// session owner's token, with the main window hidden.
void LaunchInSession(DWORD sessionId) {
    if (sessionId == 0) {
        return; // session 0 is the non-interactive services session
    }

    HANDLE userToken = nullptr;
    if (!WTSQueryUserToken(sessionId, &userToken)) {
        return; // no interactive user logged on in this session
    }

    LPVOID env = nullptr;
    CreateEnvironmentBlock(&env, userToken, FALSE);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.lpDesktop = const_cast<LPWSTR>(L"winsta0\\default");
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};
    std::wstring cmd = L"\"" + g_trayAppPath + L"\" --hidden";
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    BOOL ok = CreateProcessAsUserW(
        userToken,
        g_trayAppPath.c_str(),
        cmdBuf.data(),
        nullptr, nullptr, FALSE,
        CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_CONSOLE,
        env, nullptr, &si, &pi);

    if (ok) {
        CloseHandle(pi.hThread);
        std::lock_guard<std::mutex> lock(g_childrenMutex);
        g_children.push_back(pi.hProcess);
    }

    if (env) {
        DestroyEnvironmentBlock(env);
    }
    CloseHandle(userToken);
}

// Launches the tray application in every active terminal session (except 0).
void LaunchInAllSessions() {
    PWTS_SESSION_INFOW sessions = nullptr;
    DWORD count = 0;
    if (!WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &sessions, &count)) {
        return;
    }
    for (DWORD i = 0; i < count; ++i) {
        if (sessions[i].SessionId != 0) {
            LaunchInSession(sessions[i].SessionId);
        }
    }
    WTSFreeMemory(sessions);
}

// Terminates every launched tray application. Called when the service stops.
void TerminateAllLaunched() {
    std::lock_guard<std::mutex> lock(g_childrenMutex);
    for (HANDLE h : g_children) {
        if (h) {
            TerminateProcess(h, 0);
            CloseHandle(h);
        }
    }
    g_children.clear();
}

// ---- RPC server -----------------------------------------------------------

bool StartRpcServer() {
    RPC_STATUS s = RpcServerUseProtseqEpW(
        reinterpret_cast<RPC_WSTR>(const_cast<wchar_t*>(IPC_PROTSEQ)),
        RPC_C_PROTSEQ_MAX_REQS_DEFAULT,
        reinterpret_cast<RPC_WSTR>(const_cast<wchar_t*>(IPC_ENDPOINT)),
        nullptr);
    if (s != RPC_S_OK) {
        return false;
    }

    s = RpcServerRegisterIf2(
        ServiceControl_v1_0_s_ifspec, nullptr, nullptr,
        RPC_IF_ALLOW_LOCAL_ONLY,
        RPC_C_LISTEN_MAX_CALLS_DEFAULT, (DWORD)-1, nullptr);
    return s == RPC_S_OK;
}

// ---- Service control handler ----------------------------------------------

DWORD WINAPI ServiceCtrlHandler(DWORD control, DWORD eventType, LPVOID eventData, LPVOID) {
    switch (control) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        // Requirement: Stop and Shutdown handling is disabled. The service
        // ignores these and can only be stopped through the RPC interface.
        return NO_ERROR;

    case SERVICE_CONTROL_SESSIONCHANGE:
        if (eventType == WTS_SESSION_LOGON) {
            auto* notify = reinterpret_cast<WTSSESSION_NOTIFICATION*>(eventData);
            LaunchInSession(notify->dwSessionId);
        }
        return NO_ERROR;

    case SERVICE_CONTROL_INTERROGATE:
        return NO_ERROR;

    default:
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

void WINAPI ServiceMain(DWORD, LPWSTR*) {
    g_statusHandle = RegisterServiceCtrlHandlerExW(SERVICE_NAME, ServiceCtrlHandler, nullptr);
    if (!g_statusHandle) {
        return;
    }

    ReportStatus(SERVICE_START_PENDING, 0);
    g_trayAppPath = ResolveTrayAppPath();

    if (!StartRpcServer()) {
        ReportStatus(SERVICE_STOPPED, 0);
        return;
    }

    // Accept only session-change notifications; Stop/Shutdown are not accepted.
    ReportStatus(SERVICE_RUNNING, SERVICE_ACCEPT_SESSIONCHANGE);

    LaunchInAllSessions();

    // Blocks until SvcStop calls RpcMgmtStopServerListening.
    RpcServerListen(1, RPC_C_LISTEN_MAX_CALLS_DEFAULT, FALSE);

    // Stopping.
    ReportStatus(SERVICE_STOP_PENDING, 0);
    RpcServerUnregisterIf(ServiceControl_v1_0_s_ifspec, nullptr, FALSE);
    TerminateAllLaunched();
    ReportStatus(SERVICE_STOPPED, 0);
}

// ---- install / uninstall ---------------------------------------------------

int InstallService() {
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!scm) {
        return 1;
    }

    SC_HANDLE svc = CreateServiceW(
        scm, SERVICE_NAME, SERVICE_NAME,
        SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL, exePath,
        nullptr, nullptr, nullptr, nullptr, nullptr);

    int rc = svc ? 0 : 1;
    if (svc) {
        CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);
    return rc;
}

int UninstallService() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) {
        return 1;
    }
    SC_HANDLE svc = OpenServiceW(scm, SERVICE_NAME, DELETE);
    int rc = 1;
    if (svc) {
        rc = DeleteService(svc) ? 0 : 1;
        CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);
    return rc;
}

} // namespace

// ---- RPC interface implementation ------------------------------------------

void SvcStop(handle_t /*hBinding*/) {
    // Signals the blocking RpcServerListen to return, which drives the stop
    // sequence in ServiceMain (terminate launched apps + report stopped).
    RpcMgmtStopServerListening(nullptr);
}

// MIDL memory-management callbacks.
void* __RPC_USER MIDL_user_allocate(size_t size) { return malloc(size); }
void __RPC_USER MIDL_user_free(void* p) { free(p); }

int wmain(int argc, wchar_t** argv) {
    if (argc > 1) {
        if (_wcsicmp(argv[1], L"install") == 0) {
            return InstallService();
        }
        if (_wcsicmp(argv[1], L"uninstall") == 0) {
            return UninstallService();
        }
    }

    SERVICE_TABLE_ENTRYW table[] = {
        { const_cast<LPWSTR>(SERVICE_NAME), ServiceMain },
        { nullptr, nullptr }
    };

    if (!StartServiceCtrlDispatcherW(table)) {
        return 1;
    }
    return 0;
}
