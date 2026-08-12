#ifndef CONFIG_H
#define CONFIG_H

/* Versioning */
#ifndef ADDIN_VERSION
#define ADDIN_VERSION               "dev"
#endif
#define DUCKDB_REQUIRED_VERSION     "1.5.0"

/* DuckDB */
#define DUCKDB_DLL                  L"duckdb.dll"

/* Excel limits */
#define XL_MAX_ROW                  0x00100000
#define XL_MAX_COL                  0x00004000
#define XLSTR_MAX_LEN               0x00007fff      // Do not change

/* XLOPER12 */
#define XLTYPEMASK                  0x00000fff      // Do not change

/* Internal limits */
#define ERR_MSG_MAX_LEN             512
#define MSG_MAX_LENGTH              512

/* xlrange() */
#define XLRANGE_DEFAULT_SAMPLE_COUNT 30

/* Worksheet functions expose 30 optional parameters by default.
   Extend WORKSHEET_PARAMS() to increase the parameter count. */
#define WORKSHEET_PARAMS(X, Y) \
    X(1)  X(2)  X(3)  X(4)  X(5)  X(6)  X(7)  X(8)  X(9)  X(10) \
    X(11) X(12) X(13) X(14) X(15) X(16) X(17) X(18) X(19) X(20) \
    X(21) X(22) X(23) X(24) X(25) X(26) X(27) X(28) X(29) Y(30)

/* Excel function category */
#define FUNCTION_CATEGORY           L"DuckDB"

#endif /* CONFIG_H */
