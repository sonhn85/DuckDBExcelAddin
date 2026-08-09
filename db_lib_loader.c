#include <wchar.h>      // wmemcpy
#include <pathcch.h>    // PathCchRemoveFileSpec, PathCchCombineEx

#include "db_lib_loader.h"
#include "helper.h"

#define MSG_MAX_LENGTH      512

#define DEFINE_FUNC(duckdb_name, func) TO_DUCKDB_FUNC_TYPE(duckdb_name) func = NULL;
DUCKDB_FUNCS(DEFINE_FUNC)
#undef DEFINE_FUNC

static int load_duckdb_funcs(const HWND hwnd, const HMODULE dll)
{
    if (!dll) goto reset;

    const char* func_name = NULL;

    #define RESET_FUNC(ignore, func) do { func = NULL; } while (0);
    #define LOAD_FUNC(duckdb_name, func) \
    do { \
        func_name = TO_STR(duckdb_name); \
        func = (TO_DUCKDB_FUNC_TYPE(duckdb_name))GetProcAddress(dll, func_name); \
        if (!func) goto check_version; \
    } while (0);

    DUCKDB_FUNCS(LOAD_FUNC)

    // all ok!
    return 1;

check_version:
    if (!hwnd) goto reset;

    wchar_t msg[MSG_MAX_LENGTH];

    if (DUCKDB_LIBRARY_VERSION)
    {
        if (swprintf(
            msg,
            MSG_MAX_LENGTH,
            L"Unable to load function: %hs, please check duckdb version\nMinimum supported version: %hs\nLoaded library: %hs",
            func_name,
            DUCKDB_REQUIRED_VERSION,
            DUCKDB_LIBRARY_VERSION()
        ) >= 0)
            show_error(hwnd, msg);
        else
            goto fall_back;
    } else {
        goto fall_back;
    }
    goto reset;

fall_back:
    show_error(hwnd, L"Fail to load duckdb functions, please check duckdb version");

reset:
    // reset functions pointers
    DUCKDB_FUNCS(RESET_FUNC)

    #undef RESET_FUNC
    #undef LOAD_FUNC

    return 0;
}

HMODULE load_duckdb(const HWND hwnd, const wchar_t *caller_path, const wchar_t *dllname)
{
    if (!caller_path || !dllname) return NULL;

    int n = (unsigned short)caller_path[0];     // length is store at 0
    if ((n == 0) || (n >= MAX_PATH)) goto fail;

    // copy path
    wchar_t folder_path[MAX_PATH];
    wmemcpy(folder_path, caller_path + 1, n);
    folder_path[n] = L'\0';
    // remove file spec to get folder path
    HRESULT hr = PathCchRemoveFileSpec(folder_path, MAX_PATH);
    if (FAILED(hr)) goto fail;

    // get full path to dll
    wchar_t dll_path[MAX_PATH];
    hr = PathCchCombineEx(
        dll_path,
        MAX_PATH,
        folder_path,
        dllname,
        PATHCCH_NONE
    );
    if (FAILED(hr)) goto fail;

    // now load dll
    HMODULE dll = LoadLibraryExW(
        dll_path,
        NULL,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
    );
    if (!dll)
        goto fail;

    if (!load_duckdb_funcs(hwnd, dll))
    {
        FreeLibrary(dll);
        return NULL;
    }
    return dll;

fail:
    if (hwnd)
        show_error(hwnd, L"Fail to load duckdb");
    return NULL;
}