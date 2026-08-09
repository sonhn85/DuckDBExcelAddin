#ifndef DB_XLRANGE_H
#define DB_XLRANGE_H

#include <windows.h>
#include <stddef.h>     // size_t
#include "XLCALL.H"     // LPXLOPER12
#include "duckdb.h"

int register_xlrange_func
(
    duckdb_connection con,
    LPXLOPER12 ranges,
    size_t nrange,
    size_t nsample,
    duckdb_table_function *func
);

#endif // DB_XLRANGE_H