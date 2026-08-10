#include <process.h>
#include <wchar.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>

#include "DuckDBExcelAddin.h"
#include "db_lib_loader.h"
#include "db_fetch.h"
#include "db_xlrange.h"

#define XLTYPEMASK              0x0fff

#define ERR_MSG_MAX_LEN         512

#define ERR_MSG_INTERNAL        "Error: Internal error"
#define ERR_MSG_PARAM_PARSING   "Error: Invalid parameters"
#define ERR_MSG_PARAM_BIND      "Error: Parameter binding failed"
#define ERR_MSG_DUCKDB_INIT     "Error: DuckDB initialization failed"
#define ERR_MSG_UNICODE         "Error: Text conversion failed"
#define ERR_MSG_STMT_EMPTY      "Error: Empty statement"
#define ERR_MSG_STMT_PREPARE    "Error: Statement preparation failed"
#define ERR_MSG_STMT_EXEC       "Error: Statement execution failed"
#define ERR_MSG_RESULT_FETCH    "Error: Result fetch failed"
#define ERR_MSG_TBL_FUNC_REG    "Error: xlrange() registration failed"
#define ERR_MSG_SPLIT_STMT      "Error: Statement parsing failed"
#define ERR_MSG_BIND_UNSUPPORT  "Error: QUERY mode does not support parameters"

// globals
static HMODULE g_duckdb_dll = NULL;
static atomic_size_t g_sample_count = XLRANGE_DEFAULT_SAMPLE_COUNT;

// async context to pass to worker
typedef struct {
    XLOPER12 asyncHandle;
    wchar_t *stmt;
    XLOPER12 *ranges;
    size_t nrange;
    XLOPER12 *params;
    size_t nparam;
    const char *err_msg;
    bool prepare_mode;
} async_context_t;

// exported function implementation

static void unreg_funcs(const LPXLOPER12 dllPath)
{
    XLOPER12 xRegId;

    #define UNREGISTER(xllname, xlname, params, helptext, category) \
    do { \
        if (Excel12f( \
            xlfRegisterId, \
            &xRegId, \
            2, \
            dllPath, \
            TempStr12(xlname) \
        ) == xlretSuccess) { \
            Excel12f(xlfUnregister, 0, 1, &xRegId); \
            Excel12f(xlFree, 0, 1, &xRegId); \
        } \
    } while (0);

    XLL_FUNCS(UNREGISTER)
    #undef UNREGISTER
}

static void xlUnload(void)
{
    XLOPER12 dllPath;
    if (Excel12f(xlGetName, &dllPath, 0) != xlretSuccess)
        return;
    
    unreg_funcs(&dllPath);

    Excel12f(xlFree, 0, 1, &dllPath);

    if (g_duckdb_dll)
    {
        FreeLibrary(g_duckdb_dll);
        g_duckdb_dll = NULL;
    }
}

int WINAPI xlAutoOpen(void)
{

    XLOPER12 dllPath;
    XLOPER12 handle;

    if (Excel12f(xlGetName, &dllPath, 0) !=xlretSuccess) 
        return 0;
    if (Excel12f(xlGetHwnd, &handle, 0) !=xlretSuccess)
    {
        Excel12f(xlFree, 0, 1, &dllPath);        
        return 0;
    }
    HWND hwnd = (HWND)(INT_PTR)handle.val.w;
    Excel12f(xlFree, 0, 1, &handle); 

    int result = 0;

    g_duckdb_dll = load_duckdb(hwnd, dllPath.val.str, DUCKDB_DLL);
    if (!g_duckdb_dll) 
        goto cleanup;

    #define REGISTER(xllname, xlname, params, helptext, category) \
    do { \
        if(Excel12f(xlfRegister, 0, 7, \
            &dllPath, \
            TempStr12(TO_WSTR(xllname)), \
            TempStr12(params), \
            TempStr12(xlname), \
            TempStr12(helptext), \
            TempInt12(1), \
            TempStr12(category) \
        ) != xlretSuccess) \
            goto register_failure; \
    } while (0);

    XLL_FUNCS(REGISTER)
    #undef REGISTER

    // all success!
    result = 1;
    goto cleanup;

register_failure:
    show_error(hwnd, L"Fail to register worksheet functions");
    // rollback and free dll
    xlUnload();
    result = 0;

cleanup:
    Excel12f(xlFree, 0, 1, &dllPath); 
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
    free_xloper12(pxFree);
}

