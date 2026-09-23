#include "db_lib_loader.h"
#include "config.h"
#include "helper.h"
#include "db_scalar_funcs.h"

#include <math.h>

#define ERR_MSG_INTERNAL        "An internal error occurred."
#define ERR_EXCEL_DATE_TIME     "Excel serial date or time is out of range."

/* Convert Excel serial values to DuckDB temporal values. */

#define XLTYPEINT_TO_DUCKDB_DATE(X, Y) \
    do { \
        int64_t days = (int64_t)*(Y) - (int64_t)EPOCH_DELTA; \
        if (days < INT32_MIN || days > INT32_MAX) \
        { \
            DUCKDB_SCALAR_FUNCTION_SET_ERROR( \
                info, \
                ERR_EXCEL_DATE_TIME \
            ); \
            return; \
        } \
        (X)->days = (int32_t)days; \
    } while (0)

#define XLTYPEINT_TO_DUCKDB_TIME(X, IGNORE) \
    do { \
        (X)->micros = 0; \
    } while(0)

#define XLTYPEINT_TO_DUCKDB_TIMESTAMP(X, Y) \
    do { \
        int64_t days = (int64_t)*(Y) - (int64_t)EPOCH_DELTA; \
        if (days < INT64_MIN / US_PER_DAY \
            || days > INT64_MAX / US_PER_DAY) \
        { \
            DUCKDB_SCALAR_FUNCTION_SET_ERROR( \
                info, \
                ERR_EXCEL_DATE_TIME \
            ); \
            return; \
        } \
        (X)->micros = days * US_PER_DAY; \
    } while (0)

#define XLTYPENUM_TO_DUCKDB_DATE(X, Y) \
    do { \
        double d = *(Y); \
        double days = floor(d) - (double)EPOCH_DELTA; \
        if (!isfinite(days) \
            || days < (double)INT32_MIN \
            || days > (double)INT32_MAX) \
        { \
            DUCKDB_SCALAR_FUNCTION_SET_ERROR( \
                info, \
                ERR_EXCEL_DATE_TIME \
            ); \
            return; \
        } \
        (X)->days = (int32_t)days; \
    } while (0)

#define XLTYPENUM_TO_DUCKDB_TIME(X, Y) \
    do { \
        double d = *(Y); \
        if (!isfinite(d)) \
        { \
            DUCKDB_SCALAR_FUNCTION_SET_ERROR( \
                info, \
                ERR_EXCEL_DATE_TIME \
            ); \
            return; \
        } \
        double day_fraction = d - floor(d); \
        int64_t micros = (int64_t)llround( \
            day_fraction * US_PER_DAY \
        ); \
        if (micros >= US_PER_DAY) \
            micros = US_PER_DAY - 1; \
        (X)->micros = micros; \
    } while (0)

#define XLTYPENUM_TO_DUCKDB_TIMESTAMP(X, Y) \
    do { \
        double micros = \
            (*(Y) - (double)EPOCH_DELTA) * (double)US_PER_DAY; \
        if (!isfinite(micros) \
            || micros < -9223372036854775808.0 \
            || micros >= 9223372036854775808.0) \
        { \
            DUCKDB_SCALAR_FUNCTION_SET_ERROR( \
                info, \
                ERR_EXCEL_DATE_TIME \
            ); \
            return; \
        } \
        (X)->micros = (int64_t)llround(micros); \
    } while (0)

#define SCAN_FUNCTION_NAME(FUNCTION_NAME, PARAM_C_TYPE) FUNCTION_NAME##_with_##PARAM_C_TYPE

/*
 * Generate a vectorized conversion function.
 * Input NULLs are propagated to the output validity mask.
 */
