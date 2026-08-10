#include <stdio.h>      // snprintf
#include <errno.h>      // errno, ERANGE
#include <math.h>       // isfinite
#include <stdlib.h>     // malloc, free
#include <string.h>     // memcpy
#include <stdbool.h>    // bool
#include <stdint.h>     // int32_t

#include "db_xlrange.h"
#include "helper.h"     // xlstr_2_utf8
#include "db_lib_loader.h"

#define XLTYPEMASK                          0x0fff
#define ERR_MSG_XLRANGE_MAX_LEN             512
#define ERR_MSG_XLRANGE_INTERNAL            "Internal error"
#define ERR_MSG_XLRANGE_PARAM               "Invalid parameter" 
#define ERR_MSG_XLRANGE_INVALID_COL_NAME    "Invalid column name"
#define ERR_MSG_XLRANGE_EMPTY_COL_NAME      "Column name is empty"
#define ERR_MSG_XLRANGE_DOUBLE              "Failed to convert to DOUBLE"
#define ERR_MSG_XLRANGE_VARCHAR             "Failed to convert to VARCHAR"

// typedefs
typedef struct xlrange_context_t
{
    LPXLOPER12 ranges;
    size_t nrange;
    size_t nsample;
} xlrange_context_t;

typedef struct xlrange_bind_data_t
{
    LPXLOPER12 lparray; /* first data cell, not header */
    size_t ncols;
    size_t nrows;       /* data rows only */
    duckdb_type *types;
    char **colnames;
} xlrange_bind_data_t;

typedef struct xlrange_scan_state_t
{
    LPXLOPER12 lparray; /* first data cell, not header */
    size_t ncols;
    size_t nrows;
    size_t row_idx;
    duckdb_type *types;
    char **colnames;
} xlrange_scan_state_t;

static void format_err_msg(char *buf, size_t buf_size, const char *action, const char *colname, int64_t row, const char *msg)
{
    if (!buf || buf_size == 0 || !action || !msg) return;
    if (colname) {
        if (row >= 0) {
            snprintf(
                buf,
                buf_size,
                "Error %s: Column %s, row %zu: %s",
                action,
                colname,
                (size_t)(row + 2),
                msg
            );
        } else {
            snprintf(
                buf,
                buf_size,
                "Error %s: Column %s: %s",
                action,
                colname,
                msg
            );
        }
    } else {
        snprintf(
            buf,
            buf_size,
            "Error %s: %s",
            action,
            msg
        );
    }
}

static void free_bind_data(void *p)
{
    if (!p) return;
    xlrange_bind_data_t *bind_data = p;
    free(bind_data->types);
    if (bind_data->colnames)
    {
        for (size_t i = 0; i < bind_data->ncols; i++)
            free(bind_data->colnames[i]);

        free(bind_data->colnames);
    }
    free(bind_data);
}