static int split_params(
    XLOPER12 **data_ranges,
    size_t *n_data_ranges,
    XLOPER12 **stmt_params,
    size_t *n_stmt_params,
    bool deep,
    PARAM_AND_TYPE_LIST
) {
    if (!stmt_params || !data_ranges || !n_stmt_params || !n_data_ranges)
        return 0;
    *data_ranges = *stmt_params = NULL;
    *n_data_ranges = *n_stmt_params = 0;

    LPXLOPER12 src[] = { PARAM_LIST };
    size_t max_param = sizeof(src) / sizeof(src[0]);

    size_t nrange_tmp = 0;
    for (size_t i=0; i < max_param; i++)
    {
        LPXLOPER12 param = src[i];
        if (!param) return 0;       // reject NULL
        if ((param->xltype & XLTYPEMASK) == xltypeMulti)
            ++nrange_tmp;
        else
            break;
    }

    size_t total = 0;
    for (size_t i=nrange_tmp; i < max_param; i++)
    {
        LPXLOPER12 param = src[i];
        if (!param) return 0;       // reject NULL
        int type = (param->xltype & XLTYPEMASK);
        if (type == xltypeMulti) return 0;
        if (type != xltypeMissing) total = i + 1;
    }

    size_t nparam_tmp = (total > nrange_tmp)
        ? (total - nrange_tmp)
        : 0;

    LPXLOPER12 data_ranges_tmp = (nrange_tmp > 0)
        ? calloc(nrange_tmp, sizeof(*data_ranges_tmp))
        : NULL;
    LPXLOPER12 stmt_params_tmp = (nparam_tmp > 0)
        ? calloc(nparam_tmp, sizeof(*stmt_params_tmp))
        : NULL;

    if ((nrange_tmp && !data_ranges_tmp) || (nparam_tmp && !stmt_params_tmp))
    {
        free(data_ranges_tmp);
        free(stmt_params_tmp);
        return 0;
    }

    if (deep) {
        for (size_t i=0; i < nrange_tmp; i++)
        {
            if (xloper12_deep_copy(&data_ranges_tmp[i], src[i]) == 0) {
                free_xloper12_array(data_ranges_tmp, i);
                free(stmt_params_tmp);
                return 0;
            }
        }
        for (size_t i=0; i < nparam_tmp; i++)
        {
            if (xloper12_deep_copy(&stmt_params_tmp[i], src[nrange_tmp + i]) == 0)
            {
                free_xloper12_array(data_ranges_tmp, nrange_tmp);
                free_xloper12_array(stmt_params_tmp, i);
                return 0;
            }
        }
    } else {
        for (size_t i=0; i < nrange_tmp; i++) {
            data_ranges_tmp[i] = *src[i];
            data_ranges_tmp[i].xltype &= ~xlbitDLLFree;
        }
        for (size_t i=0; i < nparam_tmp; i++) 
        {
            stmt_params_tmp[i] = *src[nrange_tmp + i];
            stmt_params_tmp[i].xltype &= ~xlbitDLLFree;
        }
    }

    *stmt_params = stmt_params_tmp;
    *n_stmt_params = nparam_tmp;
    *data_ranges = data_ranges_tmp;
    *n_data_ranges = nrange_tmp;

    return 1;
}

