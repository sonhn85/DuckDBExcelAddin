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
 *   xlrange(index, sample = XLRANGE_DEFAULT_SAMPLE_COUNT)
 *   xlrange(index, all_varchar = false)
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
 * Defaults to false
 *
 * When header = true (default), the first row is interpreted as column names.
 * When header = false, column names are generated as column_0, column_1, ...
 *
 * When strict = true (default), column names must be non-empty and unique or an error is raised.
 * When strict = false, empty column names are generated as unnamed_0, unnamed_1, ...
 * and duplicated column names are renamed to name, name_1, name_2, ...
 *
 * When ignore_errors = false (default), an error is raised if values incompatible with inferred type are encountered.
 * When ignore_errors = true, incompatible values are silently converted to NULL.
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
