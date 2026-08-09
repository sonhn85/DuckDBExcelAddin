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

#define XLTYPEMASK                  0x0fff
#define ERR_MSG_XLRANGE_MAX_LEN     512
#define ERR_MSG_XLRANGE_INTERNAL    "An error occurred"
#define ERR_MSG_XLRANGE_CONVERT     "Fail to convert cell value"
#define ERR_MSG_XLRANGE_PARAM       "Invalid parameter" 
#define ERR_MSG_XLRANGE_COL_NAME    "Invalid column name"

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
} xlrange_bind_data_t;

typedef struct xlrange_scan_state_t
{
    LPXLOPER12 lparray; /* first data cell, not header */
    size_t ncols;
    size_t nrows;
    size_t row_idx;
    duckdb_type *types;
} xlrange_scan_state_t;

static void set_err_col_name(duckdb_bind_info info, size_t col)
{
    char buf[ERR_MSG_XLRANGE_MAX_LEN];
    if (snprintf(
        buf,
        ERR_MSG_XLRANGE_MAX_LEN,
        "xlrange: Invalid name for col %zu",
        col + 1
    ) < 0)
    {
        DUCKDB_BIND_SET_ERROR(info, ERR_MSG_XLRANGE_COL_NAME);
        return;
    }
    DUCKDB_BIND_SET_ERROR(info, buf);
}

static void free_bind_data(void *p)
{
    if (!p) return;
    xlrange_bind_data_t *bind_data = (xlrange_bind_data_t *)p;
    free(bind_data->types);
    free(bind_data);
}

static void xlrange_bind(duckdb_bind_info info)
{
    xlrange_context_t *ctx = (xlrange_context_t *)DUCKDB_BIND_GET_EXTRA_INFO(info);

    duckdb_logical_type lt = NULL;
    duckdb_type *types = NULL;
    xlrange_bind_data_t *bind_data = NULL;

    if (!ctx)
    {
        DUCKDB_BIND_SET_ERROR(info, ERR_MSG_XLRANGE_INTERNAL);
        goto fail;
    }
    if (DUCKDB_BIND_GET_PARAMETER_COUNT(info) != 1)
    {
        DUCKDB_BIND_SET_ERROR(info, ERR_MSG_XLRANGE_PARAM);
        goto fail;
    }
    duckdb_value val = DUCKDB_BIND_GET_PARAMETER(info, 0);      // Leak! duckdb_destroy_value(&val) crash
    if (!val)
    {
        DUCKDB_BIND_SET_ERROR(info, ERR_MSG_XLRANGE_INTERNAL);
        goto fail;
    }
    lt = DUCKDB_GET_VALUE_TYPE(val); 
    if (!lt)
    {
        DUCKDB_BIND_SET_ERROR(info, ERR_MSG_XLRANGE_INTERNAL);
        goto fail;
    }
    if (DUCKDB_GET_TYPE_ID(lt) != DUCKDB_TYPE_INTEGER)
    {
        DUCKDB_BIND_SET_ERROR(info, ERR_MSG_XLRANGE_PARAM);
        goto fail;
    }
    int32_t range_idx = DUCKDB_GET_INT32(val);
    if (range_idx <= 0 || (size_t)range_idx > ctx->nrange)
    {
        DUCKDB_BIND_SET_ERROR(info, ERR_MSG_XLRANGE_PARAM);
        goto fail;
    }
    LPXLOPER12 range = &ctx->ranges[range_idx - 1];
    if ((range->xltype & XLTYPEMASK) != xltypeMulti)
    {
        DUCKDB_BIND_SET_ERROR(info, ERR_MSG_XLRANGE_INTERNAL);
        goto fail;
    }
    size_t ncols = (size_t)range->val.array.columns;
    size_t nrows = (size_t)range->val.array.rows;
    LPXLOPER12 p = range->val.array.lparray;
    if (!p || ncols == 0 || nrows == 0)
    {
        DUCKDB_BIND_SET_ERROR(info, ERR_MSG_XLRANGE_INTERNAL);
        goto fail;
    }
    types = malloc(ncols*sizeof(*types));
    if (!types)
    {
        DUCKDB_BIND_SET_ERROR(info, ERR_MSG_XLRANGE_INTERNAL);
        goto fail;
    }
    for (size_t i=0; i < ncols; i++, p++)
    {
        if (((p->xltype & XLTYPEMASK) != xltypeStr)
            || is_null_or_whitespace_xlstr(p->val.str)
        ) {
            set_err_col_name(info, i);
            goto fail;
        }
        char *colname = NULL;
        if (xlstr_2_utf8(&colname, p->val.str, NULL) == 0 || !colname)
        {
            free(colname);
            set_err_col_name(info, i);
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
            DUCKDB_BIND_SET_ERROR(info, ERR_MSG_XLRANGE_INTERNAL);
            free(colname);
            goto fail;
        }
        types[i] = t;
        DUCKDB_BIND_ADD_RESULT_COLUMN(info, colname, col_type);
        DUCKDB_DESTROY_LOGICAL_TYPE(&col_type);
        free(colname);
    }
    bind_data = malloc(sizeof(*bind_data));
    if (!bind_data)
    {
        DUCKDB_BIND_SET_ERROR(info, ERR_MSG_XLRANGE_INTERNAL);
        goto fail;
    }
    bind_data->lparray = p;
    bind_data->ncols = (size_t)ncols;
    bind_data->nrows = (size_t)(nrows - 1);
    bind_data->types = types;
    types = NULL;

    DUCKDB_BIND_SET_BIND_DATA(info, bind_data, free_bind_data);
    bind_data = NULL;
    DUCKDB_BIND_SET_CARDINALITY(info, (idx_t)(nrows - 1), true);

    goto cleanup;

fail:
    free_bind_data(bind_data);
    free(types);

cleanup:
    if (lt) DUCKDB_DESTROY_LOGICAL_TYPE(&lt);
    //if (v) DUCKDB_DESTROY_VALUE(&v);
    return;
}

