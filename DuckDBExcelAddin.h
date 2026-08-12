#ifndef DUCKDB_EXCEL_ADDIN_H
#define DUCKDB_EXCEL_ADDIN_H

#include <windows.h>
#include "XLCALL.H"
#include "FRAMEWRK.H"

#include "helper.h"
#include "config.h"

/* Generate parameter names, declarations, help text,
   and Excel type strings for worksheet functions. */
#define WORKSHEET_PARAMX(x) param##x
#define WORKSHEET_PARAMX_SEP(x) param##x,
#define TYPE_SYMBOL(x) L"Q"
#define WORKSHEET_PARAM_AND_TYPE(x) LPXLOPER12 WORKSHEET_PARAMX(x)
#define WORKSHEET_PARAM_AND_TYPE_SEP(x) LPXLOPER12 WORKSHEET_PARAMX(x),
#define WORKSHEET_PARAM_HELP(x) L"param"
#define WORKSHEET_PARAM_HELP_SEP(x) L"param,"
#define WORKSHEET_PARAM_LIST WORKSHEET_PARAMS(WORKSHEET_PARAMX_SEP, WORKSHEET_PARAMX)
#define WORKSHEET_PARAM_STR WORKSHEET_PARAMS(TYPE_SYMBOL, TYPE_SYMBOL)
#define WORKSHEET_PARAM_AND_TYPE_LIST WORKSHEET_PARAMS(WORKSHEET_PARAM_AND_TYPE_SEP, WORKSHEET_PARAM_AND_TYPE)
#define HELP_TEXT L"statements," WORKSHEET_PARAMS(WORKSHEET_PARAM_HELP_SEP, WORKSHEET_PARAM_HELP)
#define TYPE_STR(X, Y) X WORKSHEET_PARAM_STR Y

/* Registered worksheet functions.
   Arguments:
     DLL function name,
     Excel function name,
     type string,
     help text,
     category. */

#define XLL_FUNCS(X) \
X(exec_sync,        L"DUCKDB.EXEC",          TYPE_STR(L"QD%", L"$"),   HELP_TEXT,       FUNCTION_CATEGORY) \
X(exec_async,       L"DUCKDB.EXEC.ASYNC",    TYPE_STR(L">XD%", L"$"),  HELP_TEXT,       FUNCTION_CATEGORY) \
X(query_sync,       L"DUCKDB.QUERY",         TYPE_STR(L"QD%", L"$"),   HELP_TEXT,       FUNCTION_CATEGORY) \
X(query_async,      L"DUCKDB.QUERY.ASYNC",   TYPE_STR(L">XD%", L"$"),  HELP_TEXT,       FUNCTION_CATEGORY) \
X(addin_info,       L"DUCKDB.INFO",          L"Q",                     L"",             FUNCTION_CATEGORY)

#ifdef __cplusplus
extern "C" {
#endif

#define DLLEXPORT __declspec(dllexport)

DLLEXPORT int WINAPI xlAutoOpen(void);
DLLEXPORT int WINAPI xlAutoClose(void);
DLLEXPORT int WINAPI xlAutoRemove(void);
DLLEXPORT void WINAPI xlAutoFree12(LPXLOPER12 pxFree);

DLLEXPORT LPXLOPER12 WINAPI exec_sync(
    const wchar_t *stmt,
    WORKSHEET_PARAM_AND_TYPE_LIST
);

DLLEXPORT void WINAPI exec_async(
    LPXLOPER12 asyncHandle,
    const wchar_t *stmt,
    WORKSHEET_PARAM_AND_TYPE_LIST
);

DLLEXPORT LPXLOPER12 WINAPI query_sync(
    const wchar_t *stmt,
    WORKSHEET_PARAM_AND_TYPE_LIST
);

DLLEXPORT void WINAPI query_async(
    LPXLOPER12 asyncHandle,
    const wchar_t *stmt,
    WORKSHEET_PARAM_AND_TYPE_LIST
);

DLLEXPORT LPXLOPER12 addin_info(void);

#undef DLLEXPORT

#ifdef __cplusplus
}
#endif

#endif // DUCKDB_EXCEL_ADDIN_H
