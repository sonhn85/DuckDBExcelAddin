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
 *   xlrange(index, header = true)
 *   xlrange(index, strict = true)
 *   xlrange(index, ignore_errors = false)
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
 * When header = true, the first row is interpreted as column names.
 * Defaults to true.
 *
 * When strict = true, empty or whitespace-only header names are rejected.
 * When strict = false, generated names such as unnamed_0 are used for empty
 * headers, and duplicate names receive a numeric suffix.
 * Defaults to true.
 *
 * When ignore_errors = true, values that cannot be converted to the inferred
 * column type are returned as NULL. When false, conversion errors abort the
 * scan. Defaults to false.
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
