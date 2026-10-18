/* Portable, console-free save-profile launchers. GPL-3.0; see ../../LICENSE.
 * Built twice with the existing game icon. All gameplay stays in moonstone.exe.
 * Resolve siblings from this EXE, never a shortcut's working directory. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef MOON_MULTIPLAYER
#error Define MOON_MULTIPLAYER as 0 or 1
#endif

static int launch_error(DWORD code) {
    wchar_t detail[512], message[1024];
    DWORD_PTR inserts[] = {code, (DWORD_PTR)detail};
    detail[0] = 0;
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, code, 0, detail, 512, NULL);
    const wchar_t *fallback = L"Moonstone could not start. Keep this launcher beside "
                              L"moonstone.exe and extract the complete game folder.";
    DWORD length = FormatMessageW(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ARGUMENT_ARRAY,
        L"Moonstone could not start. Keep this launcher beside moonstone.exe "
        L"and extract the complete game folder.%n%nWindows error %1!u!: %2",
        0, 0, message, 1024, (va_list *)inserts);
    MessageBoxW(NULL, length ? message : fallback, L"Moonstone", MB_OK | MB_ICONERROR);
    return 1;
}

/* Preserve wWinMain's raw argument tail. Only the executable name is skipped;
 * quotes/backslashes in the remaining arguments must reach the game unchanged. */
static const wchar_t *argument_tail(const wchar_t *line) {
    BOOL quoted = FALSE;
    while (*line > L' ' || (*line && quoted)) {
        if (*line == L'"') quoted = !quoted;
        ++line;
    }
    while (*line && *line <= L' ') ++line;
    return line;
}

static BOOL append_text(wchar_t *buffer, DWORD capacity, DWORD *used, const wchar_t *text) {
    while (*text) {
        if (*used >= capacity - 1) {
            buffer[*used] = 0;
            return FALSE;
        }
        buffer[(*used)++] = *text++;
    }
    buffer[*used] = 0;
    return TRUE;
}

static int launch_game(void) {
    static wchar_t directory[32768], exe[32768], command[32768];
    DWORD n = GetModuleFileNameW(NULL, directory, 32768);
    if (!n) return launch_error(GetLastError());
    if (n >= 32768) return launch_error(ERROR_FILENAME_EXCED_RANGE);
    wchar_t *slash = NULL;
    for (DWORD i = 0; i < n; ++i)
        if (directory[i] == L'\\') slash = directory + i;
    if (!slash) return launch_error(ERROR_PATH_NOT_FOUND);
    /* Keep the final separator, including for an installation at a drive root. */
    slash[1] = 0;
    DWORD length = 0;
    if (!append_text(exe, 32768, &length, directory) ||
        !append_text(exe, 32768, &length, L"moonstone.exe"))
        return launch_error(ERROR_FILENAME_EXCED_RANGE);
    /* Forward optional diagnostic flags verbatim (including quoted --log paths).
     * Explicit SDL/OS flags retain normal play despite main's no-argument default. */
    length = 0;
    if (!append_text(command, 32768, &length, L"\"") ||
        !append_text(command, 32768, &length, exe) ||
        !append_text(command, 32768, &length, L"\" --os --sdl --save-profile ") ||
        !append_text(command, 32768, &length,
                     MOON_MULTIPLAYER ? L"multiplayer " : L"singleplayer ") ||
        !append_text(command, 32768, &length, argument_tail(GetCommandLineW())))
        return launch_error(ERROR_FILENAME_EXCED_RANGE);
    static STARTUPINFOW startup = {.cb = sizeof(STARTUPINFOW)};
    PROCESS_INFORMATION process;
    if (!CreateProcessW(exe, command, NULL, NULL, FALSE, 0, NULL, directory, &startup, &process))
        return launch_error(GetLastError());
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}

/* No CRT, constructors, stdio or heap allocation: Windows initializes the
 * static buffers, and the launcher only calls Windows APIs. The engine keeps
 * its normal runtime. This entry point is selected explicitly in build.sh. */
void WINAPI launcher_start(void) {
    ExitProcess((UINT)launch_game());
}
