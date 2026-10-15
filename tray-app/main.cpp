// Tray application (Task 2.1).
//
// A minimal Win32 GUI application that lives in the taskbar notification area
// (the tray). It demonstrates the required behaviour:
//   * adds a tray icon on start-up;
//   * left click on the icon shows the main window;
//   * right click on the icon shows a context menu ("Открыть", "Выход");
//   * the main window has a "Файл -> Выход" menu item;
//   * re-adds its tray icon when the taskbar is recreated (TaskbarCreated);
//   * can start with the main window hidden (--hidden);
//   * keeps running in the background when the main window is closed;
//   * refuses to run more than once for the same user (named mutex).

#include <windows.h>
#include <shellapi.h>
#include <sddl.h>
#include <string>
#include "resource.h"

namespace {

const wchar_t* kWindowClass = L"TrayAppMainWindow";
const wchar_t* kWindowTitle = L"Tray Application";

NOTIFYICONDATAW g_nid = {};
UINT g_taskbarCreatedMsg = 0;   // RegisterWindowMessage("TaskbarCreated")
HANDLE g_singleInstanceMutex = nullptr;

// Builds a mutex name that is unique for the current user so that the
// application can only run once per user. The user SID is stable across
// sessions, which is exactly the "one instance per user" semantics asked for.
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

// Returns true when this is the first instance for the user. On a repeated
// launch the mutex already exists and the function returns false, letting the
// caller exit before the tray icon is ever added.
bool AcquireSingleInstance() {
    const std::wstring mutexName = BuildPerUserMutexName();
    g_singleInstanceMutex = CreateMutexW(nullptr, TRUE, mutexName.c_str());
    if (g_singleInstanceMutex == nullptr) {
        return false;
    }
    return GetLastError() != ERROR_ALREADY_EXISTS;
}

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

    // Required so the menu closes correctly when the user clicks elsewhere.
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

// Fully quits the application: removes the tray icon and ends the message loop.
void QuitApplication(HWND hwnd) {
    RemoveTrayIcon();
    DestroyWindow(hwnd);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == g_taskbarCreatedMsg) {
        // The taskbar (explorer) was recreated: our tray icon is gone, re-add it.
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
            QuitApplication(hwnd);
            break;
        }
        return 0;

    case WM_CLOSE:
        // Closing the main window hides it; the app keeps running in the tray.
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        RemoveTrayIcon();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Recognises the flags that request a hidden start-up.
bool WantsHiddenStart(LPWSTR cmdLine) {
    std::wstring args = cmdLine ? cmdLine : L"";
    return args.find(L"--hidden") != std::wstring::npos ||
           args.find(L"/hidden") != std::wstring::npos ||
           args.find(L"-hidden") != std::wstring::npos;
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int) {
    // Refuse a second instance before the tray icon is added.
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
