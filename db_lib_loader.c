#include <wchar.h>
#include <pathcch.h>

#include "db_lib_loader.h"
#include "helper.h"
#include "config.h"

#define DEFINE_DUCKDB_FUNCTION_POINTER(duckdb_name, func) TO_DUCKDB_FUNCTION_TYPE(duckdb_name) func = NULL;
DUCKDB_FUNCTION_POINTERS(DEFINE_DUCKDB_FUNCTION_POINTER)

/* Resolve all required APIs. On failure, report a version mismatch. */
static int load_DUCKDB_FUNCTION_POINTERS(const HWND hwnd, const HMODULE dll)
{
    if (!dll)
        goto reset;

    const char* func_name = NULL;

    #define RESET_FUNC(ignore, func) do { func = NULL; } while (0);
    #define LOAD_FUNC(duckdb_name, func) \
    do { \
        func_name = TO_STR(duckdb_name); \
        func = (TO_DUCKDB_FUNCTION_TYPE(duckdb_name))GetProcAddress(dll, func_name); \
        if (!func) \
            goto check_version; \
    } while (0);

    DUCKDB_FUNCTION_POINTERS(LOAD_FUNC)

    return 1;

check_version:

    if (!hwnd)
        goto reset;

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
            goto generic_error;
    }
    else
    {
        goto generic_error;
    }

    goto reset;

generic_error:

    show_error(hwnd, L"Fail to load duckdb functions, please check duckdb version");

reset:

    DUCKDB_FUNCTION_POINTERS(RESET_FUNC)

    return 0;
}

HMODULE load_duckdb(const HWND hwnd, const wchar_t *caller_path, const wchar_t *dllname)
{
    if (!caller_path || !dllname)
        return NULL;

    int path_len = (unsigned short)caller_path[0];

    if ((path_len == 0) || (path_len >= MAX_PATH))
        goto fail;

    // Copy caller workbook path from Excel string format
    wchar_t folder_path[MAX_PATH];

    wmemcpy(folder_path, caller_path + 1, path_len);

    folder_path[path_len] = L'\0';

    // Extract containing directory from workbook path
    HRESULT hr = PathCchRemoveFileSpec(folder_path, MAX_PATH);

    if (FAILED(hr))
        goto fail;

    // Build absolute DLL path relative to the workbook directory
    wchar_t dll_path[MAX_PATH];

    hr = PathCchCombineEx(
        dll_path,
        MAX_PATH,
        folder_path,
        dllname,
        PATHCCH_NONE
    );

    if (FAILED(hr))
        goto fail;

    // Load DuckDB and resolve dependencies using the DLL directory
    HMODULE dll = LoadLibraryExW(
        dll_path,
        NULL,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
    );

    if (!dll)
        goto fail;

    if (!load_DUCKDB_FUNCTION_POINTERS(hwnd, dll))
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
