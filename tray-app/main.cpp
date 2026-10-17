// Tray application (extended for Task 2.2).
//
// In addition to the Task 2.1 behaviour (tray icon, context menu, single
// instance, hidden start, background operation), this version cooperates with
// the Windows service:
//   * on start it checks who the parent process is. If the parent is not the
//     service, it makes sure the service is running (starting it and waiting
//     for the Running state if necessary) and then exits;
//   * only when launched by the service does it run as a normal tray app;
//   * the "Выход" items (window menu and tray menu) stop the service via RPC
//     over ALPC. The service then terminates every application it launched.

#include <windows.h>
#include <shellapi.h>
#include <sddl.h>
#include <winsvc.h>
#include <tlhelp32.h>
#include <rpc.h>
#include <string>
#include <vector>

#include "ipc_h.h"
#include "ipc_endpoint.h"
#include "resource.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "rpcrt4.lib")

namespace {

const wchar_t* kWindowClass = L"TrayAppMainWindow";
const wchar_t* kWindowTitle = L"Tray Application";
const wchar_t* kServiceImageName = L"TrayControlService.exe";

NOTIFYICONDATAW g_nid = {};
UINT g_taskbarCreatedMsg = 0;
HANDLE g_singleInstanceMutex = nullptr;

// ---- single instance (Task 2.1) -------------------------------------------

std::wstring BuildPerUserMutexName() {
    std::wstring name = L"Global\\TrayApp_SingleInstance";
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        DWORD size = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        if (size > 0) {
            std::string buffer(size, '\0');
            if (GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
                auto* tu = reinterpret_cast<TOKEN_USER*>(buffer.data());
                LPWSTR sidText = nullptr;
                if (ConvertSidToStringSidW(tu->User.Sid, &sidText)) {
                    name += L"_";
                    name += sidText;
                    LocalFree(sidText);
                }
            }
        }
        CloseHandle(token);
    }
    return name;
}

bool AcquireSingleInstance() {
    const std::wstring mutexName = BuildPerUserMutexName();
    g_singleInstanceMutex = CreateMutexW(nullptr, TRUE, mutexName.c_str());
    if (g_singleInstanceMutex == nullptr) {
        return false;
    }
    return GetLastError() != ERROR_ALREADY_EXISTS;
}

// ---- service interaction (Task 2.2) ---------------------------------------

DWORD GetParentProcessId() {
    DWORD pid = GetCurrentProcessId();
    DWORD parent = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return 0;
    }
    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID == pid) {
                parent = pe.th32ParentProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return parent;
}

std::wstring GetProcessImageName(DWORD pid) {
    std::wstring result;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h) {
        wchar_t buffer[MAX_PATH] = {};
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(h, 0, buffer, &size)) {
            result = buffer;
        }
        CloseHandle(h);
    }
    return result;
}

// Returns the PID of the service process (0 if the service is not running).
// This only needs SERVICE_QUERY_STATUS, which an ordinary user process can
// obtain even though the service itself runs as LocalSystem.
DWORD GetServiceProcessId() {
    DWORD pid = 0;
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm) {
        SC_HANDLE svc = OpenServiceW(scm, SERVICE_NAME, SERVICE_QUERY_STATUS);
        if (svc) {
            SERVICE_STATUS_PROCESS ssp = {};
            DWORD needed = 0;
            if (QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO,
                    reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &needed)) {
                pid = ssp.dwProcessId;
            }
            CloseServiceHandle(svc);
        }
        CloseServiceHandle(scm);
    }
    return pid;
}

// True when the parent process is the Windows service.
//
// The reliable check compares the parent PID with the service's own process
// id reported by the SCM: a medium-integrity user process cannot read the
// image name of the LocalSystem service process, so the image-name test is
// only kept as a fallback.
bool IsParentTheService() {
    DWORD parent = GetParentProcessId();
    if (parent == 0) {
        return false;
    }

    DWORD servicePid = GetServiceProcessId();
    if (servicePid != 0 && parent == servicePid) {
        return true;
    }

    std::wstring image = GetProcessImageName(parent);
    if (image.empty()) {
        return false;
    }
    size_t slash = image.find_last_of(L"\\/");
    std::wstring base = (slash == std::wstring::npos) ? image : image.substr(slash + 1);
    return _wcsicmp(base.c_str(), kServiceImageName) == 0;
}

