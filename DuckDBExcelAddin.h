#ifndef DUCKDB_EXCEL_ADDIN_H
#define DUCKDB_EXCEL_ADDIN_H

#include <windows.h>
#include "XLCALL.H"
#include "FRAMEWRK.H"

#include "helper.h"
#include "config.h"

/*
 * Helper macros used to generate worksheet function parameter lists,
 * registration type strings, and help text from WORKSHEET_PARAMS.
 *
 * These macros ensure that worksheet function signatures and Excel
 * registration metadata remain consistent.
 */
#define WORKSHEET_PARAMX(X) param##X
#define WORKSHEET_PARAMX_SEPARATED(X) param##X,
/* Registration type code for an LPXLOPER12 argument. */
#define TYPE_SYMBOL(X) L"Q"
#define WORKSHEET_PARAM_AND_TYPE(X) LPXLOPER12 WORKSHEET_PARAMX(X)
#define WORKSHEET_PARAM_AND_TYPE_SEPARATED(X) LPXLOPER12 WORKSHEET_PARAMX(X),
#define WORKSHEET_PARAM_HELP(X) L"param"
#define WORKSHEET_PARAM_HELP_SEPARATED(X) L"param,"
/* List of parameters (comma-separated) */
#define WORKSHEET_PARAM_LIST WORKSHEET_PARAMS(WORKSHEET_PARAMX_SEPARATED, WORKSHEET_PARAMX)
/* Type codes for parameters (not comma-separated) */
#define WORKSHEET_PARAM_STRING WORKSHEET_PARAMS(TYPE_SYMBOL, TYPE_SYMBOL)
/* Type and name pair of parameters (comma-separated) */
#define WORKSHEET_PARAM_AND_TYPE_LIST WORKSHEET_PARAMS(WORKSHEET_PARAM_AND_TYPE_SEPARATED, WORKSHEET_PARAM_AND_TYPE)
/* Comma-separated parameter help text. */
#define HELP_TEXT_NO_INIT L"statements," WORKSHEET_PARAMS(WORKSHEET_PARAM_HELP_SEPARATED, WORKSHEET_PARAM_HELP)
#define HELP_TEXT_WITH_INIT L"statements,statements," WORKSHEET_PARAMS(WORKSHEET_PARAM_HELP_SEPARATED, WORKSHEET_PARAM_HELP)
/* Complete Excel registration type string. */
#define TYPE_STRING(PREFIX, SUFFIX) PREFIX WORKSHEET_PARAM_STRING SUFFIX

/*
 * Worksheet function registration metadata.
 *
 * Each entry contains:
 *   (c_function,
 *    excel_function_name,
 *    registration_type_string,
 *    parameter_help_text,
 *    category)
 */
#define XLL_FUNCTIONS(X) \
X(exec_sync_no_init,    L"DUCKDB.EXEC",        TYPE_STRING(L"QD%", L"$"),    HELP_TEXT_NO_INIT,   FUNCTION_CATEGORY) \
X(exec_async_no_init,   L"DUCKDB.EXEC.ASYNC",  TYPE_STRING(L">XD%", L"$"),   HELP_TEXT_NO_INIT,   FUNCTION_CATEGORY) \
X(addin_info,           L"DUCKDB.INFO",        L"Q",                         L"",                 FUNCTION_CATEGORY) \
X(exec_sync_with_init,  L"DUCKDB.EXECX",       TYPE_STRING(L"QD%D%", L"$"),  HELP_TEXT_WITH_INIT, FUNCTION_CATEGORY) \
X(exec_async_with_init, L"DUCKDB.EXECX.ASYNC", TYPE_STRING(L">XD%D%", L"$"), HELP_TEXT_WITH_INIT, FUNCTION_CATEGORY)

#ifdef __cplusplus
extern "C" {
#endif

#define DLLEXPORT __declspec(dllexport)

/* Excel add-in initialization entry point. */
DLLEXPORT int WINAPI xlAutoOpen(void);

/* Excel add-in shutdown entry point. */
DLLEXPORT int WINAPI xlAutoClose(void);

/* Called when the add-in is removed from Excel. */
DLLEXPORT int WINAPI xlAutoRemove(void);

/* Release memory previously returned to Excel. */
DLLEXPORT void WINAPI xlAutoFree12(LPXLOPER12 pxFree);

/* Provide the Add-In Manager with information */
DLLEXPORT LPXLOPER12 WINAPI xlAddInManagerInfo12(LPXLOPER12 pxAction);

/*
 * Execute SQL statements synchronously
 *
 * Supports parameter binding.
 *
 * Returns the statement result as an XLOPER12 value.
 */
DLLEXPORT LPXLOPER12 WINAPI exec_sync_no_init(
    const wchar_t *sql,
    WORKSHEET_PARAM_AND_TYPE_LIST
);

/*
 * Execute SQL statements asynchronously
 *
 * Supports parameter binding.
 *
 * Results are delivered through Excel's asynchronous
 * worksheet function mechanism.
 */
DLLEXPORT void WINAPI exec_async_no_init(
    LPXLOPER12 asyncHandle,
    const wchar_t *sql,
    WORKSHEET_PARAM_AND_TYPE_LIST
);

/*
 * Retrieve information about the add-in version and
 * runtime environment.
 */
DLLEXPORT LPXLOPER12 addin_info(void);

/*
 * Execute SQL statements synchronously.
 *
 * An optional initialization SQL script may be executed
 * before the main statement.
 *
 * Supports parameter binding.
 *
 * Returns the statement result as an XLOPER12 value.
 */
DLLEXPORT LPXLOPER12 WINAPI exec_sync_with_init(
    const wchar_t *pre_sql,
    const wchar_t *sql,
    WORKSHEET_PARAM_AND_TYPE_LIST
);

/*
 * Execute SQL statements asynchronously.
 *
 * An optional initialization SQL script may be executed
 * before the main statement.
 *
 * Supports parameter binding.
 *
 * Results are delivered through Excel's asynchronous
 * worksheet function mechanism.
 */
DLLEXPORT void WINAPI exec_async_with_init(
    LPXLOPER12 asyncHandle,
    const wchar_t *pre_sql,
    const wchar_t *sql,
    WORKSHEET_PARAM_AND_TYPE_LIST
);

#undef DLLEXPORT

#ifdef __cplusplus
}
#endif

#endif /* DUCKDB_EXCEL_ADDIN_H */