static void xlrange_bind(duckdb_bind_info info)
{
    xlrange_context_t *ctx = DUCKDB_BIND_GET_EXTRA_INFO(info);

    duckdb_logical_type lt = NULL;
    duckdb_type *types = NULL;
    char **colnames = NULL;
    xlrange_bind_data_t *bind_data = NULL;
    char errmsg[ERR_MSG_XLRANGE_MAX_LEN];
    errmsg[0] = '\0';

    if (!ctx)
    {
        format_err_msg(errmsg, sizeof(errmsg), "binding xlrange", NULL, -1, ERR_MSG_XLRANGE_INTERNAL);
        goto fail;
    }
    if (DUCKDB_BIND_GET_PARAMETER_COUNT(info) != 1)
    {
        format_err_msg(errmsg, sizeof(errmsg), "binding xlrange", NULL, -1, ERR_MSG_XLRANGE_PARAM);
        goto fail;
    }
    duckdb_value val = DUCKDB_BIND_GET_PARAMETER(info, 0);      // Leak! duckdb_destroy_value(&val) crash
    if (!val)
    {
        format_err_msg(errmsg, sizeof(errmsg), "binding xlrange", NULL, -1, ERR_MSG_XLRANGE_INTERNAL);
        goto fail;
    }
    lt = DUCKDB_GET_VALUE_TYPE(val); 
    if (!lt)
    {
        format_err_msg(errmsg, sizeof(errmsg), "binding xlrange", NULL, -1, ERR_MSG_XLRANGE_INTERNAL);
        goto fail;
    }
    if (DUCKDB_GET_TYPE_ID(lt) != DUCKDB_TYPE_INTEGER)
    {
        format_err_msg(errmsg, sizeof(errmsg), "binding xlrange", NULL, -1, ERR_MSG_XLRANGE_PARAM);
        goto fail;
    }
    int32_t range_idx = DUCKDB_GET_INT32(val);
    if (range_idx <= 0 || (size_t)range_idx > ctx->nrange)
    {
        format_err_msg(errmsg, sizeof(errmsg), "binding xlrange", NULL, -1, ERR_MSG_XLRANGE_PARAM);
        goto fail;
    }
    LPXLOPER12 range = &ctx->ranges[range_idx - 1];
    if ((range->xltype & XLTYPEMASK) != xltypeMulti)
    {
        format_err_msg(errmsg, sizeof(errmsg), "binding xlrange", NULL, -1, ERR_MSG_XLRANGE_INTERNAL);
        goto fail;
    }
    size_t ncols = (size_t)range->val.array.columns;
    size_t nrows = (size_t)range->val.array.rows;
    LPXLOPER12 p = range->val.array.lparray;
    if (!p || ncols == 0 || nrows == 0)
    {
        format_err_msg(errmsg, sizeof(errmsg), "binding xlrange", NULL, -1, ERR_MSG_XLRANGE_INTERNAL);
        goto fail;
    }
    types = malloc(ncols*sizeof(*types));
    colnames = calloc(ncols, sizeof(*colnames));
    if (!types || !colnames)
    {
        free(types);
        free(colnames);
        format_err_msg(errmsg, sizeof(errmsg), "binding xlrange", NULL, -1, ERR_MSG_XLRANGE_INTERNAL);
        goto fail;
    }
    for (size_t i=0; i < ncols; i++, p++)
    {
        if (((p->xltype & XLTYPEMASK) != xltypeStr)
            || is_null_or_whitespace_xlstr(p->val.str)
        ) {
            format_err_msg(errmsg, sizeof(errmsg), "binding xlrange", NULL, -1, ERR_MSG_XLRANGE_EMPTY_COL_NAME);
            goto fail;
        }
        char *colname = NULL;
        if (xlstr_2_utf8(&colname, p->val.str, NULL) == 0)
        {
            format_err_msg(errmsg, sizeof(errmsg), "binding xlrange", NULL, -1, ERR_MSG_XLRANGE_INVALID_COL_NAME);
            free(colname);
            goto fail;
        }
        duckdb_type t = DUCKDB_TYPE_VARCHAR;
        if (nrows > 1)  // sampling data type, default to varchar
        {
            size_t ndatarows = nrows - 1;
            size_t nsample = ctx->nsample;
            if (nsample == 0 || nsample > ndatarows)
                nsample = ndatarows;
            LPXLOPER12 p1 = p + ncols;
            bool found = false;
            for (size_t r=0; r < nsample; r++, p1 += ncols)
            {
                switch (p1->xltype & XLTYPEMASK)
                {
                    case xltypeMissing:
                    case xltypeErr:
                    case xltypeNil:
                        continue;
                    case xltypeInt:
                    case xltypeNum:
                        t = DUCKDB_TYPE_DOUBLE;
                        found = true;
                        break;
                    case xltypeBool:
                        t = DUCKDB_TYPE_BOOLEAN;
                        found = true;
                        break;
                    case xltypeStr:
                    default:
                        t = DUCKDB_TYPE_VARCHAR;
                        found = true;
                        break;
                }
                if (found) break;
            }
        }
        duckdb_logical_type col_type = DUCKDB_CREATE_LOGICAL_TYPE(t);
        if (!col_type)
        {
            format_err_msg(errmsg, sizeof(errmsg), "binding xlrange", NULL, -1, ERR_MSG_XLRANGE_INTERNAL);
            free(colname);
            goto fail;
        }
        types[i] = t;
        colnames[i] = colname;
        DUCKDB_BIND_ADD_RESULT_COLUMN(info, colname, col_type);
        DUCKDB_DESTROY_LOGICAL_TYPE(&col_type);
    }
    bind_data = malloc(sizeof(*bind_data));
    if (!bind_data)
    {
        format_err_msg(errmsg, sizeof(errmsg), "binding xlrange", NULL, -1, ERR_MSG_XLRANGE_INTERNAL);
        goto fail;
    }
    bind_data->lparray = p;
    bind_data->ncols = (size_t)ncols;
    bind_data->nrows = (size_t)(nrows - 1);
    bind_data->types = types;
    bind_data->colnames = colnames;
    types = NULL;
    colnames = NULL;

    DUCKDB_BIND_SET_BIND_DATA(info, bind_data, free_bind_data);
    bind_data = NULL;
    DUCKDB_BIND_SET_CARDINALITY(info, (idx_t)(nrows - 1), true);

    goto cleanup;

fail:
    DUCKDB_BIND_SET_ERROR(info, (errmsg[0]) ? errmsg : ERR_MSG_XLRANGE_INTERNAL);
    free_bind_data(bind_data);
    free(types);
    if (colnames)
    {
        for (size_t i = 0; i < ncols; i++)
            free(colnames[i]);

        free(colnames);
    }

cleanup:
    if (lt) DUCKDB_DESTROY_LOGICAL_TYPE(&lt);
    //if (v) DUCKDB_DESTROY_VALUE(&v);
    return;
}