static void free_scan_state(void *p)
{
    if (!p) return;
    xlrange_scan_state_t *state = (xlrange_scan_state_t *)p;
    free(state->types);
    free(state);
}

static void xlrange_init(duckdb_init_info info)
{
    const char *errmsg = NULL;
    xlrange_bind_data_t *bind_data = (xlrange_bind_data_t *)DUCKDB_INIT_GET_BIND_DATA(info);
    if (!bind_data)
    {
        errmsg = ERR_MSG_XLRANGE_INTERNAL;
        goto fail;
    }
    xlrange_scan_state_t *state = malloc(sizeof(*state));
    size_t ncols = bind_data->ncols;
    duckdb_type *types = malloc(ncols*sizeof(*types));
    if (!state || !types)
    {
        free(state);
        free(types);
        errmsg = ERR_MSG_XLRANGE_INTERNAL;
        goto fail;
    }
    memcpy(types, bind_data->types, ncols*sizeof(*types));
    state->lparray = bind_data->lparray;
    state->ncols = ncols;
    state->nrows = bind_data->nrows;
    state->row_idx = 0;
    state->types = types;
    DUCKDB_INIT_SET_INIT_DATA(info, state, free_scan_state);
    return;

fail:
    DUCKDB_INIT_SET_ERROR(
        info,
        errmsg ? errmsg : ERR_MSG_XLRANGE_INTERNAL
    );
    return;
}

static void set_err_type_convert(duckdb_function_info info, size_t col, size_t row, const char *totype)
{
    char buf[ERR_MSG_XLRANGE_MAX_LEN];
    if (snprintf(
        buf,
        ERR_MSG_XLRANGE_MAX_LEN,
        "xlrange: Fail to convert value at col %zu, row %zu to %s",
        col + 1,
        row + 2,
        totype
    ) < 0)
    {
        DUCKDB_FUNCTION_SET_ERROR(info, ERR_MSG_XLRANGE_CONVERT);
        return;
    }
    DUCKDB_FUNCTION_SET_ERROR(info, buf);
}

static void xlrange_scan(duckdb_function_info info, duckdb_data_chunk output)
{
    xlrange_scan_state_t *state = (xlrange_scan_state_t *)DUCKDB_FUNCTION_GET_INIT_DATA(info);
    if (!state) {
        DUCKDB_FUNCTION_SET_ERROR(info, ERR_MSG_XLRANGE_INTERNAL);
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
                                set_err_type_convert(info, c, state->row_idx, "double");
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
                                set_err_type_convert(info, c, state->row_idx, "double");
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
                                set_err_type_convert(info, c, state->row_idx, "varchar");
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