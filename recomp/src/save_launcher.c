/* Portable, console-free save-profile launchers. GPL-3.0; see ../../LICENSE.
 * Built twice with the existing game icon. All gameplay stays in moonstone.exe.
 * Resolve siblings from this EXE, never a shortcut's working directory. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
#include <stdio.h>

#ifndef MOON_MULTIPLAYER
#error Define MOON_MULTIPLAYER as 0 or 1
#endif

static int launch_error(DWORD code) {
    wchar_t detail[512] = L"", message[1024];
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, code, 0, detail, 512, NULL);
    swprintf(message, 1024,
             L"Moonstone could not start. Keep this launcher beside moonstone.exe "
             L"and extract the complete game folder.\n\nWindows error %lu: %ls",
             (unsigned long)code, detail);
    MessageBoxW(NULL, message, L"Moonstone", MB_OK | MB_ICONERROR);
    return 1;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR args, int show) {
    (void)instance; (void)previous; (void)show;
    static wchar_t directory[32768], exe[32768], command[32768];
    DWORD n = GetModuleFileNameW(NULL, directory, 32768);
    if (!n) return launch_error(GetLastError());
    if (n >= 32768) return launch_error(ERROR_FILENAME_EXCED_RANGE);
    wchar_t *slash = wcsrchr(directory, L'\\');
    if (!slash) return launch_error(ERROR_PATH_NOT_FOUND);
    *slash = 0;
    int length = swprintf(exe, 32768, L"%ls\\moonstone.exe", directory);
    if (length < 0 || length >= 32768) return launch_error(ERROR_FILENAME_EXCED_RANGE);
    /* Forward optional diagnostic flags verbatim (including quoted --log paths).
     * Explicit SDL/OS flags retain normal play despite main's no-argument default. */
    length = swprintf(command, 32768, L"\"%ls\" --os --sdl --save-profile %ls %ls",
                      exe, MOON_MULTIPLAYER ? L"multiplayer" : L"singleplayer", args ? args : L"");
    if (length < 0 || length >= 32768) return launch_error(ERROR_FILENAME_EXCED_RANGE);
    STARTUPINFOW startup = {0};
    PROCESS_INFORMATION process = {0};
    startup.cb = sizeof(startup);
    if (!CreateProcessW(exe, command, NULL, NULL, FALSE, 0, NULL, directory, &startup, &process))
        return launch_error(GetLastError());
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}
