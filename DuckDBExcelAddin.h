#ifndef DUCKDB_EXCEL_ADDIN_H
#define DUCKDB_EXCEL_ADDIN_H

#include <windows.h>
#include "XLCALL.H"
#include "FRAMEWRK.H"

#include "helper.h"

#define DUCKDB_DLL                      L"duckdb.dll"
#define XLRANGE_DEFAULT_SAMPLE_COUNT    30

// worksheet functions have 30 params by default
// edit to add more
#define PARAMS(X, Y) X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9) X(10) X(11) X(12) X(13) X(14) X(15) X(16) X(17) X(18) X(19) X(20)  X(21) X(22) X(23) X(24) X(25) X(26) X(27) X(28) X(29) Y(30)

// do not change
#define PARAMX(x) param##x
#define PARAMX_SEP(x) param##x,
#define TYPE_SYMBOL(x) L"Q"
#define PARAM_AND_TYPE(x) LPXLOPER12 PARAMX(x)
#define PARAM_AND_TYPE_SEP(x) LPXLOPER12 PARAMX(x),
#define PARAM_HELP(x) L"param"
#define PARAM_HELP_SEP(x) L"param,"
#define PARAM_LIST PARAMS(PARAMX_SEP, PARAMX)
#define PARAMS_STR PARAMS(TYPE_SYMBOL, TYPE_SYMBOL)
#define PARAM_AND_TYPE_LIST PARAMS(PARAM_AND_TYPE_SEP, PARAM_AND_TYPE)
#define HELP_TEXT L"statements," PARAMS(PARAM_HELP_SEP, PARAM_HELP)
#define TYPE_STR(X, Y) X PARAMS_STR Y

// list of worksheet functions
// remark: func name in dll, worksheet function name, type string, help text, function category
#define FUNCTION_CATEGORY   L"DuckDB"
#define XLL_FUNCS(X) \
X(exec,             L"DUCKDB.EXEC",          TYPE_STR(L"QD%", L"$"),   HELP_TEXT,       FUNCTION_CATEGORY) \
X(exec_async,       L"DUCKDB.EXEC.ASYNC",    TYPE_STR(L">XD%", L"$"),  HELP_TEXT,       FUNCTION_CATEGORY) \
X(query,            L"DUCKDB.QUERY",         TYPE_STR(L"QD%", L"$"),   HELP_TEXT,       FUNCTION_CATEGORY) \
X(query_async,      L"DUCKDB.QUERY.ASYNC",   TYPE_STR(L">XD%", L"$"),  HELP_TEXT,       FUNCTION_CATEGORY) \
X(set_sample_size,  L"DUCKDB.SETSAMPLE",     L"HH",                    L"sample_count", FUNCTION_CATEGORY)

#ifdef __cplusplus
extern "C" {
#endif

#define DLLEXPORT __declspec(dllexport)

DLLEXPORT int WINAPI xlAutoOpen(void);
DLLEXPORT int WINAPI xlAutoClose(void);
DLLEXPORT int WINAPI xlAutoRemove(void);
DLLEXPORT void WINAPI xlAutoFree12(LPXLOPER12 pxFree);

DLLEXPORT LPXLOPER12 WINAPI exec(
    const wchar_t *stmt,
    PARAM_AND_TYPE_LIST
);

DLLEXPORT void WINAPI exec_async(
    LPXLOPER12 asyncHandle,
    const wchar_t *stmt,
    PARAM_AND_TYPE_LIST
);

DLLEXPORT LPXLOPER12 WINAPI query(
    const wchar_t *stmt,
    PARAM_AND_TYPE_LIST
);

DLLEXPORT void WINAPI query_async(
    LPXLOPER12 asyncHandle,
    const wchar_t *stmt,
    PARAM_AND_TYPE_LIST
);

DLLEXPORT unsigned short set_sample_size(unsigned short nsample);

#undef DLLEXPORT

#ifdef __cplusplus
}
#endif

#endif // DUCKDB_EXCEL_ADDIN_H
