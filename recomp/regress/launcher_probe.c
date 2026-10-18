/* Test-only launcher observer and failure/boundary checks. Never distribute. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(2); } } while (0)

#ifdef LAUNCHER_CHILD_PROBE
/* The real launcher starts this harmless stand-in as moonstone.exe. Write an
 * atomic UTF-16 record so Python can check the exact raw command and argv. */
static void write_bytes(HANDLE file, const void *data, DWORD size) {
    DWORD written;
    CHECK(WriteFile(file, data, size, &written, NULL) && written == size);
}
static void write_text(HANDLE file, const wchar_t *text) {
    DWORD length = (DWORD)wcslen(text);
    write_bytes(file, &length, sizeof(length));
    write_bytes(file, text, length * sizeof(wchar_t));
}
int wmain(int argc, wchar_t **argv) {
    static wchar_t path[32768], temporary[32768], cwd[32768], marker[128];
    CHECK(GetEnvironmentVariableW(L"MOON_LAUNCHER_PROBE_RESULT", path, 32768));
    CHECK(swprintf(temporary, 32768, L"%ls.tmp", path) > 0);
    CHECK(GetCurrentDirectoryW(32768, cwd));
    CHECK(GetEnvironmentVariableW(L"MOON_LAUNCHER_PROBE_MARKER", marker, 128));
    HANDLE file = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    CHECK(file != INVALID_HANDLE_VALUE);
    DWORD header[] = {0x4c41554e, (DWORD)argc, GetConsoleWindow() != NULL};
    write_bytes(file, header, sizeof(header));
    write_text(file, GetCommandLineW());
    write_text(file, cwd);
    write_text(file, marker);
    for (int i = 0; i < argc; ++i) write_text(file, argv[i]);
    CHECK(CloseHandle(file));
    CHECK(MoveFileExW(temporary, path, 0));
    return 0;
}
#else
/* Substitute only process/UI boundaries. Error formatting uses the actual
 * Windows API; the production source itself is compiled into this test. */
static wchar_t module[32768], raw[32768], got_exe[32768], got_cmd[32768], got_cwd[32768];
static wchar_t error_text[1024];
static DWORD module_result, module_error, create_error, loader_error;
static int creates, closes, messages, real_create, fail_format;
static DWORD WINAPI probe_module(HMODULE handle, LPWSTR buffer, DWORD size) {
    CHECK(handle == NULL && size == 32768);
    if (module_error) { SetLastError(module_error); return 0; }
    if (module_result) return module_result;
    wcscpy(buffer, module);
    return (DWORD)wcslen(module);
}
static LPWSTR WINAPI probe_command(void) { return raw; }
static BOOL WINAPI probe_create(LPCWSTR app, LPWSTR command,
    LPSECURITY_ATTRIBUTES pa, LPSECURITY_ATTRIBUTES ta, BOOL inherit, DWORD flags,
    LPVOID env, LPCWSTR cwd, LPSTARTUPINFOW startup, LPPROCESS_INFORMATION process) {
    ++creates;
    CHECK(!pa && !ta && !inherit && !flags && !env);
    CHECK(startup->cb == sizeof(*startup) && !startup->dwFlags && !startup->lpDesktop);
    wcscpy(got_exe, app); wcscpy(got_cmd, command); wcscpy(got_cwd, cwd);
    if (real_create) {
        BOOL result = CreateProcessW(app, command, pa, ta, inherit, flags, env, cwd, startup, process);
        loader_error = GetLastError();
        return result;
    }
    if (create_error) { SetLastError(create_error); return FALSE; }
    process->hThread = (HANDLE)(UINT_PTR)123;
    process->hProcess = (HANDLE)(UINT_PTR)456;
    return TRUE;
}
static BOOL WINAPI probe_close(HANDLE handle) {
    CHECK(handle == (HANDLE)(UINT_PTR)(closes ? 456 : 123));
    ++closes;
    return TRUE;
}
static int WINAPI probe_message(HWND owner, LPCWSTR text, LPCWSTR title, UINT flags) {
    CHECK(!owner && !wcscmp(title, L"Moonstone") && flags == (MB_OK | MB_ICONERROR));
    CHECK(wcslen(text) < 1024);
    wcscpy(error_text, text); ++messages;
    return IDOK;
}
static DWORD WINAPI probe_format(DWORD flags, LPCVOID source, DWORD id, DWORD language,
    LPWSTR output, DWORD size, va_list *args) {
    if (fail_format && (flags & FORMAT_MESSAGE_FROM_STRING)) return 0;
    return FormatMessageW(flags, source, id, language, output, size, args);
}
#define GetModuleFileNameW probe_module
#define GetCommandLineW probe_command
#define CreateProcessW probe_create
#define CloseHandle probe_close
#define MessageBoxW probe_message
#define FormatMessageW probe_format
#include "../src/save_launcher.c"
#undef GetModuleFileNameW
#undef GetCommandLineW
#undef CreateProcessW
#undef CloseHandle
#undef MessageBoxW
#undef FormatMessageW

