/*
 * DuckDB scalar functions for converting Excel serial date/time
 * values to DuckDB temporal types.
 *
 * Supports both DOUBLE and INTEGER inputs. INTEGER values are
 * accepted when type inference selects INTEGER.
 *
 *   xldate(DOUBLE or INTEGER)     -> DATE
 *   xltime(DOUBLE or INTEGER)     -> TIME
 *   xldatetime(DOUBLE or INTEGER) -> TIMESTAMP
 *
 * Uses the Excel 1900 date system.
 */
#ifndef DB_SCALAR_FUNCTIONS_H
#define DB_SCALAR_FUNCTIONS_H

#include "duckdb.h"

/*
 * Function registry definition.
 *
 * Each entry contains:
 *   (scalar function name, result type, vector type, 1st type of param, C type, converter macro, 2nd type of param, C type, converter macro)
 */
#define SCALAR_FUNCTIONS(X) \
X(xldate,     DUCKDB_TYPE_DATE,      duckdb_date,      DUCKDB_TYPE_DOUBLE, double, XLTYPENUM_TO_DUCKDB_DATE,      DUCKDB_TYPE_INTEGER, int32_t, XLTYPEINT_TO_DUCKDB_DATE) \
X(xltime,     DUCKDB_TYPE_TIME,      duckdb_time,      DUCKDB_TYPE_DOUBLE, double, XLTYPENUM_TO_DUCKDB_TIME,      DUCKDB_TYPE_INTEGER, int32_t, XLTYPEINT_TO_DUCKDB_TIME) \
X(xldatetime, DUCKDB_TYPE_TIMESTAMP, duckdb_timestamp, DUCKDB_TYPE_DOUBLE, double, XLTYPENUM_TO_DUCKDB_TIMESTAMP, DUCKDB_TYPE_INTEGER, int32_t, XLTYPEINT_TO_DUCKDB_TIMESTAMP)

#define REGISTER_FUNCTION_NAME(FUNCTION_NAME) register_##FUNCTION_NAME

#define REGISTER_FUNCTION_SIGNATURE(FUNCTION_NAME) \
int REGISTER_FUNCTION_NAME(FUNCTION_NAME)(duckdb_connection con, duckdb_scalar_function_set *func_set)

/* Generate registration function declarations from SCALAR_FUNCTIONS. */
#define DECLARE_REGISTER_FUNCTION(FUNCTION_NAME, IGNORE_1, IGNORE_2, IGNORE_3, IGNORE_4, IGNORE_5, IGNORE_6, IGNORE_7, IGNORE_8) \
REGISTER_FUNCTION_SIGNATURE(FUNCTION_NAME);

SCALAR_FUNCTIONS(DECLARE_REGISTER_FUNCTION)

#undef DECLARE_REGISTER_FUNCTION

#endif /* DB_SCALAR_FUNCTIONS_H */
