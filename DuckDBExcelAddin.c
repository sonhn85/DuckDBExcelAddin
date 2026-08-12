#include <process.h>
#include <wchar.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdio.h>

#include "DuckDBExcelAddin.h"
#include "db_lib_loader.h"
#include "db_fetch.h"
#include "db_xlrange.h"

#define ERR_MSG_INTERNAL                    "Error: Internal error"
#define ERR_MSG_INVALID_PARAM               "Error: Invalid parameters"
#define ERR_MSG_PARAM_BIND_FAILURE          "Error: Parameter binding failed"
#define ERR_MSG_DUCKDB_INIT_FAILURE         "Error: DuckDB initialization failed"
#define ERR_MSG_TEXT_CONVERSION_FAILURE     "Error: Text conversion failed"
#define ERR_MSG_STMT_EMPTY                  "Error: Empty statement"
#define ERR_MSG_STMT_PREPARE_FAILURE        "Error: Statement preparation failed"
#define ERR_MSG_STMT_EXEC_FAILURE           "Error: Statement execution failed"
#define ERR_MSG_RESULT_FETCH_FAILURE        "Error: Result fetch failed"
#define ERR_MSG_XLRANGE_REGISTER_FAILURE    "Error: xlrange() registration failed"
#define ERR_MSG_EXTRACT_STMTS_FAILURE       "Error: Statement parsing failed"
#define ERR_MSG_BIND_UNSUPPORTED            "Error: QUERY mode does not support parameters"
#define ERR_MSG_INFO_NA                     "Error: Failed to get add-in info"

static HMODULE g_duckdb_dll = NULL;

typedef struct {
    XLOPER12 asyncHandle;
    wchar_t *sql;
    XLOPER12 *ranges;
    size_t nranges;
    XLOPER12 *params;
    size_t nparams;
    const char *err_msg;
    bool prepared_mode;
} async_context_t;

/* Look up each registered function ID and unregister it. */
static void unreg_funcs(const LPXLOPER12 xllPath)
{
    XLOPER12 xRegId;

    #define UNREGISTER_FUNC(xllname, xlname, params, helptext, category) \
    do { \
        if (Excel12f( \
            xlfRegisterId, \
            &xRegId, \
            2, \
            xllPath, \
            TempStr12(xlname) \
        ) == xlretSuccess) { \
            Excel12f(xlfUnregister, 0, 1, &xRegId); \
            Excel12f(xlFree, 0, 1, &xRegId); \
        } \
    } while (0);

    XLL_FUNCS(UNREGISTER_FUNC)
    #undef UNREGISTER_FUNC
}

static void xlUnload(void)
{
    XLOPER12 xllPath;

    if (Excel12f(xlGetName, &xllPath, 0) != xlretSuccess)
        return;
    
    unreg_funcs(&xllPath);

    Excel12f(xlFree, 0, 1, &xllPath);

    if (g_duckdb_dll)
    {
        FreeLibrary(g_duckdb_dll);
        g_duckdb_dll = NULL;
    }
}

int WINAPI xlAutoOpen(void)
{

    XLOPER12 xllPath;
    XLOPER12 handle;

    if (Excel12f(xlGetName, &xllPath, 0) != xlretSuccess) 
        return 0;

    if (Excel12f(xlGetHwnd, &handle, 0) != xlretSuccess)
    {
        Excel12f(xlFree, 0, 1, &xllPath);        
        return 0;
    }

    HWND hwnd = (HWND)(INT_PTR)handle.val.w;

    Excel12f(xlFree, 0, 1, &handle); 

    int result = 0;

    // Load DuckDB from the add-in directory
    g_duckdb_dll = load_duckdb(hwnd, xllPath.val.str, DUCKDB_DLL);

    if (!g_duckdb_dll)
        goto cleanup;

    // Register all exported worksheet functions
    #define REGISTER_FUNC(xllname, xlname, params, helptext, category) \
    do { \
        if(Excel12f(xlfRegister, 0, 7, \
            &xllPath, \
            TempStr12(TO_WSTR(xllname)), \
            TempStr12(params), \
            TempStr12(xlname), \
            TempStr12(helptext), \
            TempInt12(1), \
            TempStr12(category) \
        ) != xlretSuccess) \
            goto register_failure; \
    } while (0);

    XLL_FUNCS(REGISTER_FUNC)
    #undef REGISTER_FUNC

    result = 1;
    goto cleanup;

register_failure:

    show_error(hwnd, L"Fail to register worksheet functions");

    xlUnload();

    result = 0;

cleanup:

    Excel12f(xlFree, 0, 1, &xllPath); 

    return result;
}

