#include <stdio.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "db_xlrange.h"
#include "helper.h"
#include "db_lib_loader.h"
#include "config.h"

#define ERR_MSG_XLRANGE_INTERNAL            "Internal error"
#define ERR_MSG_XLRANGE_INVALID_PARAM       "Invalid parameter" 
#define ERR_MSG_XLRANGE_EMPTY_COL_NAME      "Column name is empty"
#define ERR_MSG_XLRANGE_INVALID_COL_NAME    "Invalid column name"
#define ERR_MSG_XLRANGE_DOUBLE              "Failed to convert to DOUBLE"
#define ERR_MSG_XLRANGE_VARCHAR             "Failed to convert to VARCHAR"

typedef struct xlrange_context_t
{
    LPXLOPER12      ranges;     // Excel ranges supplied by caller
    size_t          nrange;     // Number of ranges
} xlrange_context_t;

typedef struct xlrange_bind_data_t
{
    LPXLOPER12      lparray;    // First data cell (header excluded)
    size_t          ncols;      // Number of columns
    size_t          nrows;      // Number of data rows
    duckdb_type     *types;     // Inferred DuckDB types
    char            **colnames; // UTF-8 column names (owned)
} xlrange_bind_data_t;

typedef struct xlrange_scan_state_t
{
    LPXLOPER12      lparray;    // First data cell (header excluded)
    size_t          ncols;      // Number of columns
    size_t          nrows;      // Number of data rows
    size_t          next_row;   // Next row to scan
    duckdb_type     *types;     // Inferred DuckDB types
    char            **colnames; // UTF-8 column names (owned)
} xlrange_scan_state_t;

