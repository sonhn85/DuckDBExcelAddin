/*
 * DuckDB scalar functions for converting Excel serial date/time
 * values (stored as DOUBLE) into native DuckDB temporal types.
 *
 * Supported conversions:
 *   xldate(double or int)     -> DATE
 *   xltime(double or int)     -> TIME
 *   xldatetime(double or int) -> TIMESTAMP
 *
 * Excel values are interpreted using the standard Excel 1900
 * date system.
 */
#ifndef DB_SCALAR_FUNCTIONS_H
#define DB_SCALAR_FUNCTIONS_H

#include "duckdb.h"

/*
 * Function registry definition.
 *
 * Each entry contains:
 *   (function name, duckdb type id, duckdb struct type, converter macro)
 */
#define SCALAR_FUNCTIONS(X) \
X(xldate, DUCKDB_TYPE_DATE, duckdb_date, XLTYPENUM_TO_DUCKDB_DATE, XLTYPEINT_TO_DUCKDB_DATE) \
X(xltime, DUCKDB_TYPE_TIME, duckdb_time, XLTYPENUM_TO_DUCKDB_TIME, XLTYPEINT_TO_DUCKDB_TIME) \
X(xldatetime, DUCKDB_TYPE_TIMESTAMP, duckdb_timestamp, XLTYPENUM_TO_DUCKDB_TIMESTAMP, XLTYPEINT_TO_DUCKDB_TIMESTAMP)

#define REGISTER_FUNCTION_NAME(X) register_##X

#define REGISTER_FUNCTION_SIGNATURE(X) \
int REGISTER_FUNCTION_NAME(X)(duckdb_connection con, duckdb_scalar_function_set *func_set)

/* Generate registration function declarations from SCALAR_FUNCTIONS. */
#define DECLARE_REGISTER_FUNCTION(FUNCTION_NAME, DB_TYPE_OUTPUT, C_TYPE, NUM_CONVERTER, INT_CONVERTER) \
REGISTER_FUNCTION_SIGNATURE(FUNCTION_NAME);

SCALAR_FUNCTIONS(DECLARE_REGISTER_FUNCTION)

#undef DECLARE_REGISTER_FUNCTION

#endif /* DB_SCALAR_FUNCTIONS_H */