int WINAPI xlAutoRemove(void)
{
    xlUnload();
    return 1;
}

int WINAPI xlAutoClose(void)
{
    xlUnload();
    return 1;
}

void WINAPI xlAutoFree12(LPXLOPER12 pxFree)
{
    if (!pxFree) return;
    xloper12_free(pxFree);
}

/*
 * Split worksheet arguments into:
 *   - leading xltypeMulti arguments (data ranges)
 *   - subsequent non-range arguments (statement parameters)
 *
 * Optionally deep-copy extracted values.
 */
static int split_worksheet_params(
    XLOPER12 **ranges,
    size_t *nranges,
    XLOPER12 **bind_params,
    size_t *nparams,
    bool deep_copy,
    WORKSHEET_PARAM_AND_TYPE_LIST
) {
    if (!bind_params || !ranges || !nparams || !nranges)
        return 0;

    *ranges = *bind_params = NULL;
    *nranges = *nparams = 0;

    LPXLOPER12 params[] = { WORKSHEET_PARAM_LIST };

    size_t max_params = sizeof(params) / sizeof(params[0]);

    size_t range_count = 0;

    for (size_t i=0; i < max_params; i++)
    {
        LPXLOPER12 param = params[i];

        if (!param)
            return 0;       // Reject NULL

        if (LPXLOPER12_TYPE(param) == xltypeMulti)
            ++range_count;
        else
            break;
    }

    size_t last_param = 0;

    for (size_t i=range_count; i < max_params; i++)
    {
        LPXLOPER12 param = params[i];

        if (!param) 
            return 0;       // Reject NULL

        int type = LPXLOPER12_TYPE(param);

        if (type == xltypeMulti)
            return 0;

        if (type != xltypeMissing)
            last_param = i + 1;
    }

    size_t param_count = (last_param > range_count)
        ? (last_param - range_count)
        : 0;

    LPXLOPER12 ranges_tmp = (range_count > 0)
        ? calloc(range_count, sizeof(*ranges_tmp))
        : NULL;

    LPXLOPER12 bind_params_tmp = (param_count > 0)
        ? calloc(param_count, sizeof(*bind_params_tmp))
        : NULL;

    if ((range_count && !ranges_tmp) || (param_count && !bind_params_tmp))
    {
        free(ranges_tmp);
        free(bind_params_tmp);
        return 0;
    }

    if (deep_copy)
    {
        for (size_t i=0; i < range_count; i++)
        {
            if (xloper12_deep_copy(&ranges_tmp[i], params[i]) == 0)
            {
                xloper12_free_array(ranges_tmp, i);
                free(bind_params_tmp);
                return 0;
            }
        }
        for (size_t i=0; i < param_count; i++)
        {
            if (xloper12_deep_copy(&bind_params_tmp[i], params[range_count + i]) == 0)
            {
                xloper12_free_array(ranges_tmp, range_count);
                xloper12_free_array(bind_params_tmp, i);
                return 0;
            }
        }
    }
    else
    {
        for (size_t i=0; i < range_count; i++)
        {
            ranges_tmp[i] = *params[i];
            ranges_tmp[i].xltype &= ~xlbitDLLFree;
        }
        for (size_t i=0; i < param_count; i++) 
        {
            bind_params_tmp[i] = *params[range_count + i];
            bind_params_tmp[i].xltype &= ~xlbitDLLFree;
        }
    }

    *bind_params = bind_params_tmp;
    *nparams = param_count;
    *ranges = ranges_tmp;
    *nranges = range_count;

    return 1;
}

/*
 * Bind parameters to a prepared statement.
 *
 * Returns the number of bound placeholders on success,
 * or -1 on failure.
 */
