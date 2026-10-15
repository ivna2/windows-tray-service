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
| Пункт «Выход» | `IDM_TRAY_EXIT` завершает приложение |
| Перерисовка иконки при пересоздании панели задач | обработка `RegisterWindowMessage("TaskbarCreated")` |
| Запуск со скрытым окном | флаг `--hidden` |
| Работа в фоне при закрытии окна | `WM_CLOSE` прячет окно вместо выхода |
| Меню «Файл → Выход» | `IDM_FILE_EXIT` |
| Один экземпляр на пользователя | именованный мьютекс `Global\TrayApp_SingleInstance_<SID>` |
| Сборка на конвейере | CMake + MSBuild (`.github/workflows/build.yml`) |
| Артефакт сборки | `windows-binaries` (exe) |

### Сборка локально

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
# .\build\tray-app\Release\tray-app.exe
# .\build\tray-app\Release\tray-app.exe --hidden   # старт со скрытым окном
```
