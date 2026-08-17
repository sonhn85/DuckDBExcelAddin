#ifndef CONFIG_H
#define CONFIG_H

/* Add-in version information. */
#ifndef ADDIN_VERSION
#define ADDIN_VERSION               "dev"
#endif
#define DUCKDB_REQUIRED_VERSION     "1.5.0"

/* DuckDB */
#define DUCKDB_DLL                  L"duckdb.dll"

/*
 * Excel limits.
 *
 * These values are defined by Excel and must not be modified.
 */
#define XL_MAX_ROW                  0x00100000
#define XL_MAX_COL                  0x00004000
#define XLSTR_MAX_LEN               0x00007fff

/*
 * XLOPER12 constants defined by the Excel C API.
 *
 * Do not modify.
 */
#define XLTYPEMASK                  0x00000fff

/* Internal limits */
#define ERR_MSG_MAX_LEN             512
#define MSG_MAX_LENGTH              512

/* xlrange() */
#define XLRANGE_DEFAULT_SAMPLE_COUNT 30

/*
 * Worksheet functions expose 30 optional parameters by default.
 *
 * Extend WORKSHEET_PARAMS() to increase the maximum number of
 * xlrange and bind parameters supported by worksheet functions.
 */
#define WORKSHEET_PARAMS(X, Y) \
    X(1)  X(2)  X(3)  X(4)  X(5)  X(6)  X(7)  X(8)  X(9)  X(10) \
    X(11) X(12) X(13) X(14) X(15) X(16) X(17) X(18) X(19) X(20) \
    X(21) X(22) X(23) X(24) X(25) X(26) X(27) X(28) X(29) Y(30)

/* Excel function category */
#define FUNCTION_CATEGORY           L"DuckDB"

/*
 * Constants used when converting Excel date/time values
 * to DuckDB temporal types.
 */

/*
 * Number of days between the Excel epoch (1899-12-30)
 * and the Unix epoch (1970-01-01).
 */
#define EPOCH_DELTA                 25569

#define S_PER_DAY                   86400.0             /* Seconds per day */
#define MS_PER_DAY                  86400000.0          /* milliseconds per day */
#define US_PER_DAY                  86400000000.0       /* microseconds per day */
#define NS_PER_DAY                  86400000000000.0    /* nanoseconds per day */

/*
 * Number of bits used to store the UTC offset component
 * in DuckDB TIME WITH TIME ZONE values.
 */
#define TIMETZ_OFFSET_BITS          24

/*
 * Floating-point comparison tolerance.
 */
#define EPSILON                     1e-12 

/*
 * Powers of 10 used for DECIMAL scaling.
 *
 * DuckDB currently supports a maximum DECIMAL scale of 18.
 *
 */
static const double DEC_DIVISORS[] = {
    1e0,    1e1,    1e2,    1e3,    1e4,    1e5,    1e6,    1e7,    1e8,    1e9,
    1e10,   1e11,   1e12,   1e13,   1e14,   1e15,   1e16,   1e17,   1e18
};

#endif /* CONFIG_H */
