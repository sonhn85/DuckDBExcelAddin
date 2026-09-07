#include "db_lib_loader.h"
#include "config.h"
#include "helper.h"
#include "db_scalar_funcs.h"

#include <math.h>

#define ERR_MSG_INTERNAL        "An internal error occurred."

#define XLTYPENUM_TO_DUCKDB_DATE(X, Y) \
    do { \
        (X)->days = (int32_t)floor(*(Y)) - EPOCH_DELTA; \
    } while(0)

#define XLTYPENUM_TO_DUCKDB_TIME(X, Y) \
    do { \
        double day_fraction = *(Y) - floor(*(Y)); \
        (X)->micros = (int64_t)llround(day_fraction * US_PER_DAY); \
    } while(0)

#define XLTYPENUM_TO_DUCKDB_TIMESTAMP(X, Y) \
    do { \
        (X)->micros = (int64_t)llround((*(Y) - EPOCH_DELTA) * US_PER_DAY); \
    } while(0)

/*
 * DuckDB stores validity information as a bitmap.
 * Each uint64_t represents the validity of up to 64 rows.
 * Process one bitmap word at a time for efficient NULL propagation.
 */
#define DEFINE_SCAN_FUNCTION(FUNCTION_NAME, TYPE_ENUM, TYPE, CONVERTER)     \
static void FUNCTION_NAME(                                                  \
    duckdb_function_info info,                                              \
    duckdb_data_chunk input,                                                \
    duckdb_vector output)                                                   \
{                                                                           \
    duckdb_vector in_vec = DUCKDB_DATA_CHUNK_GET_VECTOR(input, 0);          \
    if (!in_vec)                                                            \
    {                                                                       \
        DUCKDB_SCALAR_FUNCTION_SET_ERROR(info, ERR_MSG_INTERNAL);           \
        return;                                                             \
    }                                                                       \
                                                                            \
    double *in_vec_data = (double *)DUCKDB_VECTOR_GET_DATA(in_vec);         \
    if (!in_vec_data)                                                       \
    {                                                                       \
        DUCKDB_SCALAR_FUNCTION_SET_ERROR(info, ERR_MSG_INTERNAL);           \
        return;                                                             \
    }                                                                       \
                                                                            \
    TYPE *out_vec_data = DUCKDB_VECTOR_GET_DATA(output);                    \
    if (!out_vec_data)                                                      \
    {                                                                       \
        DUCKDB_SCALAR_FUNCTION_SET_ERROR(info, ERR_MSG_INTERNAL);           \
        return;                                                             \
    }                                                                       \
                                                                            \
    idx_t nrows = DUCKDB_DATA_CHUNK_GET_SIZE(input);                        \
                                                                            \
    uint64_t *in_validity = DUCKDB_VECTOR_GET_VALIDITY(in_vec);             \
                                                                            \
    if (!in_validity)                                                       \
    {                                                                       \
        for (idx_t i = 0; i < nrows; i++, in_vec_data++, out_vec_data++)    \
        {                                                                   \
            CONVERTER(out_vec_data, in_vec_data);                           \
        }                                                                   \
    }                                                                       \
    else                                                                    \
    {                                                                       \
        DUCKDB_VECTOR_ENSURE_VALIDITY_WRITABLE(output);                     \
                                                                            \
        uint64_t *out_validity = DUCKDB_VECTOR_GET_VALIDITY(output);        \
                                                                            \
        for (idx_t base = 0; base < nrows; base += 64)                      \
        {                                                                   \
            uint64_t mask = in_validity[base / 64];                         \
                                                                            \
            idx_t count = nrows - base;                                     \
                                                                            \
            if (count > 64)                                                 \
                count = 64;                                                 \
                                                                            \
            for (idx_t i = 0; i < count; i++, in_vec_data++, out_vec_data++, mask >>= 1) \
            {                                                               \
                if (mask & 1ULL)                                            \
                {                                                           \
                    CONVERTER(out_vec_data, in_vec_data);                   \
                }                                                           \
                else                                                        \
                {                                                           \
                    DUCKDB_VALIDITY_SET_ROW_INVALID(                        \
                        out_validity,                                       \
                        base + i                                            \
                    );                                                      \
                }                                                           \
            }                                                               \
        }                                                                   \
    }                                                                       \
}

SCALAR_FUNCTIONS(DEFINE_SCAN_FUNCTION)

#define DEFINE_REGISTER_FUNCTION(FUNCTION_NAME, TYPE_ENUM, TYPE, CONVERTER) \
REGISTER_FUNCTION_SIGNATURE(FUNCTION_NAME)                                  \
{                                                                           \
    if (!con || !function)                                                  \
        return 0;                                                           \
                                                                            \
    *function = NULL;                                                       \
                                                                            \
    duckdb_scalar_function scalar_func = NULL;                              \
    duckdb_logical_type in_type = NULL;                                     \
    duckdb_logical_type out_type = NULL;                                    \
                                                                            \
    int res = 0;                                                            \
                                                                            \
    in_type = DUCKDB_CREATE_LOGICAL_TYPE(DUCKDB_TYPE_DOUBLE);               \
    out_type = DUCKDB_CREATE_LOGICAL_TYPE(TYPE_ENUM);                       \
                                                                            \
    if (!in_type || !out_type)                                              \
        goto fail;                                                          \
                                                                            \
    scalar_func = DUCKDB_CREATE_SCALAR_FUNCTION();                          \
                                                                            \
    if (!scalar_func)                                                       \
        goto fail;                                                          \
                                                                            \
    DUCKDB_SCALAR_FUNCTION_SET_NAME(scalar_func, TO_STR(FUNCTION_NAME));    \
    DUCKDB_SCALAR_FUNCTION_ADD_PARAMETER(scalar_func, in_type);             \
    DUCKDB_SCALAR_FUNCTION_SET_RETURN_TYPE(scalar_func, out_type);          \
    DUCKDB_SCALAR_FUNCTION_SET_FUNCTION(scalar_func, FUNCTION_NAME);        \
                                                                            \
    if (DUCKDB_REGISTER_SCALAR_FUNCTION(con, scalar_func) != DuckDBSuccess) \
        goto fail;                                                          \
                                                                            \
    *function = scalar_func;                                                \
    res = 1;                                                                \
                                                                            \
    goto cleanup;                                                           \
                                                                            \
fail:                                                                       \
                                                                            \
    *function = NULL;                                                       \
    res = 0;                                                                \
                                                                            \
    if (scalar_func)                                                        \
        DUCKDB_DESTROY_SCALAR_FUNCTION(&scalar_func);                       \
                                                                            \
cleanup:                                                                    \
    if (in_type)                                                            \
        DUCKDB_DESTROY_LOGICAL_TYPE(&in_type);                              \
                                                                            \
    if (out_type)                                                           \
        DUCKDB_DESTROY_LOGICAL_TYPE(&out_type);                             \
                                                                            \
    return res;                                                             \
}

SCALAR_FUNCTIONS(DEFINE_REGISTER_FUNCTION)