static void free_scan_state(void *p)
{
    if (!p) return;
    xlrange_scan_state_t *state = p;
    free(state->types);
    free(state->colnames);
    free(state);
}

static void xlrange_init(duckdb_init_info info)
{
    char errmsg[ERR_MSG_XLRANGE_MAX_LEN];
    errmsg[0] = '\0';
    xlrange_bind_data_t *bind_data = DUCKDB_INIT_GET_BIND_DATA(info);
    xlrange_scan_state_t *state = NULL;
    duckdb_type *types = NULL;
    char **colnames = NULL;
    size_t ncols = 0;

    if (!bind_data)
    {
        format_err_msg(errmsg, sizeof(errmsg), "initializing xlrange", NULL, -1, ERR_MSG_XLRANGE_INTERNAL);
        goto fail;
    }
    state = calloc(1, sizeof(*state));
    ncols = bind_data->ncols;
    types = malloc(ncols*sizeof(*types));
    colnames = malloc(ncols*sizeof(*colnames));
    if (!state || !types || !colnames)
    {
        format_err_msg(errmsg, sizeof(errmsg), "initializing xlrange", NULL, -1, ERR_MSG_XLRANGE_INTERNAL);
        goto fail;
    }
    memcpy(types, bind_data->types, ncols*sizeof(*types));
    memcpy(colnames, bind_data->colnames, ncols*sizeof(*colnames));

    state->lparray = bind_data->lparray;
    state->ncols = ncols;
    state->nrows = bind_data->nrows;
    state->row_idx = 0;
    state->types = types;
    types = NULL;
    state->colnames = colnames;
    colnames = NULL;
    DUCKDB_INIT_SET_INIT_DATA(info, state, free_scan_state);
    state = NULL;
    return;

fail:
    free(types);
    free(colnames);
    free_scan_state(state);
    DUCKDB_INIT_SET_ERROR(
        info,
        (errmsg[0]) ? errmsg : ERR_MSG_XLRANGE_INTERNAL
    );
    return;
}

