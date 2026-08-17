#ifndef DB_XLRANGE_H
#define DB_XLRANGE_H

#include <windows.h>
#include <stddef.h>
#include "XLCALL.H"
#include "duckdb.h"

/*
 * Register the xlrange() table function.
 *
 * Supported signatures:
 *   xlrange(index)
 *   xlrange(index, sample = n)
 *   xlrange(index, all_varchar = true)
 *
 * index is the 1-based position of an xltypeMulti XLOPER12 argument
 * passed to an XLL worksheet function.
 *
 * sample specifies the number of data rows used for type inference.
 * A value of 0 samples all data rows.
 * Defaults to XLRANGE_DEFAULT_SAMPLE_COUNT.
 *
 * When all_varchar = true, all values are returned as VARCHAR
 * and type inference is disabled.
 *
 * Returns:
 *   1 on success.
 *   0 on failure.
 */
int register_xlrange_func
(
    duckdb_connection con,
    /* Borrowed. Must remain valid for the lifetime of the table function. */
    LPXLOPER12 ranges,
    size_t nrange,
    duckdb_table_function *function
);

#endif /* DB_XLRANGE_H */
