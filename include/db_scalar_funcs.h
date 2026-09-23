/*
 * Scalar functions for converting Excel serial values to DuckDB
 * DATE, TIME, and TIMESTAMP values.
 *
 * Supports DOUBLE and INTEGER inputs using the Excel 1900 date system.
 */
#ifndef DB_SCALAR_FUNCTIONS_H
#define DB_SCALAR_FUNCTIONS_H

#include "duckdb.h"

/*
* Scalar function definitions:
* name, result type, result C type, and two input overloads.
*/
#define SCALAR_FUNCTIONS(X) \
X(xldate,     DUCKDB_TYPE_DATE,      duckdb_date,      DUCKDB_TYPE_DOUBLE, double, XLTYPENUM_TO_DUCKDB_DATE,      DUCKDB_TYPE_INTEGER, int32_t, XLTYPEINT_TO_DUCKDB_DATE) \
X(xltime,     DUCKDB_TYPE_TIME,      duckdb_time,      DUCKDB_TYPE_DOUBLE, double, XLTYPENUM_TO_DUCKDB_TIME,      DUCKDB_TYPE_INTEGER, int32_t, XLTYPEINT_TO_DUCKDB_TIME) \
X(xldatetime, DUCKDB_TYPE_TIMESTAMP, duckdb_timestamp, DUCKDB_TYPE_DOUBLE, double, XLTYPENUM_TO_DUCKDB_TIMESTAMP, DUCKDB_TYPE_INTEGER, int32_t, XLTYPEINT_TO_DUCKDB_TIMESTAMP)

#define REGISTER_FUNCTION_NAME(FUNCTION_NAME) register_##FUNCTION_NAME

#define REGISTER_FUNCTION_SIGNATURE(FUNCTION_NAME) \
int REGISTER_FUNCTION_NAME(FUNCTION_NAME)(duckdb_connection con, duckdb_scalar_function_set *func_set)

/* Generate registration declarations for all scalar functions. */
#define DECLARE_REGISTER_FUNCTION(FUNCTION_NAME, IGNORE_1, IGNORE_2, IGNORE_3, IGNORE_4, IGNORE_5, IGNORE_6, IGNORE_7, IGNORE_8) \
REGISTER_FUNCTION_SIGNATURE(FUNCTION_NAME);

SCALAR_FUNCTIONS(DECLARE_REGISTER_FUNCTION)

#undef DECLARE_REGISTER_FUNCTION

#endif /* DB_SCALAR_FUNCTIONS_H */