static void xlrange_scan(duckdb_function_info info, duckdb_data_chunk output)
{
    char errmsg[ERR_MSG_XLRANGE_MAX_LEN];
    errmsg[0] = '\0';
    xlrange_scan_state_t *state = DUCKDB_FUNCTION_GET_INIT_DATA(info);
    if (!state) {
        format_err_msg(errmsg, sizeof(errmsg), "executing xlrange", NULL, -1, ERR_MSG_XLRANGE_INTERNAL);
        goto fail;
    }
    idx_t out_rows = 0;
    while (state->row_idx < state->nrows && out_rows < DUCKDB_VECTOR_SIZE())
    {
        for (size_t c = 0; c < state->ncols; c++)
        {
            duckdb_vector vec = DUCKDB_DATA_CHUNK_GET_VECTOR(output, c);
            size_t idx = state->row_idx * state->ncols + c;
            LPXLOPER12 cell = &state->lparray[idx];

            switch (state->types[c])
            {
                case DUCKDB_TYPE_DOUBLE:
                {
                    double *data = (double *)DUCKDB_VECTOR_GET_DATA(vec);
                    switch (cell->xltype & XLTYPEMASK)
                    {
                        case xltypeNum:
                            data[out_rows] = cell->val.num;
                            break;
                        case xltypeInt:
                            data[out_rows] = (double)cell->val.w;
                            break;
                        case xltypeBool:
                            data[out_rows] = (cell->val.xbool == true) ? 1.0 : 0.0;
                            break;
                        case xltypeStr:
                        {
                            wchar_t *src = cell->val.str;
                            char *dest = NULL;
                            if (!src) goto sqlnull;
                            if (xlstr_2_utf8(&dest, src, NULL) == 0 || !dest)
                            {
                                free(dest);
                                format_err_msg(errmsg, sizeof(errmsg), "executing xlrange", state->colnames[c], state->row_idx, ERR_MSG_XLRANGE_DOUBLE);
                                goto fail;
                            }
                            char *endptr;
                            errno = 0;
                            double num = strtod(dest, &endptr);
                            if (errno == ERANGE
                                || endptr == dest
                                || *endptr != '\0'
                                || !isfinite(num)
                            ) {
                                free(dest);
                                format_err_msg(errmsg, sizeof(errmsg), "executing xlrange", state->colnames[c], state->row_idx, ERR_MSG_XLRANGE_DOUBLE);
                                goto fail;
                            }
                            free(dest);
                            data[out_rows] = num;
                            break;
                        }
                        default:
                            goto sqlnull;
                    }
                    break;
                }
                case DUCKDB_TYPE_BOOLEAN:
                {
                    bool *data = (bool *)DUCKDB_VECTOR_GET_DATA(vec);
                    switch (cell->xltype & XLTYPEMASK) {
                        case xltypeBool:
                            data[out_rows] = cell->val.xbool ? true : false;
                            break;
                        case xltypeInt:
                            data[out_rows] = (cell->val.w != 0);
                            break;
                        case xltypeNum:
                            data[out_rows] = (cell->val.num != 0.0);
                            break;
                        default:
                            goto sqlnull;
                    }
                    break;
                }
                case DUCKDB_TYPE_VARCHAR:
                {
                    switch (cell->xltype & XLTYPEMASK) {
                        case xltypeNil:
                        case xltypeMissing:
                        case xltypeErr:
                            goto sqlnull;
                        case xltypeStr:
                        {
                            char *dest = NULL;
                            wchar_t *src = cell->val.str;
                            if (!src) goto sqlnull;
                            if (xlstr_2_utf8(&dest, src, NULL) == 0 || !dest)
                            {
                                free(dest);
                                format_err_msg(errmsg, sizeof(errmsg), "executing xlrange", state->colnames[c], state->row_idx, ERR_MSG_XLRANGE_VARCHAR);
                                goto fail;
                            }
                            DUCKDB_VECTOR_ASSIGN_STRING_ELEMENT(vec, out_rows, dest);
                            free(dest);
                            break;
                        }                       
                        case xltypeInt:
                        {
                            char buf[32];
                            snprintf(buf, sizeof(buf), "%d", (int)cell->val.w);
                            DUCKDB_VECTOR_ASSIGN_STRING_ELEMENT(vec, out_rows, buf);
                            break;
                        }
                        case xltypeNum:
                        {
                            char buf[64];
                            snprintf(buf, sizeof(buf), "%.15g", cell->val.num);
                            DUCKDB_VECTOR_ASSIGN_STRING_ELEMENT(vec, out_rows, buf);
                            break;
                        }
                        case xltypeBool:
                            DUCKDB_VECTOR_ASSIGN_STRING_ELEMENT(
                                vec,
                                out_rows,
                                cell->val.xbool ? "TRUE" : "FALSE"
                            );
                            break;
                        default:
                            goto sqlnull;
                    }
                    break;
                }
                default:
                    goto sqlnull;
            }
            continue;

        sqlnull:
            DUCKDB_VECTOR_ENSURE_VALIDITY_WRITABLE(vec);
            DUCKDB_VALIDITY_SET_ROW_INVALID(
                DUCKDB_VECTOR_GET_VALIDITY(vec),
                out_rows
            );
        }
        state->row_idx++;
        out_rows++;
    }
    DUCKDB_DATA_CHUNK_SET_SIZE(output, out_rows);
    return;

fail:
    DUCKDB_FUNCTION_SET_ERROR(info, (errmsg[0]) ? errmsg : ERR_MSG_XLRANGE_INTERNAL);
    DUCKDB_DATA_CHUNK_SET_SIZE(output, 0);
    return;
}

