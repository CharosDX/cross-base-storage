// dinput8.dll proxy loader
// Enshrouded imports DirectInput8Create from dinput8.dll. This proxy forwards that call to the system dinput8.dll
// and, when the game starts, loads every DLL in the "native" folder of each mod: mods\<mod>\native\*.dll.
// What it loads is written to mods\<mod>\native\native-loader.log, started fresh on every game start.
#include <windows.h>
#include <stdio.h>

typedef HRESULT (WINAPI *DirectInput8CreateFn)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);

static HMODULE system_dinput8;
static DirectInput8CreateFn system_create;

// Appends a line to native_dir\native-loader.log; fresh replaces the file from the previous game start.
static void log_line(const char *native_dir, int fresh, const char *text, const char *name)
{
    char path[MAX_PATH];
    if (sprintf_s(path, sizeof(path), "%snative-loader.log", native_dir) < 0) return;
    SYSTEMTIME time;
    GetLocalTime(&time);
    char line[700];
    int size = sprintf_s(line, sizeof(line), "%02d:%02d:%02d %s%s\r\n", time.wHour, time.wMinute, time.wSecond,
        text, name ? name : "");
    if (size <= 0) return;
    HANDLE file = CreateFileA(path, fresh ? GENERIC_WRITE : FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
        fresh ? CREATE_ALWAYS : OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written;
    WriteFile(file, line, (DWORD)size, &written, NULL);
    CloseHandle(file);
}

// Loads every *.dll in mods_dir\<mod>\native\.
static void load_mod_dlls(const char *mods_dir, const char *mod)
{
    char native_dir[MAX_PATH], pattern[MAX_PATH];
    if (sprintf_s(native_dir, sizeof(native_dir), "%s%s\\native\\", mods_dir, mod) < 0) return;
    if (sprintf_s(pattern, sizeof(pattern), "%s*.dll", native_dir) < 0) return;
    WIN32_FIND_DATAA found;
    HANDLE search = FindFirstFileA(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) return;
    int fresh = 1;
    do {
        char path[MAX_PATH];
        if (sprintf_s(path, sizeof(path), "%s%s", native_dir, found.cFileName) < 0) continue;
        log_line(native_dir, fresh, LoadLibraryA(path) ? "loaded " : "FAILED to load ", found.cFileName);
        fresh = 0;
    } while (FindNextFileA(search, &found));
    FindClose(search);
}

static void load_native_mods(void)
{
    char mods_dir[MAX_PATH];
    DWORD length = GetModuleFileNameA(NULL, mods_dir, MAX_PATH);
    char *slash = (length > 0 && length < MAX_PATH) ? strrchr(mods_dir, '\\') : NULL;
    if (!slash || strcpy_s(slash + 1, MAX_PATH - (slash + 1 - mods_dir), "mods\\") != 0) return;

    char pattern[MAX_PATH];
    if (sprintf_s(pattern, sizeof(pattern), "%s*", mods_dir) < 0) return;
    WIN32_FIND_DATAA found;
    HANDLE search = FindFirstFileA(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || found.cFileName[0] == '.') continue;
        load_mod_dlls(mods_dir, found.cFileName);
    } while (FindNextFileA(search, &found));
    FindClose(search);
}

static int load_system_dinput8(void)
{
    if (system_create) return 1;
    char path[MAX_PATH];
    UINT length = GetSystemDirectoryA(path, MAX_PATH);
    if (length == 0 || length + 13 >= MAX_PATH) return 0;
    strcat_s(path, MAX_PATH, "\\dinput8.dll");
    system_dinput8 = LoadLibraryA(path);
    if (!system_dinput8) return 0;
    system_create = (DirectInput8CreateFn)GetProcAddress(system_dinput8, "DirectInput8Create");
    return system_create != NULL;
}

__declspec(dllexport) HRESULT WINAPI DirectInput8Create(HINSTANCE instance, DWORD version, REFIID id, LPVOID *out,
    LPUNKNOWN outer)
{
    if (!load_system_dinput8()) return E_FAIL;
    return system_create(instance, version, id, out, outer);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        load_native_mods();
    }
    return TRUE;
}