static int bind_params(
    duckdb_prepared_statement stmt,
    const XLOPER12 *params,
    size_t nparams) 
{
    if (nparams > 0 && !params)
        return -1;

    size_t nplaceholders = (size_t)DUCKDB_NPARAMS(stmt);

    if (nparams < nplaceholders)
        return -1;

    for (size_t i = 0; i < nplaceholders; i++)
    {
        const XLOPER12 *param = &params[i];

        idx_t param_idx =  (idx_t)i + 1;
        
        switch (LPXLOPER12_TYPE(param))
        {
            case xltypeNum:
                if (DUCKDB_BIND_DOUBLE(stmt, param_idx, param->val.num) != DuckDBSuccess)
                    return -1;

                break;

            case xltypeInt:
                if (DUCKDB_BIND_INT32(stmt, param_idx, param->val.w) != DuckDBSuccess)
                    return -1;

                break;

            case xltypeBool:
                if (DUCKDB_BIND_BOOLEAN(stmt, param_idx, param->val.xbool) != DuckDBSuccess)
                    return -1;

                break;

            case xltypeStr:
            {
                char *str = NULL;

                if (xlstr_to_utf8(&str, param->val.str, NULL) == 0)
                    return -1;

                duckdb_state state = str
                    ? DUCKDB_BIND_VARCHAR(stmt, param_idx, str)
                    : DUCKDB_BIND_NULL(stmt, param_idx);

                free(str);
                
                if(state != DuckDBSuccess)
                    return -1;

                break;
            }

            case xltypeMissing:
            case xltypeNil:
                if (DUCKDB_BIND_NULL(stmt, param_idx) != DuckDBSuccess)
                    return -1;

                break;

            case xltypeErr:

                switch (param->val.err)
                {
                    case xlerrNA:
                    case xlerrNull:
                        if(DUCKDB_BIND_NULL(stmt, param_idx) != DuckDBSuccess)
                            return -1;

                        break;

                    default:
                        return -1;
                }
                break;

            default:
                return -1;
        }
    }

    return nplaceholders;
}

/*
 * Execute one or more SQL statements and return the result
 * of the final statement as an Excel range.
 *
 * In prepare mode, worksheet parameters are bound to statement
 * placeholders and xlrange() table functions are registered.
 */
