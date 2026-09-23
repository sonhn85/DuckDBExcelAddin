#ifndef DB_XLRANGE_H
#define DB_XLRANGE_H

#include <windows.h>
#include <stddef.h>
#include "XLCALL.H"
#include "duckdb.h"

/*
 * Register the xlrange() table function.
 *
 * xlrange() exposes an Excel range to DuckDB with configurable
 * headers, type inference, strict naming, and conversion handling.
 *
 * ranges is borrowed and must remain valid for the function lifetime.
 *
 * Returns 1 on success and 0 on failure.
 */
int register_xlrange_func
(
    duckdb_connection con,
    LPXLOPER12 ranges,
    size_t nrange,
    duckdb_table_function *function
);

#endif /* DB_XLRANGE_H */