int register_xlrange_func
(
    duckdb_connection con,
    LPXLOPER12 ranges,
    size_t nrange,
    size_t nsample,
    duckdb_table_function *func
) 
{
    if (!func || (!ranges && nrange > 0)) return 0;

    int res = 0;
    *func = NULL;
    duckdb_table_function tbl_func = NULL;
    duckdb_logical_type param_type = NULL;

    tbl_func = DUCKDB_CREATE_TABLE_FUNCTION();
    if (!tbl_func) goto fail;

    xlrange_context_t *func_ctx = malloc(sizeof(*func_ctx));
    if (!func_ctx) goto fail;
    func_ctx->ranges = ranges;
    func_ctx->nrange = nrange;
    func_ctx->nsample = nsample;
    DUCKDB_TABLE_FUNCTION_SET_EXTRA_INFO(tbl_func, func_ctx, free);

    param_type = DUCKDB_CREATE_LOGICAL_TYPE(DUCKDB_TYPE_INTEGER);
    if (!param_type) goto fail;

    DUCKDB_TABLE_FUNCTION_SET_NAME(tbl_func, "xlrange");
    DUCKDB_TABLE_FUNCTION_ADD_PARAMETER(tbl_func, param_type);
    DUCKDB_TABLE_FUNCTION_SET_BIND(tbl_func, xlrange_bind);
    DUCKDB_TABLE_FUNCTION_SET_INIT(tbl_func, xlrange_init);
    DUCKDB_TABLE_FUNCTION_SET_FUNCTION(tbl_func, xlrange_scan);

    if (DUCKDB_REGISTER_TABLE_FUNCTION(con, tbl_func) != DuckDBSuccess)
        goto fail;

    *func = tbl_func;
    res = 1;
    goto cleanup;

fail:
    if (tbl_func) DUCKDB_DESTROY_TABLE_FUNCTION(&tbl_func);
    res = 0;

cleanup:
    if (param_type) DUCKDB_DESTROY_LOGICAL_TYPE(&param_type);
    return res;
}