#define DEFINE_SCAN_FUNCTION(FUNCTION_NAME, RESULT_TYPE, VECTOR_TYPE, PARAM_C_TYPE, CONVERTER)  \
static void SCAN_FUNCTION_NAME(FUNCTION_NAME, PARAM_C_TYPE)(                                    \
    duckdb_function_info info,                                                                  \
    duckdb_data_chunk input,                                                                    \
    duckdb_vector output)                                                                       \
{                                                                                               \
    duckdb_vector in_vec = DUCKDB_DATA_CHUNK_GET_VECTOR(input, 0);                              \
    if (!in_vec)                                                                                \
    {                                                                                           \
        DUCKDB_SCALAR_FUNCTION_SET_ERROR(info, ERR_MSG_INTERNAL);                               \
        return;                                                                                 \
    }                                                                                           \
                                                                                                \
    PARAM_C_TYPE *in_vec_data = (PARAM_C_TYPE *)DUCKDB_VECTOR_GET_DATA(in_vec);                 \
    if (!in_vec_data)                                                                           \
    {                                                                                           \
        DUCKDB_SCALAR_FUNCTION_SET_ERROR(info, ERR_MSG_INTERNAL);                               \
        return;                                                                                 \
    }                                                                                           \
                                                                                                \
    VECTOR_TYPE *out_vec_data = DUCKDB_VECTOR_GET_DATA(output);                                 \
    if (!out_vec_data)                                                                          \
    {                                                                                           \
        DUCKDB_SCALAR_FUNCTION_SET_ERROR(info, ERR_MSG_INTERNAL);                               \
        return;                                                                                 \
    }                                                                                           \
                                                                                                \
    idx_t nrows = DUCKDB_DATA_CHUNK_GET_SIZE(input);                                            \
                                                                                                \
    uint64_t *in_validity = DUCKDB_VECTOR_GET_VALIDITY(in_vec);                                 \
                                                                                                \
    if (!in_validity)                                                                           \
    {                                                                                           \
        for (idx_t i = 0; i < nrows; i++, in_vec_data++, out_vec_data++)                        \
        {                                                                                       \
            CONVERTER(out_vec_data, in_vec_data);                                               \
        }                                                                                       \
    }                                                                                           \
    else                                                                                        \
    {                                                                                           \
        DUCKDB_VECTOR_ENSURE_VALIDITY_WRITABLE(output);                                         \
                                                                                                \
        uint64_t *out_validity = DUCKDB_VECTOR_GET_VALIDITY(output);                            \
                                                                                                \
        for (idx_t base = 0; base < nrows; base += 64)                                          \
        {                                                                                       \
            uint64_t mask = in_validity[base / 64];                                             \
                                                                                                \
            idx_t count = nrows - base;                                                         \
                                                                                                \
            if (count > 64)                                                                     \
                count = 64;                                                                     \
                                                                                                \
            for (idx_t i = 0; i < count; i++, in_vec_data++, out_vec_data++, mask >>= 1)        \
            {                                                                                   \
                if (mask & 1ULL)                                                                \
                {                                                                               \
                    CONVERTER(out_vec_data, in_vec_data);                                       \
                }                                                                               \
                else                                                                            \
                {                                                                               \
                    DUCKDB_VALIDITY_SET_ROW_INVALID(                                            \
                        out_validity,                                                           \
                        base + i                                                                \
                    );                                                                          \
                }                                                                               \
            }                                                                                   \
        }                                                                                       \
    }                                                                                           \
}

#define DEFINE_SCAN_FUNCTION_ALL_PARAM_TYPES(FUNCTION_NAME, RESULT_TYPE, VECTOR_TYPE, PARAM_TYPE_1, PARAM_C_TYPE_1, CONVERTER_1, PARAM_TYPE_2, PARAM_C_TYPE_2, CONVERTER_2) \
DEFINE_SCAN_FUNCTION(FUNCTION_NAME, RESULT_TYPE, VECTOR_TYPE, PARAM_C_TYPE_1, CONVERTER_1) \
DEFINE_SCAN_FUNCTION(FUNCTION_NAME, RESULT_TYPE, VECTOR_TYPE, PARAM_C_TYPE_2, CONVERTER_2)

/* Generate scan functions for DOUBLE and INTEGER inputs. */
SCALAR_FUNCTIONS(DEFINE_SCAN_FUNCTION_ALL_PARAM_TYPES)

/*
 * Register both input overloads as one DuckDB scalar function set.
 * On success, ownership of the function set is returned to the caller.
 */
