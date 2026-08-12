#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>

#include "db_fetch.h"
#include "db_lib_loader.h"
#include "helper.h"
#include "config.h"

#define ERR_MSG_INTERNAL            "An internal error occurred"
#define ERR_MSG_ALLOC               "Memory allocation failed"
#define ERR_MSG_INVALID_COL_NAME    "Invalid column name"
#define ERR_MSG_NO_DATA             "Statement returned no data"
#define ERR_MSG_EXCEED_SIZE_LIMIT   "Excel row or column limit exceeded"
#define ERR_MSG_UNSUPPORTED_TYPE    "Unsupported DuckDB type. Convert the value to VARCHAR before returning it to Excel"

// duckdb max decimal scale is 18 
static const double DEC_DIVISORS[] = {
    1e0,    1e1,    1e2,    1e3,    1e4,    1e5,    1e6,    1e7,    1e8,    1e9,
    1e10,   1e11,   1e12,   1e13,   1e14,   1e15,   1e16,   1e17,   1e18
};

#define S_PER_DAY   86400.0                 // seconds per day
#define MS_PER_DAY  86400000.0              // miliseconds per day
#define US_PER_DAY  86400000000.0           // microseconds per day
#define NS_PER_DAY  86400000000000.0        // nanoseconds per day
#define EPOCH_DELTA 25569.0                 // in days between excel and duckdb epoch
#define TIMETZ_OFFSET_BITS      24

void free_and_reset_chunk_list(chunk_list *chunklist)
{
    if (!chunklist)
        return;

    chunk_node *node = chunklist->head;

    while (node)
    {
        chunk_node *next = node->next;
        free(node->vectors);
        free(node->valid_masks);
        DUCKDB_DESTROY_DATA_CHUNK(&(node->chunk));
        free(node);
        node = next;
    }

    free(chunklist->col_names);
    free(chunklist->col_types);
    free(chunklist->base_types);
    free(chunklist->dec_scales);

    *chunklist = (chunk_list){0};
}