static void reset(void) {
    module_result = module_error = create_error = loader_error = 0;
    creates = closes = messages = real_create = fail_format = 0;
    error_text[0] = got_exe[0] = got_cmd[0] = got_cwd[0] = 0;
    wcscpy(module, L"C:\\Game Folder\\Moonstone Singleplayer.exe");
    wcscpy(raw, L"\"C:\\Game Folder\\Moonstone Singleplayer.exe\" --log \"scratch log\"");
}
static void check_error(DWORD code, int expected_creates) {
    CHECK(launch_game() == 1);
    CHECK(messages == 1 && creates == expected_creates && closes == 0);
    if (real_create && code == ERROR_BAD_EXE_FORMAT) {
        /* Windows versions can reject a non-PE file as bad format or as an
         * unsupported legacy executable. Both must retain the actual code. */
        CHECK(loader_error == ERROR_BAD_EXE_FORMAT || loader_error == ERROR_EXE_MACHINE_TYPE_MISMATCH);
        code = loader_error;
    }
    wchar_t expected[64];
    swprintf(expected, 64, L"Windows error %lu:", (unsigned long)code);
    if (!wcsstr(error_text, expected)) fprintf(stderr, "Expected %ls; got %ls\n", expected, error_text);
    CHECK(wcsstr(error_text, expected));
    CHECK(wcsstr(error_text, L"Keep this launcher beside moonstone.exe"));
}
int wmain(int argc, wchar_t **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    setvbuf(stdout, NULL, _IONBF, 0);
    static const struct { const wchar_t *input, *expected; } tails[] = {
        {L"", L""}, {L"launcher.exe", L""}, {L"\"a b.exe\"", L""},
        {L"launcher.exe  \t  --log \"a b\"", L"--log \"a b\""},
        {L"\"a b.exe\"\t--x \"\" tail", L"--x \"\" tail"},
        {L"a\" b\".exe --flag", L"--flag"},
        {L"\"unterminated name --flag", L""},
        {L"x.exe\r\n--flag", L"--flag"}, {L" x.exe --flag", L"x.exe --flag"},
        {L"\"C:\\a b\\x.exe\" --literal %PATH% & >", L"--literal %PATH% & >"}
    };
    for (unsigned i = 0; i < sizeof(tails) / sizeof(tails[0]); ++i)
        CHECK(!wcscmp(argument_tail(tails[i].input), tails[i].expected));

    wchar_t guarded[] = {L'L', 0, 0, 0, L'R'};
    DWORD used = 0;
    CHECK(append_text(guarded + 1, 3, &used, L"ab"));
    CHECK(!append_text(guarded + 1, 3, &used, L"c"));
    CHECK(used == 2 && !wcscmp(guarded + 1, L"ab") && guarded[0] == L'L' && guarded[4] == L'R');
    used = 0;
    CHECK(append_text(guarded + 1, 1, &used, L""));
    CHECK(!append_text(guarded + 1, 1, &used, L"a") && used == 0);
    puts("PASS argument tails and buffer guards");

    reset(); CHECK(launch_game() == 0 && creates == 1 && closes == 2 && !messages);
    CHECK(!wcscmp(got_exe, L"C:\\Game Folder\\moonstone.exe"));
    CHECK(!wcscmp(got_cwd, L"C:\\Game Folder\\"));
    CHECK(wcsstr(got_cmd, MOON_MULTIPLAYER ? L"--save-profile multiplayer " : L"--save-profile singleplayer "));
    CHECK(wcsstr(got_cmd, L"--log \"scratch log\""));
    reset(); wcscpy(module, L"F:\\Launcher.exe");
    CHECK(launch_game() == 0 && !wcscmp(got_cwd, L"F:\\") && !wcscmp(got_exe, L"F:\\moonstone.exe"));
    reset(); wcscpy(module, L"\\\\server\\share\\Launcher.exe");
    CHECK(launch_game() == 0 && !wcscmp(got_cwd, L"\\\\server\\share\\"));
    reset(); wcscpy(module, L"\\\\?\\C:\\Game\\Launcher.exe");
    CHECK(launch_game() == 0 && !wcscmp(got_exe, L"\\\\?\\C:\\Game\\moonstone.exe"));
    puts("PASS Windows paths and child handle cleanup");

    reset(); module_error = ERROR_ACCESS_DENIED; check_error(ERROR_ACCESS_DENIED, 0);
    reset(); module_result = 32768; check_error(ERROR_FILENAME_EXCED_RANGE, 0);
    reset(); wcscpy(module, L"no-separator"); check_error(ERROR_PATH_NOT_FOUND, 0);
    reset(); wmemset(module, L'a', 32766); module[0] = L'C'; module[1] = L':';
    module[32764] = L'\\'; module[32766] = 0;
    check_error(ERROR_FILENAME_EXCED_RANGE, 0);
    const DWORD failures[] = {2, 5, 193, 1114, 0xffffffff};
    for (unsigned i = 0; i < sizeof(failures) / sizeof(failures[0]); ++i) {
        reset(); create_error = failures[i]; check_error(failures[i], 1);
    }
    reset(); fail_format = 1; create_error = 2;
    CHECK(launch_game() == 1 && messages == 1 && wcsstr(error_text, L"extract the complete game folder."));
    puts("PASS startup errors and message formatting");

    /* The complete child command can contain 32767 UTF-16 units plus NUL. */
    reset(); wcscpy(module, L"C:\\x.exe"); wcscpy(raw, L"x ");
    CHECK(launch_game() == 0);
    size_t prefix = wcslen(got_cmd);
    size_t available = 32767 - prefix;
    reset(); wcscpy(module, L"C:\\x.exe"); raw[0] = L'x'; raw[1] = L' ';
    wmemset(raw + 2, L'a', available); raw[2 + available] = 0;
    CHECK(launch_game() == 0 && wcslen(got_cmd) == 32767);
    reset(); wcscpy(module, L"C:\\x.exe"); raw[0] = L'x'; raw[1] = L' ';
    wmemset(raw + 2, L'a', available + 1); raw[3 + available] = 0;
    check_error(ERROR_FILENAME_EXCED_RANGE, 0);
    puts("PASS command-line length boundaries");

    /* Real Windows loader failures in private folders; only the dialog is
     * captured instead of making the operator dismiss it. */
    CHECK(argc == 3);
    for (int i = 1; i < argc; ++i) {
        reset(); real_create = 1;
        CHECK(swprintf(module, 32768, L"%ls\\Launcher.exe", argv[i]) > 0);
        check_error(i == 1 ? ERROR_FILE_NOT_FOUND : ERROR_BAD_EXE_FORMAT, 1);
    }
    printf("PASS profile %d: argument tails, bounded buffers, paths, handles, error formatting, real missing/invalid child\n", MOON_MULTIPLAYER);
    return 0;
}
#endif