static int bind_params_to_stmt(duckdb_prepared_statement stmt, const XLOPER12 *params, size_t nparam) 
{
    if (nparam > 0 && !params) return -1;
    size_t n_placeholder = (size_t)DUCKDB_NPARAMS(stmt);
    // check for params count mismatch
    if (nparam < n_placeholder) return -1;

    for (size_t i = 0; i < n_placeholder; i++) {
        const XLOPER12 *param = &params[i];
        idx_t p_idx =  (idx_t)i + 1;
        
        switch (param->xltype & XLTYPEMASK)
        {
            case xltypeNum:
                if(DUCKDB_BIND_DOUBLE(stmt, p_idx, param->val.num) != DuckDBSuccess)
                    return -1;
                break;
            case xltypeInt:
                if(DUCKDB_BIND_INT32(stmt, p_idx, param->val.w) != DuckDBSuccess)
                    return -1;
                break;
            case xltypeBool:
                if(DUCKDB_BIND_BOOLEAN(stmt, p_idx, param->val.xbool) != DuckDBSuccess)
                    return -1;
                break;
            case xltypeStr:
            {
                char *str = NULL;
                if(xlstr_2_utf8(&str, param->val.str, NULL) == 0) return -1;
                duckdb_state state = str
                    ? DUCKDB_BIND_VARCHAR(stmt, p_idx, str)
                    : DUCKDB_BIND_NULL(stmt, p_idx);
                free(str);  // remember to free temporary string
                if(state != DuckDBSuccess) return -1;
                break;
            }
            case xltypeMissing:
            case xltypeNil:
                if(DUCKDB_BIND_NULL(stmt, p_idx) != DuckDBSuccess)
                    return -1;
                break;
            case xltypeErr:
                switch (param->val.err)
                {
                    case xlerrNA:
                    case xlerrNull:
                        if(DUCKDB_BIND_NULL(stmt, p_idx) != DuckDBSuccess)
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
    return n_placeholder;
}

static LPXLOPER12 execute_statement_create_range(
    const wchar_t *stmts_str,
    XLOPER12 *data_ranges,
    size_t nrange,
    XLOPER12 *bind_params,
    size_t nparam,
    bool prepare_mode
) {
    // init to safe values
    char *stmts_str_utf8 = NULL;
    duckdb_database db = NULL;
    duckdb_connection con = NULL;
    duckdb_table_function tbl_func = NULL;
    chunk_list chunks = {0};
    idx_t n_stmts = 0;
    LPXLOPER12 result = NULL;
    char buf[ERR_MSG_MAX_LEN];
    buf[0] = '\0';

    if (!prepare_mode && nparam > 0) {
        result = make_string_cell(ERR_MSG_BIND_UNSUPPORT);
        goto cleanup;
    }
    if (is_null_or_whitespace_xlstr(stmts_str))
    {
        result = make_string_cell(ERR_MSG_STMT_EMPTY);
        goto cleanup;
    }
    if (xlstr_2_utf8(&stmts_str_utf8, stmts_str, NULL) == 0)
    {
        result = make_string_cell(ERR_MSG_UNICODE);
        goto cleanup;
    }
    if (DUCKDB_OPEN(NULL, &db) != DuckDBSuccess)
    {
        result = make_string_cell(ERR_MSG_DUCKDB_INIT);
        goto cleanup;
    }
    if (DUCKDB_CONNECT(db, &con) != DuckDBSuccess)
    {
        result = make_string_cell(ERR_MSG_DUCKDB_INIT);
        goto cleanup;
    }
    if (register_xlrange_func(
        con,
        data_ranges,
        nrange,
        atomic_load(&g_sample_count),
        &tbl_func
    ) == 0) {
        result = make_string_cell(ERR_MSG_TBL_FUNC_REG);
        goto cleanup;
    }

    // Use prepared statement
    if (prepare_mode)
    {
        duckdb_extracted_statements stmts = NULL;
        duckdb_prepared_statement *prep_stmts = NULL;
        n_stmts = DUCKDB_EXTRACT_STATEMENTS(con, stmts_str_utf8, &stmts);
        if(n_stmts == 0) {
            result = make_string_cell(
                stmts ? DUCKDB_EXTRACT_STATEMENTS_ERROR(stmts) : ERR_MSG_SPLIT_STMT
            );
            goto prepared_branch_cleanup;
        }
        prep_stmts = calloc((size_t)n_stmts, sizeof(*prep_stmts));
        if (!prep_stmts)
        {
            result = make_string_cell(ERR_MSG_STMT_PREPARE);
            goto prepared_branch_cleanup;
        }
        for (idx_t i = 0; i < n_stmts; i++)
        {
            duckdb_prepared_statement prep_stmt = NULL;
            if(DUCKDB_PREPARE_EXTRACTED_STATEMENT(con, stmts, i, &prep_stmt) != DuckDBSuccess)
            {
                result = make_string_cell(
                    prep_stmt ? DUCKDB_PREPARE_ERROR(prep_stmt) : ERR_MSG_STMT_PREPARE
                );
                goto prepared_branch_cleanup;
            }
            prep_stmts[i] = prep_stmt;
            int n_bind = bind_params_to_stmt(prep_stmt, bind_params, nparam);
            if (n_bind < 0)
            {
                result = make_string_cell(ERR_MSG_PARAM_BIND);
                goto prepared_branch_cleanup;
            }
            nparam -= (size_t)n_bind;
            bind_params += n_bind;
        }
        if (nparam != 0)
        {
            result = make_string_cell(ERR_MSG_PARAM_BIND);
            goto prepared_branch_cleanup;
        }
        for (idx_t i = 0; i < n_stmts; i++)
        {
            duckdb_prepared_statement prep_stmt = prep_stmts[i];
            duckdb_result qresult = {0};
            const char *msg;
            duckdb_state state = DUCKDB_EXECUTE_PREPARED(prep_stmt, &qresult);
            if (state != DuckDBSuccess)
            {
                msg = DUCKDB_RESULT_ERROR(&qresult);
                result = make_string_cell(msg ? msg : ERR_MSG_STMT_EXEC);
                goto prepared_branch_destroy_result;
            }
            if ((i + 1) == n_stmts) // last statement
            {
                if (fetch_chunks(&qresult, &chunks, buf, sizeof(buf)) == 0)
                {
                    result = make_string_cell((buf[0]) ? buf : ERR_MSG_RESULT_FETCH);
                    goto prepared_branch_destroy_result;
                }
                result = chunks_to_range(&chunks);
            }

        prepared_branch_destroy_result:
            DUCKDB_DESTROY_RESULT(&qresult);
            if (state != DuckDBSuccess) break;
        }
    prepared_branch_cleanup:
        if (prep_stmts)
        {
            for (idx_t i = 0; i < n_stmts; i++)
            {
                if (prep_stmts[i]) DUCKDB_DESTROY_PREPARE(&prep_stmts[i]);
            }
        }
        free(prep_stmts);
        if (stmts) DUCKDB_DESTROY_EXTRACTED(&stmts);
    }
    // direct query mode
    else 
    {
        duckdb_result qresult = {0};
        const char *msg = NULL;
        if (DUCKDB_QUERY(con, stmts_str_utf8, &qresult) != DuckDBSuccess)
        {
            msg = DUCKDB_RESULT_ERROR(&qresult);
            result = make_string_cell(msg ? msg : ERR_MSG_STMT_EXEC);
            goto query_branch_cleanup;
        }
        if (fetch_chunks(&qresult, &chunks, buf, sizeof(buf)) == 0)
        {
            result = make_string_cell((buf[0]) ? buf : ERR_MSG_RESULT_FETCH);
            goto query_branch_cleanup;
        }
        result = chunks_to_range(&chunks);

    query_branch_cleanup:
        DUCKDB_DESTROY_RESULT(&qresult);
    }

cleanup:
    free_and_reset_chunk_list(&chunks);
    if (tbl_func) DUCKDB_DESTROY_TABLE_FUNCTION(&tbl_func);
    free(stmts_str_utf8);
    if (con) DUCKDB_DISCONNECT(&con);
    if (db) DUCKDB_CLOSE(&db);

    if (!result) result = make_string_cell(ERR_MSG_INTERNAL);
    
    return result;
}

static unsigned WINAPI exec_worker(LPVOID lpParam)
{

    async_context_t *asynCtx = (async_context_t *)lpParam;
    if (!asynCtx) return 0;

    LPXLOPER12 result;
    if (asynCtx->err_msg)        
        result = make_string_cell(asynCtx->err_msg);
    else 
        result = execute_statement_create_range(
            asynCtx->stmt,
            asynCtx->ranges,
            asynCtx->nrange,
            asynCtx->params,
            asynCtx->nparam,
            asynCtx->prepare_mode
        );

    if (Excel12f(
        xlAsyncReturn,
        NULL,
        2,
        &asynCtx->asyncHandle,
        result
    ) != xlretSuccess) {
        free_xloper12(result);
    }

    free_xloper12_array(asynCtx->params, asynCtx->nparam);
    free_xloper12_array(asynCtx->ranges, asynCtx->nrange);
    free(asynCtx->stmt);
    free(asynCtx);

    return 0;
}

static void exec_async_base(
    LPXLOPER12 asyncHandle,
    const wchar_t *stmt,
    bool prepare_mode,
    PARAM_AND_TYPE_LIST
) {

    if (!asyncHandle || !stmt) return;

    async_context_t *asynCtx = calloc(1, sizeof(*asynCtx));
    if (!asynCtx) return;

    asynCtx->asyncHandle = *asyncHandle;

    size_t n = stmt[0];
    wchar_t *cpy = malloc((n + 1)*sizeof(*cpy));
    if (!cpy) {
        asynCtx->err_msg = ERR_MSG_INTERNAL;
        goto fire_thread;
    }

    wmemcpy(cpy, stmt, n + 1);
    asynCtx->stmt = cpy;

    XLOPER12 *params = NULL;
    XLOPER12 *ranges = NULL;
    size_t nparam = 0;
    size_t nrange = 0;
    if (split_params(
        &ranges,
        &nrange,
        &params,
        &nparam,
        true, //deep
        PARAM_LIST
    ) == 0) {
        asynCtx->err_msg = ERR_MSG_PARAM_PARSING;
        goto fire_thread;
    }

    asynCtx->params = params;
    asynCtx->nparam = nparam;
    asynCtx->ranges = ranges;
    asynCtx->nrange = nrange;
    asynCtx->prepare_mode = prepare_mode;

fire_thread:
    HANDLE thread = (HANDLE)_beginthreadex(
        NULL,
        0,
        exec_worker,
        asynCtx,
        0,
        NULL
    );

    if (thread) {
        CloseHandle(thread);
    } else {
        LPXLOPER12 err = make_string_cell(ERR_MSG_INTERNAL);
        Excel12f(
            xlAsyncReturn,
            NULL,
            2,
            &asynCtx->asyncHandle,
            err
        );
        free_xloper12_array(asynCtx->params, asynCtx->nparam);
        free_xloper12_array(asynCtx->ranges, asynCtx->nrange);
        free(asynCtx->stmt);
        free(asynCtx);
    }

    return;
}

static LPXLOPER12 exec_base(
    const wchar_t *stmt,
    bool prepare_mode,
    PARAM_AND_TYPE_LIST
) {

    XLOPER12 *params = NULL;
    XLOPER12 *ranges = NULL;
    size_t nparam = 0;
    size_t nrange = 0;
    if (split_params(
        &ranges,
        &nrange,
        &params,
        &nparam,
        false, //shallow
        PARAM_LIST
    ) == 0)
        return make_string_cell(ERR_MSG_PARAM_PARSING);

    LPXLOPER12 result = execute_statement_create_range(
        stmt,
        ranges,
        nrange,
        params,
        nparam,
        prepare_mode
    );
    
    free(ranges);
    free(params);

    return result;
}

unsigned short WINAPI set_sample_size(unsigned short nsample)
{
    atomic_store(&g_sample_count, (size_t)nsample);
    return (unsigned short)nsample;
}

LPXLOPER12 WINAPI exec(
    const wchar_t *stmt,
    PARAM_AND_TYPE_LIST
) {
    return exec_base(
        stmt,
        true,
        PARAM_LIST
    );
}

void WINAPI exec_async(
    LPXLOPER12 asyncHandle,
    const wchar_t *stmt,
    PARAM_AND_TYPE_LIST
) {
    exec_async_base(
        asyncHandle,
        stmt,
        true,
        PARAM_LIST
    );
}

LPXLOPER12 WINAPI query(
    const wchar_t *stmt,
    PARAM_AND_TYPE_LIST
) {
    return exec_base(
        stmt,
        false,
        PARAM_LIST
    );
}

void WINAPI query_async(
    LPXLOPER12 asyncHandle,
    const wchar_t *stmt,
    PARAM_AND_TYPE_LIST
) {
    exec_async_base(
        asyncHandle,
        stmt,
        false,
        PARAM_LIST
    );
}