static void format_error_message(
    char *buf,
    size_t buf_size,
    const char *action,
    const char *colname,
    const char *msg
)
{
    if (!buf || buf_size == 0 || !action || !msg)
        return;

    if (colname)
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

int fetch_chunks(
    duckdb_result *result,
    chunk_list *chunklist,
    char *errmsg,
    size_t buf_size
)
{
    if (!errmsg || buf_size == 0)
        goto fail;

    errmsg[0] = '\0';

    if (!chunklist || !result)
    {
        format_error_message(
            errmsg,
            buf_size,
            "fetching data",
            NULL,
            ERR_MSG_INTERNAL
        );
        goto fail;
    }

    free_and_reset_chunk_list(chunklist);

    idx_t ncols = DUCKDB_COLUMN_COUNT(result);

    if (ncols == 0)
    {
        format_error_message(
            errmsg,
            buf_size,
            "fetching data",
            NULL,
            ERR_MSG_NO_DATA
        );
        goto fail;
    }
    else if (ncols > XL_MAX_COL)
    {
        format_error_message(
            errmsg,
            buf_size,
            "fetching data",
            NULL,
            ERR_MSG_EXCEED_SIZE_LIMIT
        );
        goto fail;
    }

    const char **col_names = calloc(ncols, sizeof(*col_names));
    duckdb_type *col_types = malloc(ncols*sizeof(*col_types));
    duckdb_type *base_types = malloc(ncols*sizeof(*base_types));
    uint8_t *dec_scales = malloc(ncols*sizeof(*dec_scales));

    if (!col_names || !col_types || !base_types || !dec_scales)
    {
        free(col_names);
        free(col_types);
        free(base_types);
        free(dec_scales);

        format_error_message(
            errmsg,
            buf_size,
            "fetching data",
            NULL,
            ERR_MSG_ALLOC
        );
        goto fail;
    }

    chunklist->ncols = ncols;
    chunklist->col_names = col_names;
    chunklist->col_types = col_types;
    chunklist->base_types = base_types;
    chunklist->dec_scales = dec_scales;

    for (idx_t c=0; c < ncols; c++)
    {
        const char *col_name = DUCKDB_COLUMN_NAME(result, c);

        if (!col_name)
        {
            format_error_message(
                errmsg,
                buf_size,
                "fetching data",
                NULL,
                ERR_MSG_INVALID_COL_NAME
            );
            goto fail;
        }

        col_names[c] = col_name;

        duckdb_type type = DUCKDB_COLUMN_TYPE(result, c);

        col_types[c] = base_types[c] = type;

        switch (type)
        {
            case DUCKDB_TYPE_DECIMAL:
            {
                duckdb_logical_type lt = DUCKDB_COLUMN_LOGICAL_TYPE(result, c);

                if (!lt) {
                    format_error_message(
                        errmsg,
                        buf_size,
                        "fetching data",
                        NULL,
                        ERR_MSG_INTERNAL
                    );
                    goto fail;  
                }

                base_types[c] = DUCKDB_DECIMAL_INTERNAL_TYPE(lt);

                dec_scales[c] = DUCKDB_DECIMAL_SCALE(lt);

                DUCKDB_DESTROY_LOGICAL_TYPE(&lt);

                break;
            }

            case DUCKDB_TYPE_VARCHAR:
            case DUCKDB_TYPE_BOOLEAN:
            case DUCKDB_TYPE_TINYINT:
            case DUCKDB_TYPE_UTINYINT:
            case DUCKDB_TYPE_SMALLINT:
            case DUCKDB_TYPE_USMALLINT:
            case DUCKDB_TYPE_INTEGER:
            case DUCKDB_TYPE_UINTEGER:
            case DUCKDB_TYPE_BIGINT:
            case DUCKDB_TYPE_UBIGINT:
            case DUCKDB_TYPE_HUGEINT:
            case DUCKDB_TYPE_UHUGEINT:
            case DUCKDB_TYPE_FLOAT:
            case DUCKDB_TYPE_DOUBLE:
            case DUCKDB_TYPE_DATE:
            case DUCKDB_TYPE_TIME:
            case DUCKDB_TYPE_TIME_TZ:
            case DUCKDB_TYPE_TIMESTAMP:
            case DUCKDB_TYPE_TIMESTAMP_TZ:
            case DUCKDB_TYPE_TIMESTAMP_S:
            case DUCKDB_TYPE_TIMESTAMP_MS:
            case DUCKDB_TYPE_TIMESTAMP_NS:
                dec_scales[c] = 0;
                break;
            
            default:
                format_error_message(
                    errmsg,
                    buf_size,
                    "fetching data",
                    col_name,
                    ERR_MSG_UNSUPPORTED_TYPE
                );
                goto fail;
        }
    }

    for (;;) 
    {
        chunk_node *node = NULL;
        void **vectors = NULL;
        uint64_t **valid_masks = NULL;

        duckdb_data_chunk chunk = DUCKDB_FETCH_CHUNK(*result);

        if (!chunk) break;

        idx_t nrows = DUCKDB_DATA_CHUNK_GET_SIZE(chunk);

        idx_t row_total = chunklist->nrows + nrows;

        if (row_total > XL_MAX_ROW - 1)
        {
            format_error_message(
                errmsg,
                buf_size,
                "fetching data",
                NULL,
                ERR_MSG_EXCEED_SIZE_LIMIT
            );
            goto loop_fail;
        }

        node = malloc(sizeof(*node));
        vectors = malloc(ncols * sizeof(*vectors));
        valid_masks = malloc(ncols * sizeof(*valid_masks));

        if (!node || !vectors || !valid_masks)
        {
            format_error_message(
                errmsg,
                buf_size,
                "fetching data",
                NULL,
                ERR_MSG_ALLOC
            );
            goto loop_fail;
        }

        for (idx_t c=0; c < ncols; c++)
        {
            duckdb_vector vec = DUCKDB_DATA_CHUNK_GET_VECTOR(chunk, c);

            if (!vec)
            {
                format_error_message(
                    errmsg,
                    buf_size,
                    "fetching data",
                    NULL,
                    ERR_MSG_INTERNAL
                );
                goto loop_fail;
            }

            void *vec_data = DUCKDB_VECTOR_GET_DATA(vec);

            if (!vec_data)
            {
                format_error_message(
                    errmsg,
                    buf_size,
                    "fetching data",
                    NULL,
                    ERR_MSG_UNSUPPORTED_TYPE
                );
                goto loop_fail;
            }
    
            vectors[c] = vec_data;
            valid_masks[c] = DUCKDB_VECTOR_GET_VALIDITY(vec);
        }

        node->nrows = nrows;
        node->chunk = chunk;
        node->vectors = vectors;
        node->valid_masks = valid_masks;
        node->next = NULL;

        if (!chunklist->tail)
        {
            chunklist->head = chunklist->tail = node;
        }
        else
        {
            chunklist->tail->next = node;
            chunklist->tail = node;
        }

        chunklist->nrows = row_total;
        chunklist->nchunks++;

        continue;
    
    loop_fail:
        free(node);
        free(vectors);
        free(valid_masks);
        DUCKDB_DESTROY_DATA_CHUNK(&chunk);
        goto fail;
    }

    return 1;

fail:

    free_and_reset_chunk_list(chunklist);

    return 0;
}

#define TO_DUCKDB_TYPE_ENUM(X) DUCKDB_TYPE_##X
#define TO_FUNC_NAME(X, Y) X##_to_##Y
#define TO_DEC_FUNC_NAME(X, Y) X##_BASED_DEC_to_##Y

#define DEF_FUNC(lib_type, vector_type, xl_type, converter_codes)                   \
void TO_FUNC_NAME(lib_type, xl_type)(LPXLOPER12 cell, chunk_list *chunklist, idx_t col) \
{                                                                                   \
    idx_t ncols = chunklist->ncols;                                                 \
    for (chunk_node *node=chunklist->head; node!=NULL; node = node->next)           \
    {                                                                               \
        const vector_type *val = (const vector_type *)(node->vectors[col]);         \
        idx_t nrows = node->nrows;                                                  \
        const uint64_t *valid_mask = (const uint64_t *)(node->valid_masks[col]);    \
        if (!valid_mask)                                                            \
        {                                                                           \
            for (idx_t i = 0; i < nrows; i++, cell += ncols, val++)                 \
            {                                                                       \
                converter_codes;                                                    \
            }                                                                       \
        } else {                                                                    \
            for (idx_t base = 0; base < nrows; base += 64)                          \
            {                                                                       \
                uint64_t mask = valid_mask[base / 64];                              \
                idx_t count = nrows - base;                                         \
                if (count > 64)                                                     \
                    count = 64;                                                     \
                while (count--)                                                     \
                {                                                                   \
                    if (mask & 1ULL)                                                \
                    {                                                               \
                        converter_codes;                                            \
                    } else {                                                        \
                        cell->xltype = xltypeErr;                                   \
                        cell->val.err = xlerrNA;                                    \
                    }                                                               \
                    mask >>= 1;                                                     \
                    cell += ncols;                                                  \
                    val++;                                                          \
                }                                                                   \
            }                                                                       \
        }                                                                           \
    }                                                                               \
}

#define DEF_DECIMAL_FUNC(lib_type, vector_type, xl_type, converter_codes)           \
void TO_DEC_FUNC_NAME(lib_type, xl_type)(LPXLOPER12 cell, chunk_list *chunklist, idx_t col) \
{                                                                                   \
    idx_t ncols = chunklist->ncols;                                                 \
    double dec_divisor = DEC_DIVISORS[chunklist->dec_scales[col]];                  \
    for (chunk_node *node=chunklist->head; node!=NULL; node = node->next)           \
    {                                                                               \
        const vector_type *val = (const vector_type *)(node->vectors[col]);         \
        idx_t nrows = node->nrows;                                                  \
        const uint64_t *valid_mask = (const uint64_t *)(node->valid_masks[col]);    \
        if (!valid_mask)                                                            \
        {                                                                           \
            for (idx_t i = 0; i < nrows; i++, cell += ncols, val++)                 \
            {                                                                       \
                converter_codes(dec_divisor);                                       \
            }                                                                       \
        } else {                                                                    \
            for (idx_t base = 0; base < nrows; base += 64)                          \
            {                                                                       \
                uint64_t mask = valid_mask[base / 64];                              \
                idx_t count = nrows - base;                                         \
                if (count > 64)                                                     \
                    count = 64;                                                     \
                while (count--)                                                     \
                {                                                                   \
                    if (mask & 1ULL)                                                \
                    {                                                               \
                        converter_codes(dec_divisor);                               \
                    } else {                                                        \
                        cell->xltype = xltypeErr;                                   \
                        cell->val.err = xlerrNA;                                    \
                    }                                                               \
                    mask >>= 1;                                                     \
                    cell += ncols;                                                  \
                    val++;                                                          \
                }                                                                   \
            }                                                                       \
        }                                                                           \
    }                                                                               \
}

#define INT_TO_DBL \
    do { \
        cell->xltype = xltypeNum; \
        cell->val.num = (double)*val; \
    } while (0)

#define HINT_TO_DBL \
    do { \
        cell->xltype = xltypeNum; \
        cell->val.num = DUCKDB_HUGEINT_TO_DOUBLE(*val); \
    } while (0)

#define UHINT_TO_DBL \
    do { \
        cell->xltype = xltypeNum; \
        cell->val.num = DUCKDB_UHUGEINT_TO_DOUBLE(*val); \
    } while (0)

#define FLOAT_TO_DBL \
    do { \
        if (isfinite(*val)) { \
            cell->xltype = xltypeNum; \
            cell->val.num = (double)*val; \
        } else { \
            cell->xltype = xltypeErr; \
            cell->val.err = xlerrNum; \
        } \
    } while (0)

#define BOOL_TO_BOOL \
    do { \
        cell->xltype = xltypeBool; \
        cell->val.xbool = *val; \
    } while (0)

#define STR_TO_STR \
    do { \
        uint32_t sz = DUCKDB_STRING_T_LENGTH(*val); \
        const char *src = DUCKDB_STRING_T_DATA((duckdb_string_t *)val); \
        wchar_t *dest = NULL; \
        if (!src) { \
            cell->xltype = xltypeErr; \
            cell->val.err = xlerrNA; \
        } else if (utf8_to_xlstr(&dest, src, sz) != 0 && dest) { \
            cell->xltype = xltypeStr | xlbitDLLFree; \
            cell->val.str = dest; \
        } else { \
            cell->xltype = xltypeErr; \
            cell->val.err = xlerrValue; \
        } \
    } while (0)

#define DEC_TO_DBL(divisor) \
    do { \
        cell->xltype = xltypeNum; \
        cell->val.num = (double)*val / divisor; \
    } while (0)

#define DEC128_TO_DBL(divisor) \
    do { \
        cell->xltype = xltypeNum; \
        cell->val.num = DUCKDB_HUGEINT_TO_DOUBLE(*val) / divisor; \
    } while (0)

#define DATE_TO_DBL \
    do { \
        cell->xltype = xltypeNum; \
        cell->val.num = (double)val->days + EPOCH_DELTA; \
    } while (0)

#define TIME_TO_DBL \
    do { \
        cell->xltype = xltypeNum; \
        cell->val.num = (double)val->micros / US_PER_DAY; \
    } while (0)

#define TIME_TZ_TO_DBL \
    do { \
        cell->xltype = xltypeNum; \
        cell->val.num = (double)(val->bits >> TIMETZ_OFFSET_BITS) / US_PER_DAY; \
    } while (0)

#define TIMESTAMP_S_TO_DBL \
    do { \
        cell->xltype = xltypeNum; \
        cell->val.num = ((double)val->seconds / S_PER_DAY) + EPOCH_DELTA; \
    } while (0)

#define TIMESTAMP_MS_TO_DBL \
    do { \
        cell->xltype = xltypeNum; \
        cell->val.num = ((double)val->millis / MS_PER_DAY) + EPOCH_DELTA; \
    } while (0)    

#define TIMESTAMP_TO_DBL \
    do { \
        cell->xltype = xltypeNum; \
        cell->val.num = ((double)val->micros / US_PER_DAY) + EPOCH_DELTA; \
    } while (0) 

#define TIMESTAMP_NS_TO_DBL \
    do { \
        cell->xltype = xltypeNum; \
        cell->val.num = ((double)val->nanos / NS_PER_DAY) + EPOCH_DELTA; \
    } while (0)

#define INT_CONVERTERS(X) \
X(TINYINT, int8_t, XLNUM, INT_TO_DBL) \
X(UTINYINT, uint8_t, XLNUM, INT_TO_DBL) \
X(SMALLINT, int16_t, XLNUM, INT_TO_DBL) \
X(USMALLINT, uint16_t, XLNUM, INT_TO_DBL) \
X(INTEGER, int32_t, XLNUM, INT_TO_DBL) \
X(UINTEGER, uint32_t, XLNUM, INT_TO_DBL) \
X(BIGINT, int64_t, XLNUM, INT_TO_DBL) \
X(UBIGINT, uint64_t, XLNUM, INT_TO_DBL) \
X(HUGEINT, duckdb_hugeint, XLNUM, HINT_TO_DBL) \
X(UHUGEINT, duckdb_uhugeint, XLNUM, UHINT_TO_DBL)

#define BOOL_CONVERTERS(X) \
X(BOOLEAN, bool, XLBOOL, BOOL_TO_BOOL)

#define FLOAT_CONVERTERS(X) \
X(FLOAT, float, XLNUM, FLOAT_TO_DBL) \
X(DOUBLE, double, XLNUM, FLOAT_TO_DBL)

#define STRING_CONVERTERS(X) \
X(VARCHAR, duckdb_string_t, XLSTR, STR_TO_STR)

#define DECIMAL_CONVERTERS(X) \
X(SMALLINT, int16_t, XLNUM, DEC_TO_DBL) \
X(INTEGER, int32_t, XLNUM, DEC_TO_DBL) \
X(BIGINT, int64_t, XLNUM, DEC_TO_DBL) \
X(HUGEINT, duckdb_hugeint, XLNUM, DEC128_TO_DBL)

#define DATE_TIME_CONVERTERS(X) \
X(DATE, duckdb_date, XLNUM, DATE_TO_DBL) \
X(TIME, duckdb_time, XLNUM, TIME_TO_DBL) \
X(TIME_TZ, duckdb_time_tz, XLNUM, TIME_TZ_TO_DBL) \
X(TIMESTAMP_S, duckdb_timestamp_s, XLNUM, TIMESTAMP_S_TO_DBL) \
X(TIMESTAMP_MS, duckdb_timestamp_ms, XLNUM, TIMESTAMP_MS_TO_DBL) \
X(TIMESTAMP, duckdb_timestamp, XLNUM, TIMESTAMP_TO_DBL) \
X(TIMESTAMP_NS, duckdb_timestamp_ns, XLNUM, TIMESTAMP_NS_TO_DBL) \
X(TIMESTAMP_TZ, duckdb_timestamp, XLNUM, TIMESTAMP_TO_DBL)

#define NON_DEC_CONVERTERS(X) \
STRING_CONVERTERS(X) \
INT_CONVERTERS(X) \
BOOL_CONVERTERS(X) \
FLOAT_CONVERTERS(X) \
DATE_TIME_CONVERTERS(X)

NON_DEC_CONVERTERS(DEF_FUNC)
DECIMAL_CONVERTERS(DEF_DECIMAL_FUNC)

LPXLOPER12 chunks_to_range(chunk_list *chunklist) 
{
    char errmsg[ERR_MSG_MAX_LEN];
    errmsg[0] = '\0';
    LPXLOPER12 lparray = NULL;

    LPXLOPER12 range = NULL;

    if (!chunklist)
    {
        format_error_message(
            errmsg,
            sizeof(errmsg),
            "writing output",
            NULL,
            ERR_MSG_INTERNAL
        );
        goto fail;
    }

    size_t ncols = (size_t)chunklist->ncols;
    size_t nrows = (size_t)chunklist->nrows;

    if (ncols > XL_MAX_COL || nrows > XL_MAX_ROW - 1)
    {
        format_error_message(
            errmsg,
            sizeof(errmsg),
            "writing output",
            NULL,
            ERR_MSG_EXCEED_SIZE_LIMIT
        );
        goto fail;
    }

    range = calloc(1, sizeof(*range));

    if (!range)
    {
        format_error_message(
            errmsg,
            sizeof(errmsg),
            "writing output",
            NULL,
            ERR_MSG_ALLOC
        );
        goto fail;
    }

    // Transfer ownership via xlbitDLLFree 
    range->xltype = xltypeMulti | xlbitDLLFree;

    lparray = calloc(ncols*(nrows + 1), sizeof(*lparray));
    if (!lparray)
    {
        format_error_message(
            errmsg,
            sizeof(errmsg),
            "writing output",
            NULL,
            ERR_MSG_ALLOC
        );
        goto fail;
    }

    LPXLOPER12 cell = lparray;

    const char **col_names = chunklist->col_names;

    for (size_t c=0; c < ncols; c++, cell++)
    {
        const char *col_name = col_names[c];

        if (!col_name)
        {
            format_error_message(
                errmsg,
                sizeof(errmsg),
                "writing output",
                NULL,
                ERR_MSG_INVALID_COL_NAME
            );
            goto rollback;
        }

        wchar_t *xlstr = NULL; 

        if (utf8_to_xlstr(&xlstr, col_name, -1) == 0 || !xlstr)
        {
            format_error_message(
                errmsg,
                sizeof(errmsg),
                "writing output",
                col_name,
                ERR_MSG_INVALID_COL_NAME
            );
            goto rollback;
        }

        // Transfer ownership via xlbitDLLFree 
        cell->xltype = xltypeStr | xlbitDLLFree;
        cell->val.str = xlstr;

        continue;

    rollback:

        xloper12_free_array(lparray, c);

        lparray = NULL;

        goto fail;
    }

    for (size_t c=0; c < ncols; c++, cell++)
    {
        switch (chunklist->col_types[c])
        {
            #define MAPPING(lib_type, vector_type, xl_type, converter_codes) \
            case TO_DUCKDB_TYPE_ENUM(lib_type): \
                TO_FUNC_NAME(lib_type, xl_type)(cell, chunklist, c); \
                break;
            NON_DEC_CONVERTERS(MAPPING)

            case DUCKDB_TYPE_DECIMAL:
                switch (chunklist->base_types[c]) 
                {
                    #define MAPPING_DECIMAL(lib_type, vector_type, xl_type, converter_codes) \
                    case TO_DUCKDB_TYPE_ENUM(lib_type): \
                        TO_DEC_FUNC_NAME(lib_type, xl_type)(cell, chunklist, c); \
                        break;
                    DECIMAL_CONVERTERS(MAPPING_DECIMAL)

                    default:
                        cell->xltype = xltypeErr;
                        cell->val.err = xlerrValue;
                        break;
                }
                break;

        default:
            cell->xltype = xltypeErr;
            cell->val.err = xlerrValue;
            break;
        }
    }

    range->val.array.lparray = lparray;
    lparray = NULL;

    range->val.array.rows = (RW)(nrows + 1);   // +1 for header
    range->val.array.columns = (COL)ncols;

    return range;

fail:

    free(lparray);
    xloper12_free(range);

    return make_string_cell(
        errmsg[0] ? errmsg : ERR_MSG_INTERNAL
    );
}