// Ensures the service is running: starts it if stopped and waits for Running.
void EnsureServiceRunning() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) {
        return;
    }
    SC_HANDLE svc = OpenServiceW(scm, SERVICE_NAME, SERVICE_QUERY_STATUS | SERVICE_START);
    if (svc) {
        SERVICE_STATUS status = {};
        if (QueryServiceStatus(svc, &status) && status.dwCurrentState == SERVICE_STOPPED) {
            StartServiceW(svc, 0, nullptr);
        }
        // Wait (bounded) for the Running state.
        for (int i = 0; i < 100; ++i) {
            if (!QueryServiceStatus(svc, &status)) {
                break;
            }
            if (status.dwCurrentState == SERVICE_RUNNING) {
                break;
            }
            Sleep(100);
        }
        CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);
}

// Stops the service through RPC over ALPC (ncalrpc).
void StopServiceViaRpc() {
    RPC_WSTR stringBinding = nullptr;
    if (RpcStringBindingComposeW(
            nullptr,
            reinterpret_cast<RPC_WSTR>(const_cast<wchar_t*>(IPC_PROTSEQ)),
            nullptr,
            reinterpret_cast<RPC_WSTR>(const_cast<wchar_t*>(IPC_ENDPOINT)),
            nullptr, &stringBinding) != RPC_S_OK) {
        return;
    }

    handle_t binding = nullptr;
    if (RpcBindingFromStringBindingW(stringBinding, &binding) == RPC_S_OK) {
        RpcTryExcept {
            SvcStop(binding);
        }
        RpcExcept(1) {
            // Service already gone / not reachable: nothing to do.
        }
        RpcEndExcept
        RpcBindingFree(&binding);
    }
    RpcStringFreeW(&stringBinding);
}

// ---- tray / window (Task 2.1) ---------------------------------------------

void InitTrayIconData(HWND hwnd) {
    g_nid = {};
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = TRAY_ICON_ID;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, kWindowTitle);
}

void AddTrayIcon(HWND hwnd) {
    InitTrayIconData(hwnd);
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void RemoveTrayIcon() {
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
}

void ShowMainWindow(HWND hwnd) {
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
}

void ShowTrayContextMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_TRAY_OPEN, L"Открыть");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_TRAY_EXIT, L"Выход");

    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
}

HMENU CreateMainMenu() {
    HMENU fileMenu = CreatePopupMenu();
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_EXIT, L"Выход");

    HMENU menuBar = CreateMenu();
    AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(fileMenu), L"Файл");
    return menuBar;
}

// "Выход" stops the whole service (Task 2.2, requirements 3 and 4). The
// service terminates this application as part of its stop sequence.
void OnExitRequested(HWND hwnd) {
    StopServiceViaRpc();
    RemoveTrayIcon();
    DestroyWindow(hwnd);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == g_taskbarCreatedMsg) {
        AddTrayIcon(hwnd);
        return 0;
    }

    switch (msg) {
    case WM_CREATE:
        AddTrayIcon(hwnd);
        return 0;

    case WM_TRAYICON:
        switch (LOWORD(lParam)) {
        case WM_LBUTTONUP:
            ShowMainWindow(hwnd);
            break;
        case WM_RBUTTONUP:
            ShowTrayContextMenu(hwnd);
            break;
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_TRAY_OPEN:
            ShowMainWindow(hwnd);
            break;
        case IDM_TRAY_EXIT:
        case IDM_FILE_EXIT:
            OnExitRequested(hwnd);
            break;
        }
        return 0;

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        RemoveTrayIcon();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool WantsHiddenStart(LPWSTR cmdLine) {
    std::wstring args = cmdLine ? cmdLine : L"";
    return args.find(L"--hidden") != std::wstring::npos ||
           args.find(L"/hidden") != std::wstring::npos ||
           args.find(L"-hidden") != std::wstring::npos;
}

} // namespace

// MIDL memory-management callbacks required by the generated client stub.
void* __RPC_USER MIDL_user_allocate(size_t size) { return malloc(size); }
void __RPC_USER MIDL_user_free(void* p) { free(p); }

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int) {
    // Task 2.2: the application only runs as a tray app when the service is its
    // parent. Any other (manual) launch just ensures the service is running
    // and then exits.
    if (!IsParentTheService()) {
        EnsureServiceRunning();
        return 0;
    }

    if (!AcquireSingleInstance()) {
        return 0;
    }

    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc)) {
        return 1;
    }

    HWND hwnd = CreateWindowExW(
        0, kWindowClass, kWindowTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 480, 320,
        nullptr, CreateMainMenu(), hInstance, nullptr);
    if (!hwnd) {
        return 1;
    }

    const bool hidden = WantsHiddenStart(lpCmdLine);
    ShowWindow(hwnd, hidden ? SW_HIDE : SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_singleInstanceMutex) {
        CloseHandle(g_singleInstanceMutex);
    }
    return static_cast<int>(msg.wParam);
}