static LPXLOPER12 run_sql_create_range(
    const wchar_t *sql,
    XLOPER12 *ranges,
    size_t nranges,
    XLOPER12 *params,
    size_t nparams,
    bool prepared_mode
) {
    char *sql_utf8 = NULL;
    duckdb_database db = NULL;
    duckdb_connection con = NULL;
    duckdb_table_function tbl_func = NULL;
    chunk_list chunks = {0};
    idx_t stmt_count = 0;
    LPXLOPER12 result = NULL;

    char errmsg[ERR_MSG_MAX_LEN];
    errmsg[0] = '\0';

    if (!prepared_mode && nparams > 0)
    {
        result = make_string_cell(ERR_MSG_BIND_UNSUPPORTED);
        goto cleanup;
    }

    if (is_null_or_whitespace_xlstr(sql))
    {
        result = make_string_cell(ERR_MSG_STMT_EMPTY);
        goto cleanup;
    }

    if (xlstr_to_utf8(&sql_utf8, sql, NULL) == 0)
    {
        result = make_string_cell(ERR_MSG_TEXT_CONVERSION_FAILURE);
        goto cleanup;
    }

    if (DUCKDB_OPEN(NULL, &db) != DuckDBSuccess)
    {
        result = make_string_cell(ERR_MSG_DUCKDB_INIT_FAILURE);
        goto cleanup;
    }

    if (DUCKDB_CONNECT(db, &con) != DuckDBSuccess)
    {
        result = make_string_cell(ERR_MSG_DUCKDB_INIT_FAILURE);
        goto cleanup;
    }

    if (register_xlrange_func(
        con,
        ranges,
        nranges,
        &tbl_func
    ) == 0)
    {
        result = make_string_cell(ERR_MSG_XLRANGE_REGISTER_FAILURE);
        goto cleanup;
    }

    if (prepared_mode)
    {
        duckdb_extracted_statements extracted_stmts = NULL;

        duckdb_prepared_statement *prepared_stmts = NULL;

        stmt_count = DUCKDB_EXTRACT_STATEMENTS(con, sql_utf8, &extracted_stmts);

        if(stmt_count == 0)
        {
            result = make_string_cell(
                extracted_stmts 
                    ? DUCKDB_EXTRACT_STATEMENTS_ERROR(extracted_stmts)
                    : ERR_MSG_EXTRACT_STMTS_FAILURE
            );
            goto prepared_branch_cleanup;
        }

        prepared_stmts = calloc((size_t)stmt_count, sizeof(*prepared_stmts));

        if (!prepared_stmts)
        {
            result = make_string_cell(ERR_MSG_STMT_PREPARE_FAILURE);
            goto prepared_branch_cleanup;
        }

        for (idx_t i = 0; i < stmt_count; i++)
        {
            duckdb_prepared_statement prep_stmt = NULL;

            if(DUCKDB_PREPARE_EXTRACTED_STATEMENT(con, extracted_stmts, i, &prep_stmt) != DuckDBSuccess)
            {
                result = make_string_cell(
                    prep_stmt
                        ? DUCKDB_PREPARE_ERROR(prep_stmt)
                        : ERR_MSG_STMT_PREPARE_FAILURE
                );
                goto prepared_branch_cleanup;
            }

            prepared_stmts[i] = prep_stmt;

            int n_bind = bind_params(prep_stmt, params, nparams);

            if (n_bind < 0)
            {
                result = make_string_cell(ERR_MSG_PARAM_BIND_FAILURE);
                goto prepared_branch_cleanup;
            }

            nparams -= (size_t)n_bind;

            params += n_bind;
        }

        if (nparams != 0)
        {
            result = make_string_cell(ERR_MSG_PARAM_BIND_FAILURE);
            goto prepared_branch_cleanup;
        }

        for (idx_t i = 0; i < stmt_count; i++)
        {
            duckdb_prepared_statement prep_stmt = prepared_stmts[i];

            duckdb_result qresult = {0};

            const char *msg;

            duckdb_state state = DUCKDB_EXECUTE_PREPARED(prep_stmt, &qresult);

            if (state != DuckDBSuccess)
            {
                msg = DUCKDB_RESULT_ERROR(&qresult);
                result = make_string_cell(
                    msg ? msg : ERR_MSG_STMT_EXEC_FAILURE
                );
                goto prepared_branch_destroy_result;
            }

            // Only return the result of the final statement
            if ((i + 1) == stmt_count)
            {
                if (fetch_chunks(&qresult, &chunks, errmsg, sizeof(errmsg)) == 0)
                {
                    result = make_string_cell(
                        errmsg[0] ? errmsg : ERR_MSG_RESULT_FETCH_FAILURE
                    );
                    goto prepared_branch_destroy_result;
                }

                result = chunks_to_range(&chunks);
            }

        prepared_branch_destroy_result:

            DUCKDB_DESTROY_RESULT(&qresult);

            if (state != DuckDBSuccess) break;
        }

    prepared_branch_cleanup:

        if (prepared_stmts)
        {
            for (idx_t i = 0; i < stmt_count; i++)
            {
                if (prepared_stmts[i]) DUCKDB_DESTROY_PREPARE(&prepared_stmts[i]);
            }

            free(prepared_stmts);
        }

        if (extracted_stmts) DUCKDB_DESTROY_EXTRACTED(&extracted_stmts);
    }
    else 
    {
        duckdb_result qresult = {0};

        const char *msg = NULL;

        if (DUCKDB_QUERY(con, sql_utf8, &qresult) != DuckDBSuccess)
        {
            msg = DUCKDB_RESULT_ERROR(&qresult);
            result = make_string_cell(
                msg ? msg : ERR_MSG_STMT_EXEC_FAILURE
            );
            goto query_branch_cleanup;
        }

        if (fetch_chunks(&qresult, &chunks, errmsg, sizeof(errmsg)) == 0)
        {
            result = make_string_cell(
                errmsg[0] ? errmsg : ERR_MSG_RESULT_FETCH_FAILURE
            );
            goto query_branch_cleanup;
        }

        result = chunks_to_range(&chunks);

    query_branch_cleanup:

        DUCKDB_DESTROY_RESULT(&qresult);
    }

cleanup:

    free_and_reset_chunk_list(&chunks);

    if (tbl_func) DUCKDB_DESTROY_TABLE_FUNCTION(&tbl_func);

    free(sql_utf8);

    if (con) DUCKDB_DISCONNECT(&con);
    
    if (db) DUCKDB_CLOSE(&db);

    if (!result) result = make_string_cell(ERR_MSG_INTERNAL);
    
    return result;
}

/*
 * Background worker for asynchronous worksheet functions.
 *
 * Executes the request, returns the result through Excel's
 * asynchronous callback, and releases all owned resources.
 */
static unsigned WINAPI run_sql_worker(LPVOID lpParam)
{

    async_context_t *ctx = (async_context_t *)lpParam;

    if (!ctx)
        return 0;

    LPXLOPER12 xl_result;

    if (ctx->err_msg)        
        xl_result = make_string_cell(ctx->err_msg);
    else 
        xl_result = run_sql_create_range(
            ctx->sql,
            ctx->ranges,
            ctx->nranges,
            ctx->params,
            ctx->nparams,
            ctx->prepared_mode
        );

    if (Excel12f(
        xlAsyncReturn,
        NULL,
        2,
        &ctx->asyncHandle,
        xl_result
    ) != xlretSuccess)
    {
        xloper12_free(xl_result);
    }

    xloper12_free_array(ctx->params, ctx->nparams);
    xloper12_free_array(ctx->ranges, ctx->nranges);
    free(ctx->sql);
    free(ctx);

    return 0;
}

/*
 * Create an asynchronous execution context and
 * queue execution on a worker thread.
 */
