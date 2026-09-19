# DuckDBExcelAddin

[![DuckDB](https://img.shields.io/badge/DuckDB-1.5.x-orange)](https://duckdb.org/)
[![Windows](https://img.shields.io/badge/Windows-10%2B-lightblue)](#requirements)
[![Excel 365](https://img.shields.io/badge/Excel-365-green)](#requirements)
[![Excel 2024](https://img.shields.io/badge/Excel-2024-green)](#requirements)
[![Excel 2021](https://img.shields.io/badge/Excel-2021-green)](#requirements)
[![License](https://img.shields.io/badge/License-MIT-blue)](LICENSE)

A native Microsoft Excel XLL add-in for querying Excel ranges with DuckDB SQL and parameter binding.

## Screenshot

![Screenshot](docs/screenshot.jpg "Screenshot")

## Quick Start

```excel
=DUCKDB.EXEC(
"SELECT cif, SUM(amount)
 FROM xlrange(1)
 GROUP BY cif",
A1:D10000
)
```

Query Excel ranges directly with DuckDB SQL and return the result as a dynamic array.

## Features

- Native DuckDB integration
- Query Excel ranges
- Parameter binding from Excel values
- Asynchronous execution
- No .NET runtime required

## Status

> **Stable**
>
> Core functionality is considered stable and suitable for production use. Future releases will prioritize backward compatibility with existing workbooks.

## Comparison with xlDuckDB

This project was inspired by [xlDuckDB](https://github.com/RusselWebber/xlDuckDb), an XLL add-in that integrates DuckDB with Microsoft Excel.

| Feature | DuckDBExcelAddin | xlDuckDB |
|----------|----------|----------|
| Native Excel formula experience | ✅ | ✅ |
| Dynamic array (spill) results | ✅ | ✅ |
| Query external files | ✅ | ✅ |
| Query Excel ranges | ✅ | ✅ |
| Parameter binding from Excel values | ✅ | ❌ |
| `xlrange` type inference options | ✅ | ❌ |
| Helpers for Excel date and time values | ✅ | ❌ |
| Async execution | ✅ | ❓ |
| XLL implementation | ✅ | ✅ |
| .NET free | ✅ | ❌ |
| Runtime DuckDB DLL upgrade | ✅ | ❓ |

*Comparison based on publicly documented features available at the time of writing.*

# Installation

## Requirements

- Microsoft Excel 64-bit with Dynamic Array (Spill Range) support:
  - Microsoft 365 Excel
  - Excel 2024
  - Excel 2021
- DuckDB 1.5.x (`duckdb.dll`)

## Enable the Add-in

Copy these files to the same folder:

```text
DuckDBExcelAddIn.xll
duckdb.dll
```
Open the XLL directly, or add it through the Excel Add-ins dialog.

## DuckDB Upgrade

DuckDB is loaded dynamically at runtime.

Upgrading DuckDB generally requires only replacing:

```text
duckdb.dll
```

with a newer compatible version.

# Tutorial

## Query Excel Ranges

- Excel ranges are exposed as `xlrange()` table function:

```excel
=DUCKDB.EXEC(
"SELECT *
 FROM xlrange(1)
 WHERE cif = 10001",
A1:D100
)
```

- With inference options:

```excel
=DUCKDB.EXEC(
"SELECT *
 FROM xlrange(1, sample=50)
 WHERE cif = 10001",
A1:D100
)
```

```excel
=DUCKDB.EXEC(
"SELECT *
 FROM xlrange(1, all_varchar=true)
 WHERE cif = 10001",
A1:D100
)
```

## Query External Files

- Use database file as default database:

```excel
=DUCKDB.EXECA(
"Path\db.duckdb",
"SELECT * FROM table_name;"
)
```

- Use DuckDB `read_xxx()` functions:

```excel
=DUCKDB.EXEC(
"SELECT *
 FROM read_duckdb('Path\db.duckdb', table_name='mytable')
 LIMIT 100"
)
```

## Parameter Binding

- Auto-incremented parameters:

```excel
=DUCKDB.EXEC(
"SELECT *
 FROM xlrange(?)
 WHERE cif = ?",
A1:D100,
1,
10001
)
```

- Positional parameters:

```excel
=DUCKDB.EXEC(
"SELECT *
 FROM xlrange($1)
 WHERE cif = $2",
A1:D100,
1,
10001
)
```

- Dynamic range selector:

```excel
=DUCKDB.EXEC(
"SELECT *
 FROM xlrange(?)
 WHERE cif = ?",
A1:D100,
F1:I200,
2,
10001
)
```

- Dynamic file reader:

```excel
=DUCKDB.EXEC(
"SELECT *
 FROM read_duckdb(?, table_name=?)
 LIMIT 100",
"Path\db.duckdb",
"mytable"
)
```

- Dynamic column selector:

```excel
=DUCKDB.EXEC(
"SELECT columns(?)
 FROM read_duckdb(?, table_name=?)
 LIMIT 100",
"column1",
"Path\db.duckdb",
"mytable"
)
```

- Dynamic table selector:

```excel
=DUCKDB.EXEC(
"CREATE TABLE mytable(col1) AS
 SELECT * FROM range(10);
 SELECT *
 FROM query_table(?)",
"mytable"
)
```

## Excel Date and Time Helpers

Adds scalar functions to convert Excel date and time values stored as DOUBLE to DuckDB DATE, TIME and TIMESTAMP:

```excel
=DUCKDB.EXEC(
"SELECT xldate(date_col)
 FROM xlrange(1)",
A1:D100
)
```

```excel
=DUCKDB.EXEC(
"SELECT xltime(time_col)
 FROM xlrange(1)",
A1:D100
)
```

```excel
=DUCKDB.EXEC(
"SELECT xldatetime(datetime_col)
 FROM xlrange(1)",
A1:D100
)
```

# Architecture

DuckDBExcelAddin executes SQL inside the Excel process using the embedded DuckDB engine.

Excel ranges are exposed to DuckDB through the `xlrange()` table function, allowing worksheet data to participate in SQL queries.

SQL statements are extracted, then each statement is prepared, bound with parameters, and executed.

The result of the final statement is materialized and returned to Excel as a dynamic array (spill range).

# References

## Parameter Binding

| Type | Support | Note |
|------|---------|------|
| Auto incremented `?` | ✅ | |
| Positional `$1` | ✅ | Must reset parameter index for **each statement**. |
| Named `$param` | ❌ | Excel doesn't support named parameters. |

## Formulas

### All Formulas

| Function | Since | Syntax | Purpose | Equivalent xlDuckDB Formula |
|----------|-------|---------|---------|-----------------------------|
| DUCKDB.EXEC / DUCKDB.EXEC.ASYNC | | `=DUCKDB.EXEC(sql, [range1], [range2], ..., [param1], [param2], ...)` | Execute SQL using in-memory database. | `=DuckDbQuery(sql,, range)` |
| DUCKDB.EXECX / DUCKDB.EXECX.ASYNC | | `=DUCKDB.EXECX([init_sql], sql, [range1], [range2], ..., [param1], [param2], ...)` | Execute initialization SQL, then main SQL using in-memory database. | |
| DUCKDB.EXECA / DUCKDB.EXECA.ASYNC | 1.1.0 | `=DUCKDB.EXECA([db_file_path], sql, [range1], [range2], ..., [param1], [param2], ...)` | Execute SQL using a DuckDB file as the default database. | `=DuckDbQuery(sql, dbfilepath, range)` |
| DUCKDB.EXECAX / DUCKDB.EXECAX.ASYNC | 1.1.0 | `=DUCKDB.EXECAX([db_file_path], [init_sql], sql, [range1], [range2], ..., [param1], [param2], ...)` | EXECA plus initialization SQL. | |
| DUCKDB.INFO | | `=DUCKDB.INFO()` | Return add-in and DuckDB runtime information. | |

### Formula Parameters

- Only sql is required, other parameters can be ignored. For example: `=DUCKDB.EXECA( , A1)`.
- Ranges are exposed to `xlrange()` and must appear before bound parameters.
- When multiple SQL statements are supplied, all statements are executed sequentially, but only the result of the final statement is returned to Excel.
- Asynchronous formulas do not block Excel recalculation, but they introduce overhead due to thread creation and deep copying of worksheet ranges.
- Initialization SQL is executed before the main query and can be used to define reusable macros, views, or other helper objects.
- Parameters are not bound in initialization SQL.

## xlrange

Exposes Excel ranges as table function.

Syntax:

```sql
xlrange(index, sample=n, all_varchar=false, header=true, strict=true, ignore_errors=false)
```

### Options

| Parameter | Since | Status | Default | Description |
|-----------|-------|--------|---------|-------------|
| `index` | | 🔴 **Required** | | 1-based position of an Excel range passed to formula. |
| `all_varchar` | | 🟢 Optional | `false` | - When `true`, all values are returned as `VARCHAR` and type inference is disabled.<br>- When `false`, column type is inferred. |
| `sample` | | 🟢 Optional | `30` | Number of data rows used for type inference.<br>- A value of `0` samples all data rows.<br>- This option is ignored when `all_varchar=true`. |
| `header` | 1.2.0 | 🟢 Optional | `true` | - When `true`, the first row is interpreted as column names. <br>- When `false`, column names are generated as `column_0`, `column_1`, ... |
| `strict` | 1.3.0 | 🟢 Optional | `true` | - When `true`, column names must be non-empty and unique or an error is raised.<br>- When `false`, empty column names are generated as `unnamed_0`, `unnamed_1`, ... and duplicated column names are renamed to `name`, `name_1`, `name_2`, ...<br>- This option is ignored when `header=false`. |
| `ignore_errors` | 1.5.0 | 🟢 Optional | `false` | - When `true` an error is raised if values incompatible with inferred type are encountered.<br>- When `false` incompatible values are **silently** converted to NULL. |

### Type Mapping

| Excel Value | DuckDB Type |
|-------------|-------------|
| Number | DOUBLE or INTEGER |
| Boolean | BOOLEAN |
| Text | VARCHAR |
| Empty/Error | NULL |

### Inference strategy

1. Scan for the first non-empty value and use its type as the candidate column type. Whole-number numeric cells are inferred as INTEGER when all sampled values fit within the INT32 range.

2. Sample the remaining rows up to the configured sample limit.

3. If incompatible types are encountered, the column type is promoted to DOUBLE or VARCHAR.

## Date and Time Helpers

| Function | Purpose | Syntax |
|----------|---------|--------|
| `xldate` | Convert an Excel serial date value to a DuckDB `DATE`. | `xldate(value)` |
| `xltime` | Convert the fractional portion of an Excel serial value to a DuckDB `TIME`. | `xltime(value)` |
| `xldatetime` | Convert an Excel serial datetime value to a DuckDB `TIMESTAMP`. | `xldatetime(value)` |

# Known Limitations

## Excel Number Formats

Formulas cannot access cell number formats.

When using `xlrange`, if header cells are numeric values formatted as dates or times. The Column names are registered as numeric strings. For example, the column will be name "46387" for Excel date December 31st, 2026.

Workaround: Convert Excel date to text with `=TEXT(value, date_format)` formula first.

## Excel Worksheet Limits

Results are returned as Excel Dynamic Arrays.

Excel limits apply:

| Limit | Value |
|---------|---------|
| Rows | 1,048,576 |
| Columns | 16,384 |
| String length | 32,767 |

Queries exceeding these limits are not supported.

## Excel Number Types

Large DuckDB numeric types (BIGINT, HUGEINT, DECIMAL) may lose precision when converted to Excel numbers (DOUBLE).

## Excel Range-Based Data Exchange

Data is exchanged through Excel ranges.

- Excel worksheet limits apply.
- Large datasets may consume significant memory.
- Input and output data must fit within Excel worksheets.

For large datasets, query DuckDB-supported sources directly (Parquet, CSV, DuckDB databases, etc.) and return only the required results to Excel.

```sql
SELECT *
FROM read_parquet('large_dataset.parquet')
LIMIT 1000
```

## DuckDB Composite Types

The following DuckDB types are not supported as Excel results:

- LIST
- STRUCT
- MAP
- UNION
- VARIANT

Workaround: Cast the value to `VARCHAR`.

# Troubleshooting

## Troubleshooting

| Issue | Verify |
|---------|---------|
| Add-in fails to load | - Excel is 64-bit<br>- `duckdb.dll` is located next to `DuckDBExcelAddIn.xll`<br>- `duckdb.dll` version is 1.5.x<br>- The add-in is not blocked |
| `#VALUE!` returned | - The add-in is loaded<br>- Dynamic Arrays are supported<br>- Input ranges and Result size do not exceed Excel limits |
| `#SPILL!` error | - The destination spill range is empty<br>- There are enough rows and columns available to display the result |

# Building

## Build Requirements

- Excel XLL SDK
- DuckDB C API (`duckdb.h`)
- [uthash](https://troydhanson.github.io/uthash/) by _troydhanson_ and maintained _Arthur O'Dwyer_

## Compiler Support

Development and testing are performed primarily using w64devkit (MinGW-w64).

Other toolchains such as Visual Studio (MSVC) may work but are currently unverified.

Contributions and testing reports are welcome.

## Build Notes

`XLCALL.H` `XLOPER` struct contain a member named:

```c
bool
```

which conflicts with C `bool` keyword.

You need to edit the header and rename the member to, for example:

```c
xbool
```

and update the corresponding references in `FRAMEWRK.C`.

This modification only affects local compilation and does not affect runtime behavior because `XLOPER` is never used.

## Build instruction

- Development

```bash
make EXCEL_SDK_PATH=<excel-sdk> DUCKDB_INC_PATH=<duckdb-include> xll
```

- Release

```bash
make ADDIN_VERSION=vx.x.x EXCEL_SDK_PATH=<excel-sdk> DUCKDB_INC_PATH=<duckdb-include> xll
```

## Tests

The release package includes a workbook containing examples and regression tests for the major features of DuckDBExcelAddin.

```text
examples\test_cases.xlsx
```

# Acknowledgements

Special thanks to the DuckDB team and contributors for creating an exceptional embedded analytical database.

This project was inspired by [xlDuckDB](https://github.com/RusselWebber/xlDuckDb), particularly its formula-based integration approach and the `xlrange` concept.

Additional thanks to:

- [uthash](https://troydhanson.github.io/uthash/) by _troydhanson_ and  maintained _Arthur O'Dwyer_
- Microsoft Excel XLL SDK

# License

MIT License
