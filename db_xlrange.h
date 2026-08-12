#ifndef DB_XLRANGE_H
#define DB_XLRANGE_H

#include <windows.h>
#include <stddef.h>
#include "XLCALL.H"
#include "duckdb.h"

/*
 * Register xlrange() table function.
 *
 * Supported signatures:
 *   xlrange(index)
 *   xlrange(index, sample = n)
 *
 * ranges must remain valid for the lifetime of the registered function.
 *
 * Returns 1 on success, 0 on failure.
 */
int register_xlrange_func
(
    duckdb_connection con,
    LPXLOPER12 ranges,
    size_t nrange,
    duckdb_table_function *func
);

#endif // DB_XLRANGE_H