static void run_sql_async(
    LPXLOPER12 asyncHandle,
    const wchar_t *sql,
    bool prepared_mode,
    WORKSHEET_PARAM_AND_TYPE_LIST
) {

    if (!asyncHandle || !sql)
        return;

    async_context_t *ctx = calloc(1, sizeof(*ctx));

    if (!ctx)
        return;

    ctx->asyncHandle = *asyncHandle;

    size_t sql_len = sql[0];

    wchar_t *sql_copy = malloc((sql_len + 1)*sizeof(*sql_copy));

    if (!sql_copy)
    {
        ctx->err_msg = ERR_MSG_INTERNAL;
        goto fire_thread;
    }

    wmemcpy(sql_copy, sql, sql_len + 1);
    ctx->sql = sql_copy;

    XLOPER12 *params = NULL;
    XLOPER12 *ranges = NULL;
    size_t nparams = 0;
    size_t nranges = 0;

    if (split_worksheet_params(
        &ranges,
        &nranges,
        &params,
        &nparams,
        true,
        WORKSHEET_PARAM_LIST
    ) == 0)
    {
        ctx->err_msg = ERR_MSG_INVALID_PARAM;
        goto fire_thread;
    }

    ctx->params = params;
    ctx->nparams = nparams;
    ctx->ranges = ranges;
    ctx->nranges = nranges;
    ctx->prepared_mode = prepared_mode;

fire_thread:

    HANDLE thread = (HANDLE)_beginthreadex(
        NULL,
        0,
        run_sql_worker,
        ctx,
        0,
        NULL
    );

    if (thread)
    {
        CloseHandle(thread);
    }
    else 
    {
        LPXLOPER12 err = make_string_cell(ERR_MSG_INTERNAL);

        Excel12f(
            xlAsyncReturn,
            NULL,
            2,
            &ctx->asyncHandle,
            err
        );

        xloper12_free_array(ctx->params, ctx->nparams);
        xloper12_free_array(ctx->ranges, ctx->nranges);
        free(ctx->sql);
        free(ctx);
    }

    return;
}

static LPXLOPER12 run_sql_sync(
    const wchar_t *sql,
    bool prepared_mode,
    WORKSHEET_PARAM_AND_TYPE_LIST
)
{
    XLOPER12 *params = NULL;
    XLOPER12 *ranges = NULL;
    size_t nparams = 0;
    size_t nranges = 0;

    if (split_worksheet_params(
        &ranges,
        &nranges,
        &params,
        &nparams,
        false,
        WORKSHEET_PARAM_LIST
    ) == 0)
        return make_string_cell(ERR_MSG_INVALID_PARAM);

    LPXLOPER12 xl_result = run_sql_create_range(
        sql,
        ranges,
        nranges,
        params,
        nparams,
        prepared_mode
    );
    
    free(ranges);
    free(params);

    return xl_result;
}

LPXLOPER12 WINAPI exec_sync(
    const wchar_t *sql,
    WORKSHEET_PARAM_AND_TYPE_LIST
)
{
    return run_sql_sync(
        sql,
        true,
        WORKSHEET_PARAM_LIST
    );
}

void WINAPI exec_async(
    LPXLOPER12 asyncHandle,
    const wchar_t *sql,
    WORKSHEET_PARAM_AND_TYPE_LIST
)
{
    run_sql_async(
        asyncHandle,
        sql,
        true,
        WORKSHEET_PARAM_LIST
    );
}

LPXLOPER12 WINAPI query_sync(
    const wchar_t *sql,
    WORKSHEET_PARAM_AND_TYPE_LIST
)
{
    return run_sql_sync(
        sql,
        false,
        WORKSHEET_PARAM_LIST
    );
}

void WINAPI query_async(
    LPXLOPER12 asyncHandle,
    const wchar_t *sql,
    WORKSHEET_PARAM_AND_TYPE_LIST
)
{
    run_sql_async(
        asyncHandle,
        sql,
        false,
        WORKSHEET_PARAM_LIST
    );
}

/* Return add-in and loaded DuckDB version information. */
LPXLOPER12 addin_info(void)
{
    char buf[XLSTR_MAX_LEN];

    const char *duckdb_ver = "unknown";

    if (DUCKDB_LIBRARY_VERSION)
        duckdb_ver = DUCKDB_LIBRARY_VERSION();

    int n = snprintf(
        buf,
        sizeof(buf),
        "Add-in version: %s\n"
        "DuckDB version: %s\n",
        ADDIN_VERSION,
        duckdb_ver ? duckdb_ver : "unknown"
    );

    if (n < 0 || (size_t)n >= sizeof(buf))
        return make_string_cell(ERR_MSG_INFO_NA);

    return make_string_cell(buf);
}