static void format_error_message
(
    char            *buf,
    size_t          buf_size,
    const char      *action,
    const char      *colname,
    long long       col_idx,
    long long       row_idx,
    const char      *msg
)
{
    if (!buf || buf_size == 0 || !action || !msg)
        return;

    if (colname)
    {
        if (row_idx >= 0)
        {
            snprintf(
                buf,
                buf_size,
                "Error %s: Column %s, row %lld: %s",
                action,
                colname,
                row_idx + 2, // +1 for 0-based index, +1 for header row
                msg
            );
        }
        else
        {
            snprintf(
                buf,
                buf_size,
                "Error %s: Column %s: %s",
                action,
                colname,
                msg
            );
        }
    }
    else if (col_idx >= 0)
    {
        if (row_idx >= 0)
        {
            snprintf(
                buf,
                buf_size,
                "Error %s: Column #%lld, row %lld: %s",
                action,
                col_idx + 1,
                row_idx + 2,    // +1 for 0-based index, +1 for header row
                msg
            );
        }
        else
        {
            snprintf(
                buf,
                buf_size,
                "Error %s: Column #%lld: %s",
                action,
                col_idx + 1,
                msg
            );
        }
    }
    else
    {
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
    if (!p)
        return;

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

#define SET_BIND_ERROR(BUF, MSG) \
    format_error_message(        \
        (BUF),                   \
        sizeof(BUF),             \
        "binding xlrange",       \
        NULL,                    \
        -1,                      \
        -1,                      \
        (MSG)                    \
    )

static void xlrange_bind(duckdb_bind_info info)
{
    xlrange_bind_data_t     *bind_data = NULL;

    duckdb_value            val_index = NULL;
    duckdb_value            val_sample = NULL;

    duckdb_logical_type     lt_index = NULL;
    duckdb_logical_type     lt_sample = NULL;

    duckdb_type             *types = NULL;
    char                    **colnames = NULL;

    int32_t                 range_idx = 0;
    size_t                  nsample = 0;
    size_t                  ndatarows = 0;
    size_t                  nrows = 0;
    size_t                  ncols = 0;

    char errmsg[ERR_MSG_MAX_LEN];
    errmsg[0] = '\0';

    xlrange_context_t *ctx = DUCKDB_BIND_GET_EXTRA_INFO(info);

    if (!ctx)
    {
        SET_BIND_ERROR(errmsg, ERR_MSG_XLRANGE_INTERNAL);
        goto fail;
    }

    /* xlrange(index) -> one required positional parameter
       val_index owns lt_index */
    if (DUCKDB_BIND_GET_PARAMETER_COUNT(info) != 1
        || !(val_index = DUCKDB_BIND_GET_PARAMETER(info, 0))
        || DUCKDB_IS_NULL_VALUE(val_index)
        || !(lt_index = DUCKDB_GET_VALUE_TYPE(val_index))
        || DUCKDB_GET_TYPE_ID(lt_index) != DUCKDB_TYPE_INTEGER)
    {
        SET_BIND_ERROR(errmsg, ERR_MSG_XLRANGE_INVALID_PARAM);
        goto fail;
    }

    range_idx = DUCKDB_GET_INT32(val_index);

    if (range_idx <= 0 || (size_t)range_idx > ctx->nrange)
    {
        SET_BIND_ERROR(errmsg, ERR_MSG_XLRANGE_INVALID_PARAM);
        goto fail;
    }

    /* xlrange(..., sample = n)
       val_sample owns lt_sample */
    int32_t sample_count = XLRANGE_DEFAULT_SAMPLE_COUNT; // Default
    val_sample = DUCKDB_BIND_GET_NAMED_PARAMETER(info, "sample");
    if (val_sample)
    {
        if (!DUCKDB_IS_NULL_VALUE(val_sample))
        {
            if (!(lt_sample = DUCKDB_GET_VALUE_TYPE(val_sample))
                || DUCKDB_GET_TYPE_ID(lt_sample) != DUCKDB_TYPE_INTEGER)
            {
                sample_count = -1;
            }
            else
            {
                sample_count = DUCKDB_GET_INT32(val_sample);
            }
        }
        else
        {
            sample_count = -1;
        }
    }

    if (sample_count < 0)
    {
        SET_BIND_ERROR(errmsg, ERR_MSG_XLRANGE_INVALID_PARAM);
        goto fail;
    }

    nsample = (size_t)sample_count;

    LPXLOPER12 range = &ctx->ranges[range_idx - 1];

    if (LPXLOPER12_TYPE(range) != xltypeMulti)
    {
        SET_BIND_ERROR(errmsg, ERR_MSG_XLRANGE_INTERNAL);
        goto fail;
    }

    ncols = (size_t)range->val.array.columns;
    nrows = (size_t)range->val.array.rows;
    
    ndatarows = (nrows > 0) ? nrows - 1 : 0;

    LPXLOPER12 p = range->val.array.lparray;

    if (!p || ncols == 0 || nrows == 0)
    {
        SET_BIND_ERROR(errmsg, ERR_MSG_XLRANGE_INTERNAL);
        goto fail;
    }

    types = malloc(ncols*sizeof(*types));
    colnames = calloc(ncols, sizeof(*colnames));

    if (!types || !colnames)
    {
        SET_BIND_ERROR(errmsg, ERR_MSG_XLRANGE_INTERNAL);
        goto fail;
    }

    // Bind result columns and infer DuckDB types
    for (size_t i=0; i < ncols; i++, p++)
    {
        char *colname = NULL;
        duckdb_logical_type lt_col = NULL;

        if (LPXLOPER12_TYPE(p) != xltypeStr)
        {
            SET_BIND_ERROR(errmsg, ERR_MSG_XLRANGE_INTERNAL);
            goto bind_column_failure;
        }

        bool empty_name = is_null_or_whitespace_xlstr(p->val.str);
        if (empty_name
            || (xlstr_to_utf8(&colname, p->val.str, NULL) == 0)
            || !colname)
        {
            format_error_message(
                errmsg,
                sizeof(errmsg),
                "binding xlrange",
                NULL,
                (long long)i,
                -1,
                (empty_name) 
                    ? ERR_MSG_XLRANGE_EMPTY_COL_NAME
                    : ERR_MSG_XLRANGE_INVALID_COL_NAME
            );
            goto bind_column_failure;
        }

        // Defaults to VARCHAR if no sample value is found
        duckdb_type type = DUCKDB_TYPE_VARCHAR;
        if (ndatarows > 0)
        {
            // nsample == 0 means sample all data rows
            if (nsample == 0 || nsample > ndatarows)
                nsample = ndatarows;

            LPXLOPER12 cell = p + ncols;

            bool found = false;

            // Sample first non-empty value for type inference
            for (size_t j=0; j < nsample; j++, cell += ncols)
            {
                switch (LPXLOPER12_TYPE(cell))
                {
                    case xltypeMissing:
                    case xltypeErr:
                    case xltypeNil:
                        continue;

                    case xltypeInt:
                    case xltypeNum:
                        type = DUCKDB_TYPE_DOUBLE;
                        found = true;
                        break;

                    case xltypeBool:
                        type = DUCKDB_TYPE_BOOLEAN;
                        found = true;
                        break;
                        
                    case xltypeStr:
                    default:
                        type = DUCKDB_TYPE_VARCHAR;
                        found = true;
                        break;
                }

                if (found)
                    break;
            }
        }

        lt_col = DUCKDB_CREATE_LOGICAL_TYPE(type);

        if (!lt_col)
        {
            SET_BIND_ERROR(errmsg, ERR_MSG_XLRANGE_INTERNAL);
            goto bind_column_failure;
        }
        else
        {
            types[i] = type;
            colnames[i] = colname;

            DUCKDB_BIND_ADD_RESULT_COLUMN(info, colname, lt_col);
            colname = NULL;
        }

        DUCKDB_DESTROY_LOGICAL_TYPE(&lt_col);

        continue;

    bind_column_failure:

        free(colname);
    
        if (lt_col)
            DUCKDB_DESTROY_LOGICAL_TYPE(&lt_col);

        goto fail;
    }

    bind_data = malloc(sizeof(*bind_data));

    if (!bind_data)
    {
        SET_BIND_ERROR(errmsg, ERR_MSG_XLRANGE_INTERNAL);
        goto fail;
    }

    bind_data->lparray = p;
    bind_data->ncols = (size_t)ncols;
    bind_data->nrows = ndatarows;
    bind_data->types = types;
    types = NULL;
    bind_data->colnames = colnames;
    colnames = NULL;

    DUCKDB_BIND_SET_BIND_DATA(info, bind_data, free_bind_data);
    bind_data = NULL;

    DUCKDB_BIND_SET_CARDINALITY(info, (idx_t)ndatarows, true);

    goto cleanup;

fail:

    DUCKDB_BIND_SET_ERROR(
        info,
        errmsg[0] ? errmsg : ERR_MSG_XLRANGE_INTERNAL
    );

    free_bind_data(bind_data);
    free(types);
    if (colnames)
    {
        for (size_t i = 0; i < ncols; i++)
            free(colnames[i]);

        free(colnames);
    }

cleanup:

    if (val_index)
        DUCKDB_DESTROY_VALUE(&val_index);

    if (val_sample)
        DUCKDB_DESTROY_VALUE(&val_sample);

    return;
}

static void free_scan_state(void *p)
{
    if (!p)
        return;

    xlrange_scan_state_t *state = p;

    free(state->types);

    if (state->colnames)
    {
        for (size_t i = 0; i < state->ncols; i++)
            free(state->colnames[i]);

        free(state->colnames);
    }

    free(state);
}

#define SET_INIT_ERROR(BUF, MSG) \
    format_error_message(        \
        (BUF),                   \
        sizeof(BUF),             \
        "initializing xlrange",  \
        NULL,                    \
        -1,                      \
        -1,                      \
        (MSG)                    \
    )

static void xlrange_init(duckdb_init_info info)
{
    xlrange_scan_state_t *state = NULL;
    duckdb_type          *types = NULL;
    char                 **colnames = NULL;
    size_t               ncols = 0;

    char errmsg[ERR_MSG_MAX_LEN];
    errmsg[0] = '\0';

    xlrange_bind_data_t *bind_data = DUCKDB_INIT_GET_BIND_DATA(info);

    if (!bind_data)
    {
        SET_INIT_ERROR(errmsg, ERR_MSG_XLRANGE_INTERNAL);
        goto fail;
    }

    ncols = bind_data->ncols;

    state = calloc(1, sizeof(*state));

    types = malloc(ncols * sizeof(*types));
    colnames = calloc(ncols, sizeof(*colnames));

    if (!state || !types || !colnames)
    {
        SET_INIT_ERROR(errmsg, ERR_MSG_XLRANGE_INTERNAL);
        goto fail;
    }

    memcpy(types, bind_data->types, ncols*sizeof(*types));

    // Copy column names
    for (size_t i = 0; i < ncols; i++)
    {
        char *src = bind_data->colnames[i];

        char *copy = NULL;

        if (!src || !(copy = _strdup(src)))
        {
            format_error_message(
                errmsg,
                sizeof(errmsg),
                "initializing xlrange",
                NULL,
                (long long)i,
                -1,
                ERR_MSG_XLRANGE_INTERNAL
            );
            goto fail;
        }

        colnames[i] = copy;
    }

    state->lparray = bind_data->lparray;
    state->ncols = ncols;
    state->nrows = bind_data->nrows;
    state->next_row = 0;
    state->types = types;
    types = NULL;
    state->colnames = colnames;
    colnames = NULL;

    DUCKDB_INIT_SET_INIT_DATA(info, state, free_scan_state);
    state = NULL;

    return;

fail:
    free(types);

    if (colnames)
    {
        for (size_t i = 0; i < ncols; i++)
            free(colnames[i]);

        free(colnames);
    }

    free_scan_state(state);

    DUCKDB_INIT_SET_ERROR(
        info,
        errmsg[0] ? errmsg : ERR_MSG_XLRANGE_INTERNAL
    );

    return;
}

#define SET_SCANNING_ERROR(BUF, MSG) \
    format_error_message(            \
        (BUF),                       \
        sizeof(BUF),                 \
        "scanning xlrange",          \
        NULL,                        \
        -1,                          \
        -1,                          \
        (MSG)                        \
    )

static void xlrange_scan(duckdb_function_info info, duckdb_data_chunk output)
{
    char errmsg[ERR_MSG_MAX_LEN];
    errmsg[0] = '\0';

    xlrange_scan_state_t *state = DUCKDB_FUNCTION_GET_INIT_DATA(info);

    if (!state)
    {
        SET_SCANNING_ERROR(errmsg, ERR_MSG_XLRANGE_INTERNAL);
        goto fail;
    }

    idx_t out_rows = 0;

    while (
        state->next_row < state->nrows
        && out_rows < DUCKDB_VECTOR_SIZE())
    {
        for (size_t c = 0; c < state->ncols; c++)
        {
            duckdb_vector vec = DUCKDB_DATA_CHUNK_GET_VECTOR(output, c);

            size_t idx = state->next_row * state->ncols + c;

            LPXLOPER12 cell = &state->lparray[idx];

            switch (state->types[c])
            {
                case DUCKDB_TYPE_DOUBLE:
                {
                    double *data = (double *)DUCKDB_VECTOR_GET_DATA(vec);

                    switch (LPXLOPER12_TYPE(cell))
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
                            char *dest = NULL;
                            char *endptr = NULL;
                            double num;
                            bool ok = false;

                            wchar_t *src = cell->val.str;

                            if (!src)
                                goto sqlnull;

                            if (xlstr_to_utf8(&dest, src, NULL) == 0 || !dest)
                                goto double_convert_cleanup;

                            errno = 0;

                            num = strtod(dest, &endptr);

                            if (errno == ERANGE
                                || endptr == dest
                                || *endptr != '\0'
                                || !isfinite(num))
                            {
                                goto double_convert_cleanup;
                            }
                            
                            data[out_rows] = num;
                            ok = true;

                        double_convert_cleanup:

                            free(dest);

                            if (!ok) 
                            {
                                format_error_message(
                                    errmsg,
                                    sizeof(errmsg),
                                    "scanning xlrange",
                                    state->colnames[c],
                                    -1,
                                    (long long)state->next_row,
                                    ERR_MSG_XLRANGE_DOUBLE
                                );

                                goto fail;
                            }

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
                    
                    switch (LPXLOPER12_TYPE(cell)) {
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
                    switch (LPXLOPER12_TYPE(cell))
                    {
                        case xltypeNil:
                        case xltypeMissing:
                        case xltypeErr:
                            goto sqlnull;

                        case xltypeStr:
                        {
                            char *dest = NULL;

                            wchar_t *src = cell->val.str;

                            bool ok = false;

                            if (!src) goto sqlnull;

                            if (xlstr_to_utf8(&dest, src, NULL) == 0 || !dest)
                                goto varchar_convert_cleanup;

                            DUCKDB_VECTOR_ASSIGN_STRING_ELEMENT(vec, out_rows, dest);
                            ok = true;

                        varchar_convert_cleanup:

                            free(dest);

                            if (!ok)
                            {
                                format_error_message(
                                    errmsg,
                                    sizeof(errmsg),
                                    "scanning xlrange",
                                    state->colnames[c],
                                    -1,
                                    (long long)state->next_row,
                                    ERR_MSG_XLRANGE_VARCHAR
                                );

                                goto fail;
                            }

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

        state->next_row++;

        out_rows++;
    }

    DUCKDB_DATA_CHUNK_SET_SIZE(output, out_rows);

    return;

fail:

    DUCKDB_FUNCTION_SET_ERROR(
        info,
        errmsg[0] ? errmsg : ERR_MSG_XLRANGE_INTERNAL
    );

    DUCKDB_DATA_CHUNK_SET_SIZE(output, 0);

    return;
}

int register_xlrange_func
(
    duckdb_connection con,
    LPXLOPER12 ranges,
    size_t nrange,
    duckdb_table_function *func
) 
{
    int res = 0;

    duckdb_table_function fn_tmp = NULL;
    duckdb_logical_type index_type = NULL;
    duckdb_logical_type sample_type = NULL;

    if (!func || (!ranges && nrange > 0)) return 0;

    *func = NULL;

    fn_tmp = DUCKDB_CREATE_TABLE_FUNCTION();

    if (!fn_tmp)
        goto fail;

    // Shared context for all xlrange() instances in the query
    xlrange_context_t *ctx = malloc(sizeof(*ctx));
    if (!ctx)
        goto fail;

    ctx->ranges = ranges;
    ctx->nrange = nrange;

    DUCKDB_TABLE_FUNCTION_SET_EXTRA_INFO(fn_tmp, ctx, free);

    // xlrange(index)
    index_type = DUCKDB_CREATE_LOGICAL_TYPE(DUCKDB_TYPE_INTEGER);
    // xlrange(..., sample := n)
    sample_type = DUCKDB_CREATE_LOGICAL_TYPE(DUCKDB_TYPE_INTEGER);

    if (!index_type || !sample_type)
        goto fail;

    DUCKDB_TABLE_FUNCTION_SET_NAME(fn_tmp, "xlrange");
    DUCKDB_TABLE_FUNCTION_ADD_PARAMETER(fn_tmp, index_type);
    DUCKDB_TABLE_FUNCTION_ADD_NAMED_PARAMETER(fn_tmp, "sample", sample_type);
    DUCKDB_TABLE_FUNCTION_SET_BIND(fn_tmp, xlrange_bind);
    DUCKDB_TABLE_FUNCTION_SET_INIT(fn_tmp, xlrange_init);
    DUCKDB_TABLE_FUNCTION_SET_FUNCTION(fn_tmp, xlrange_scan);

    if (DUCKDB_REGISTER_TABLE_FUNCTION(con, fn_tmp) != DuckDBSuccess)
        goto fail;

    *func = fn_tmp;
    res = 1;

    goto cleanup;

fail:
    if (fn_tmp)
        DUCKDB_DESTROY_TABLE_FUNCTION(&fn_tmp);
    res = 0;

cleanup:
    if (index_type)
        DUCKDB_DESTROY_LOGICAL_TYPE(&index_type);

    if (sample_type)
        DUCKDB_DESTROY_LOGICAL_TYPE(&sample_type);

    return res;
}
