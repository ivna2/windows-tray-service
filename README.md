# windows-tray-service

Учебный проект по дисциплине «Защита информации от вредоносного ПО».

## Задание 2.1 — графическое приложение с иконкой в трее

Приложение `tray-app` на Win32 API:

| Требование | Реализация |
|---|---|
| Иконка в трее при запуске | `Shell_NotifyIcon(NIM_ADD)` в `WM_CREATE` |
| Левый клик по иконке — главное окно | `WM_TRAYICON` → `WM_LBUTTONUP` |
| Правый клик — контекстное меню | `WM_TRAYICON` → `WM_RBUTTONUP`, `TrackPopupMenu` |
| Пункт «Открыть» | `IDM_TRAY_OPEN` показывает окно |
| Пункт «Выход» | `IDM_TRAY_EXIT` |
| Перерисовка иконки при пересоздании панели задач | обработка `RegisterWindowMessage("TaskbarCreated")` |
| Запуск со скрытым окном | флаг `--hidden` |
| Работа в фоне при закрытии окна | `WM_CLOSE` прячет окно вместо выхода |
| Меню «Файл → Выход» | `IDM_FILE_EXIT` |
| Один экземпляр на пользователя | именованный мьютекс `Global\TrayApp_SingleInstance_<SID>` |
| Сборка на конвейере | CMake + MSBuild (`.github/workflows/build.yml`) |
| Артефакт сборки | `windows-binaries` (exe) |

## Задание 2.2 — Windows-служба и взаимодействие с приложением

Служба `TrayControlService` + расширенный `tray-app`, общение через **Windows RPC с транспортом ALPC** (`ncalrpc`).

### Служба

| Требование | Реализация |
|---|---|
| Запуск GUI во всех терминальных сессиях, кроме 0, от имени владельца, окно скрыто | `WTSEnumerateSessions` + `WTSQueryUserToken` + `CreateProcessAsUser` (`SW_HIDE`, `--hidden`) |
| Отслеживание входов новых пользователей | `SERVICE_CONTROL_SESSIONCHANGE` → `WTS_SESSION_LOGON` |
| Отключение сигналов Stop и Shutdown | `dwControlsAccepted` без `SERVICE_ACCEPT_STOP/SHUTDOWN`; обработчик игнорирует их |
| RPC-сервер поверх ALPC, служба работает до остановки сервера | `RpcServerUseProtseqEp("ncalrpc")` + блокирующий `RpcServerListen` |
| Интерфейс остановки для клиентов | `ServiceControl::SvcStop` (ipc.idl) |
| Завершение всех GUI при остановке | список хэндлов процессов → `TerminateProcess` |

Установка / удаление службы:

```powershell
TrayControlService.exe install
TrayControlService.exe uninstall
```

### Графическое приложение (дополнения 2.2)

| Требование | Реализация |
|---|---|
| Проверка состояния службы, запуск и ожидание Running, затем выход | `EnsureServiceRunning` (при запуске не от службы) |
| Проверка родительского процесса (должна быть служба) | `IsParentTheService`; иначе выход |
| «Выход» в меню «Файл» останавливает службу | `StopServiceViaRpc` |
| «Выход» в контекстном меню трея останавливает службу | `StopServiceViaRpc` |

Необязательные требования (DACL-защита от завершения процессов) в этом проекте не реализованы.

### Сборка локально

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Бинарники: `build\tray-app\Release\tray-app.exe`, `build\service\Release\TrayControlService.exe`.