#define DEFINE_REGISTER_FUNCTION(FUNCTION_NAME, RESULT_TYPE, IGNORE_1, PARAM_TYPE_1, PARAM_C_TYPE_1, IGNORE_2, PARAM_TYPE_2, PARAM_C_TYPE_2, IGNORE_3)  \
REGISTER_FUNCTION_SIGNATURE(FUNCTION_NAME)                                                                  \
{                                                                                                           \
    if (!con || !func_set)                                                                                  \
        return 0;                                                                                           \
                                                                                                            \
    *func_set = NULL;                                                                                       \
    duckdb_scalar_function_set func_set_tmp = NULL;                                                         \
    duckdb_scalar_function scalar_func_1 = NULL;                                                            \
    duckdb_scalar_function scalar_func_2 = NULL;                                                            \
    duckdb_logical_type in_type_1 = NULL;                                                                   \
    duckdb_logical_type in_type_2 = NULL;                                                                   \
    duckdb_logical_type out_type = NULL;                                                                    \
                                                                                                            \
    int res = 0;                                                                                            \
                                                                                                            \
    func_set_tmp = DUCKDB_CREATE_SCALAR_FUNCTION_SET(TO_STR(FUNCTION_NAME));                                \
    scalar_func_1 = DUCKDB_CREATE_SCALAR_FUNCTION();                                                        \
    scalar_func_2 = DUCKDB_CREATE_SCALAR_FUNCTION();                                                        \
    in_type_1 = DUCKDB_CREATE_LOGICAL_TYPE(PARAM_TYPE_1);                                                   \
    in_type_2 = DUCKDB_CREATE_LOGICAL_TYPE(PARAM_TYPE_2);                                                   \
    out_type = DUCKDB_CREATE_LOGICAL_TYPE(RESULT_TYPE);                                                     \
                                                                                                            \
    if (!func_set_tmp || !scalar_func_1 || !scalar_func_2 || !in_type_1 || !in_type_2 || !out_type)         \
        goto fail;                                                                                          \
                                                                                                            \
    DUCKDB_SCALAR_FUNCTION_SET_NAME(scalar_func_1, TO_STR(FUNCTION_NAME));                                  \
    DUCKDB_SCALAR_FUNCTION_ADD_PARAMETER(scalar_func_1, in_type_1);                                         \
    DUCKDB_SCALAR_FUNCTION_SET_RETURN_TYPE(scalar_func_1, out_type);                                        \
    DUCKDB_SCALAR_FUNCTION_SET_FUNCTION(scalar_func_1, SCAN_FUNCTION_NAME(FUNCTION_NAME, PARAM_C_TYPE_1));  \
                                                                                                            \
    DUCKDB_SCALAR_FUNCTION_SET_NAME(scalar_func_2, TO_STR(FUNCTION_NAME));                                  \
    DUCKDB_SCALAR_FUNCTION_ADD_PARAMETER(scalar_func_2, in_type_2);                                         \
    DUCKDB_SCALAR_FUNCTION_SET_RETURN_TYPE(scalar_func_2, out_type);                                        \
    DUCKDB_SCALAR_FUNCTION_SET_FUNCTION(scalar_func_2, SCAN_FUNCTION_NAME(FUNCTION_NAME, PARAM_C_TYPE_2));  \
                                                                                                            \
    if (DUCKDB_ADD_SCALAR_FUNCTION_TO_SET(func_set_tmp, scalar_func_1) != DuckDBSuccess)                    \
        goto fail;                                                                                          \
                                                                                                            \
    scalar_func_1 = NULL;                                                                                   \
                                                                                                            \
    if (DUCKDB_ADD_SCALAR_FUNCTION_TO_SET(func_set_tmp, scalar_func_2) != DuckDBSuccess)                    \
        goto fail;                                                                                          \
                                                                                                            \
    scalar_func_2 = NULL;                                                                                   \
                                                                                                            \
    if (DUCKDB_REGISTER_SCALAR_FUNCTION_SET(con, func_set_tmp) != DuckDBSuccess)                            \
        goto fail;                                                                                          \
                                                                                                            \
    *func_set = func_set_tmp;                                                                               \
    res = 1;                                                                                                \
                                                                                                            \
    goto cleanup;                                                                                           \
                                                                                                            \
fail:                                                                                                       \
                                                                                                            \
    *func_set = NULL;                                                                                       \
    res = 0;                                                                                                \
                                                                                                            \
    if (func_set_tmp)                                                                                       \
        DUCKDB_DESTROY_SCALAR_FUNCTION_SET(&func_set_tmp);                                                  \
                                                                                                            \
    if (scalar_func_1)                                                                                      \
        DUCKDB_DESTROY_SCALAR_FUNCTION(&scalar_func_1);                                                     \
                                                                                                            \
    if (scalar_func_2)                                                                                      \
        DUCKDB_DESTROY_SCALAR_FUNCTION(&scalar_func_2);                                                     \
                                                                                                            \
cleanup:                                                                                                    \
    if (in_type_1)                                                                                          \
        DUCKDB_DESTROY_LOGICAL_TYPE(&in_type_1);                                                            \
                                                                                                            \
    if (in_type_2)                                                                                          \
        DUCKDB_DESTROY_LOGICAL_TYPE(&in_type_2);                                                            \
                                                                                                            \
    if (out_type)                                                                                           \
        DUCKDB_DESTROY_LOGICAL_TYPE(&out_type);                                                             \
                                                                                                            \
    return res;                                                                                             \
}

SCALAR_FUNCTIONS(DEFINE_REGISTER_FUNCTION)
