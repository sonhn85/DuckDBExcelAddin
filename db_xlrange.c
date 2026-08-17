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
#define ERR_MSG_XLRANGE_INT                 "Failed to convert to INTEGER"
#define ERR_MSG_XLRANGE_VARCHAR             "Failed to convert to VARCHAR"

typedef struct xlrange_context_t
{
    LPXLOPER12      ranges;     /* Excel ranges supplied by caller */
    size_t          nrange;     /* Number of ranges */
} xlrange_context_t;

typedef struct xlrange_bind_data_t
{
    LPXLOPER12      lparray;    /* First data cell (header excluded) */
    size_t          ncols;      /* Number of columns */
    size_t          nrows;      /* Number of data rows */
    duckdb_type     *types;     /* Inferred DuckDB types */
    char            **colnames; /* UTF-8 column names (owned) */
} xlrange_bind_data_t;

typedef struct xlrange_scan_state_t
{
    LPXLOPER12      lparray;    /* First data cell (header excluded) */
    size_t          ncols;      /* Number of columns */
    size_t          nrows;      /* Number of data rows */
    size_t          next_row;   /* Next row to scan */
    duckdb_type     *types;     /* Inferred DuckDB types */
    char            **colnames; /* UTF-8 column names (owned) */
    idx_t           vec_size;   /* Duckdb vector size */
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
    format_error_message( \
        BUF, \
        sizeof(BUF), \
        "binding xlrange", \
        NULL, \
        -1, \
        -1, \
        MSG \
    )

static bool is_int(double num)
{
    return (fabs(fmod(num, 1.0)) < EPSILON
            && num >= INT32_MIN
            && num <= INT32_MAX);
}

static void xlrange_bind(duckdb_bind_info info)
{
    xlrange_bind_data_t     *bind_data = NULL;

    duckdb_value            val_index = NULL;
    duckdb_value            val_sample = NULL;
    duckdb_value            val_all_varchar = NULL;

    duckdb_logical_type     lt_index = NULL;
    duckdb_logical_type     lt_sample = NULL;
    duckdb_logical_type     lt_all_varchar = NULL;

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
     * val_index owns lt_index */
    if (DUCKDB_BIND_GET_PARAMETER_COUNT(info) != 1)
    {
        SET_BIND_ERROR(errmsg, ERR_MSG_XLRANGE_INVALID_PARAM);
        goto fail;
    }

    if (!(val_index = DUCKDB_BIND_GET_PARAMETER(info, 0)))
    {
        SET_BIND_ERROR(errmsg, ERR_MSG_XLRANGE_INTERNAL);
        goto fail;
    }

    if (DUCKDB_IS_NULL_VALUE(val_index))
    {
        SET_BIND_ERROR(errmsg, ERR_MSG_XLRANGE_INVALID_PARAM);
        goto fail;
    }

    if(!(lt_index = DUCKDB_GET_VALUE_TYPE(val_index)))
    {
        SET_BIND_ERROR(errmsg, ERR_MSG_XLRANGE_INTERNAL);
        goto fail;
    }

    if(DUCKDB_GET_TYPE_ID(lt_index) != DUCKDB_TYPE_INTEGER)
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

    /* xlrange(..., all_varchar = true)
     * val_all_varchar owns lt_all_varchar */
    bool all_varchar = false; // Default

    val_all_varchar = DUCKDB_BIND_GET_NAMED_PARAMETER(info, "all_varchar");

    if (val_all_varchar)
    {
        if (DUCKDB_IS_NULL_VALUE(val_all_varchar))
        {
            SET_BIND_ERROR(errmsg, ERR_MSG_XLRANGE_INVALID_PARAM);
            goto fail;
        }

        if (!(lt_all_varchar = DUCKDB_GET_VALUE_TYPE(val_all_varchar)))
        {
            SET_BIND_ERROR(errmsg, ERR_MSG_XLRANGE_INTERNAL);
            goto fail;
        }

        if (DUCKDB_GET_TYPE_ID(lt_all_varchar) != DUCKDB_TYPE_BOOLEAN)
        {
            SET_BIND_ERROR(errmsg, ERR_MSG_XLRANGE_INVALID_PARAM);
            goto fail;
        }

        all_varchar = DUCKDB_GET_BOOL(val_all_varchar);
    }

    /* xlrange(..., sample = n)
     * val_sample owns lt_sample */
    int32_t sample_count = XLRANGE_DEFAULT_SAMPLE_COUNT; // Default

    val_sample = DUCKDB_BIND_GET_NAMED_PARAMETER(info, "sample");

    if (!all_varchar && val_sample)
    {
        if (DUCKDB_IS_NULL_VALUE(val_sample))
        {
            SET_BIND_ERROR(errmsg, ERR_MSG_XLRANGE_INVALID_PARAM);
            goto fail;
        }

        if (!(lt_sample = DUCKDB_GET_VALUE_TYPE(val_sample)))
        {
            SET_BIND_ERROR(errmsg, ERR_MSG_XLRANGE_INTERNAL);
            goto fail;
        }

        if (DUCKDB_GET_TYPE_ID(lt_sample) != DUCKDB_TYPE_INTEGER)
        {
            SET_BIND_ERROR(errmsg, ERR_MSG_XLRANGE_INVALID_PARAM);
            goto fail;
        }

        sample_count = DUCKDB_GET_INT32(val_sample);

        if (sample_count < 0)
        {
            SET_BIND_ERROR(errmsg, ERR_MSG_XLRANGE_INVALID_PARAM);
            goto fail;
        }
    }

    nsample = sample_count;

    LPXLOPER12 range = &ctx->ranges[range_idx - 1];

    if (LPXLOPER12_TYPE(range) != xltypeMulti)
    {
        SET_BIND_ERROR(errmsg, ERR_MSG_XLRANGE_INTERNAL);
        goto fail;
    }

    ncols = (size_t)range->val.array.columns;
    nrows = (size_t)range->val.array.rows;
    ndatarows = (nrows > 0) ? nrows - 1 : 0;

    if (nsample == 0 || nsample > ndatarows)
        nsample = ndatarows;

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

    /* Infer DuckDB types and bind result columns */
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

        /*
        * Type inference strategy:
        *
        * 1. Scan for the first non-empty value and use its type as the
        *    candidate column type.
        *
        * 2. Sample remaining rows.
        *
        * 3. If incompatible types are encountered, fall back to VARCHAR.
        *
        * 4. Integer-valued numeric cells are inferred as INTEGER when
        *    all sampled numeric values fit within INT32.
        */
        WORD xltype = xltypeStr;

        if (!all_varchar && ndatarows > 0)
        {
            /* First data cell */
            LPXLOPER12 cell = p + ncols;

            /* Sample first non-empty value */
            size_t i;

            for (i = 0; i < nsample; i++, cell += ncols)
            {
                WORD cell_type = LPXLOPER12_TYPE(cell);

                if (cell_type == xltypeNum)
                {
                    /* Check if double is actually int */
                    xltype = is_int(cell->val.num) ? xltypeInt : xltypeNum;
                    break;
                }
                else if (cell_type == xltypeStr
                         || cell_type == xltypeBool)
                {
                    xltype = cell_type;
                    break;
                }
            }

            /* Sample remaining rows */
            if (xltype != xltypeStr)
            {
                for (size_t j = i; j < nsample; j++, cell += ncols)
                {
                    WORD cell_type = LPXLOPER12_TYPE(cell);

                    if (xltype == xltypeInt)
                    {
                        if (cell_type == xltypeNum)
                        {
                            if (!is_int(cell->val.num))
                            {
                                xltype = xltypeNum;
                                break;
                            }
                        }
                        else if (cell_type == xltypeInt 
                                 || cell_type == xltypeNil 
                                 || cell_type == xltypeMissing 
                                 || cell_type == xltypeErr)
                        {
                            continue;
                        }
                        else
                        {
                            xltype = xltypeStr;
                            break;
                        } 
                    }
                    else if (cell_type == xltypeNil
                             || cell_type == xltypeMissing
                             || cell_type == xltypeErr)
                    {
                        continue;
                    }
                    else if (cell_type != xltype)
                    {
                        xltype = xltypeStr;
                        break;
                    }
                }
            }
        }

        // Map xltype to duckdb type
        duckdb_type type;
    
        switch (xltype)
        {
            case xltypeInt:
                type = DUCKDB_TYPE_INTEGER;
                break;

            case xltypeNum:
                type = DUCKDB_TYPE_DOUBLE;
                break;

            case xltypeBool:
                type = DUCKDB_TYPE_BOOLEAN;
                break;

            case xltypeStr:
            default:
                type = DUCKDB_TYPE_VARCHAR;
                break;
        }

        lt_col = DUCKDB_CREATE_LOGICAL_TYPE(type);

        if (!lt_col)
        {
            SET_BIND_ERROR(errmsg, ERR_MSG_XLRANGE_INTERNAL);
            goto bind_column_failure;
        }

        types[i] = type;
        colnames[i] = colname;

        DUCKDB_BIND_ADD_RESULT_COLUMN(info, colname, lt_col);
        colname = NULL;

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

    if (val_all_varchar)
        DUCKDB_DESTROY_VALUE(&val_all_varchar);

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
    format_error_message( \
        BUF, \
        sizeof(BUF), \
        "initializing xlrange", \
        NULL, \
        -1, \
        -1, \
        MSG \
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
    state->vec_size = DUCKDB_VECTOR_SIZE();

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
    format_error_message( \
        BUF, \
        sizeof(BUF), \
        "scanning xlrange", \
        NULL, \
        -1, \
        -1, \
        MSG \
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
        && out_rows < state->vec_size)
    {
        for (size_t c = 0; c < state->ncols; c++)
        {
            duckdb_vector vec = DUCKDB_DATA_CHUNK_GET_VECTOR(output, c);

            if (!vec)
            {
                SET_SCANNING_ERROR(errmsg, ERR_MSG_XLRANGE_INTERNAL);
                goto fail;
            }

            size_t idx = state->next_row * state->ncols + c;

            LPXLOPER12 cell = &state->lparray[idx];

            switch (state->types[c])
            {
                case DUCKDB_TYPE_INTEGER:
                {
                    int32_t *data = DUCKDB_VECTOR_GET_DATA(vec);

                    if (!data)
                    {
                        SET_SCANNING_ERROR(errmsg, ERR_MSG_XLRANGE_INTERNAL);
                        goto fail;
                    }

                    switch (LPXLOPER12_TYPE(cell))
                    {
                        case xltypeInt:
                            data[out_rows] = cell->val.w;
                            break;

                        case xltypeNum:
                            if (!is_int(cell->val.num))
                            {
                                format_error_message(
                                    errmsg,
                                    sizeof(errmsg),
                                    "scanning xlrange",
                                    state->colnames[c],
                                    -1,
                                    (long long)state->next_row,
                                    ERR_MSG_XLRANGE_INT
                                );
                                goto fail;
                            }

                            data[out_rows] = (int32_t)cell->val.num;
                            break;

                        case xltypeBool:
                            data[out_rows] = (cell->val.xbool == true) ? 1 : 0;
                            break;

                        case xltypeStr:
                        {
                            wchar_t *src = cell->val.str;

                            if (!src)
                                goto sqlnull;

                            char *dest = NULL;
                            char *endptr = NULL;
                            long long num;
                            bool ok = false;

                            if (xlstr_to_utf8(&dest, src, NULL) == 0 || !dest)
                                goto int_convert_cleanup;

                            errno = 0;

                            num = strtoll(dest, &endptr, 10);

                            if (errno == ERANGE
                                || endptr == dest
                                || *endptr != '\0'
                                || num < INT32_MIN
                                || num > INT32_MAX)
                            {
                                goto int_convert_cleanup;
                            }
                            
                            data[out_rows] = (int32_t)num;
                            ok = true;

                        int_convert_cleanup:

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
                                    ERR_MSG_XLRANGE_INT
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

                case DUCKDB_TYPE_DOUBLE:
                {
                    double *data = DUCKDB_VECTOR_GET_DATA(vec);

                    if (!data)
                    {
                        SET_SCANNING_ERROR(errmsg, ERR_MSG_XLRANGE_INTERNAL);
                        goto fail;
                    }

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
                            wchar_t *src = cell->val.str;

                            if (!src)
                                goto sqlnull;

                            char *dest = NULL;
                            char *endptr = NULL;
                            double num;
                            bool ok = false;

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
                    bool *data = DUCKDB_VECTOR_GET_DATA(vec);

                    if (!data)
                    {
                        SET_SCANNING_ERROR(errmsg, ERR_MSG_XLRANGE_INTERNAL);
                        goto fail;
                    }
                    
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
                            wchar_t *src = cell->val.str;

                            if (!src) goto sqlnull;

                            char *dest = NULL;
                            bool ok = false;

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
                            snprintf(buf, sizeof(buf), "%d", cell->val.w);
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
    duckdb_table_function *function
) 
{
    int res = 0;

    duckdb_table_function table_func = NULL;
    duckdb_logical_type int_type = NULL;
    duckdb_logical_type bool_type = NULL;

    if (!function || (!ranges && nrange > 0)) return 0;

    *function = NULL;

    table_func = DUCKDB_CREATE_TABLE_FUNCTION();

    if (!table_func)
        goto fail;

    // Shared context for all xlrange() instances in the query
    xlrange_context_t *ctx = malloc(sizeof(*ctx));
    if (!ctx)
        goto fail;

    ctx->ranges = ranges;
    ctx->nrange = nrange;

    DUCKDB_TABLE_FUNCTION_SET_EXTRA_INFO(table_func, ctx, free);

    // xlrange(index), xlrange(..., sample = n)
    int_type = DUCKDB_CREATE_LOGICAL_TYPE(DUCKDB_TYPE_INTEGER);
    // xlrange(..., all_varchar = true)
    bool_type = DUCKDB_CREATE_LOGICAL_TYPE(DUCKDB_TYPE_BOOLEAN);

    if (!int_type || !bool_type)
        goto fail;

    DUCKDB_TABLE_FUNCTION_SET_NAME(table_func, "xlrange");
    DUCKDB_TABLE_FUNCTION_ADD_PARAMETER(table_func, int_type);
    DUCKDB_TABLE_FUNCTION_ADD_NAMED_PARAMETER(table_func, "sample", int_type);
    DUCKDB_TABLE_FUNCTION_ADD_NAMED_PARAMETER(table_func, "all_varchar", bool_type);
    DUCKDB_TABLE_FUNCTION_SET_BIND(table_func, xlrange_bind);
    DUCKDB_TABLE_FUNCTION_SET_INIT(table_func, xlrange_init);
    DUCKDB_TABLE_FUNCTION_SET_FUNCTION(table_func, xlrange_scan);

    if (DUCKDB_REGISTER_TABLE_FUNCTION(con, table_func) != DuckDBSuccess)
        goto fail;

    *function = table_func;
    res = 1;

    goto cleanup;

fail:
    if (table_func)
        DUCKDB_DESTROY_TABLE_FUNCTION(&table_func);
    res = 0;

cleanup:
    if (int_type)
        DUCKDB_DESTROY_LOGICAL_TYPE(&int_type);

    if (bool_type)
        DUCKDB_DESTROY_LOGICAL_TYPE(&bool_type);

    return res;
}
