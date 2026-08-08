# DuckDB Excel Add-in (XLL)

A native Microsoft Excel XLL add-in that enables running SQL directly against Excel ranges using DuckDB.

The add-in provides:

- Native DuckDB integration inside Excel
- Excel range table functions (`xlrange`)
- Prepared statements with parameter binding
- Multiple SQL statements in a single execution
- Asynchronous execution
- Dynamic-array (spill range) output
- Configurable schema inference
- Lightweight deployment
- No .NET runtime dependency

> Status: Early Release / Experimental
>
> Tested with:
>
> - Microsoft Excel 64-bit
> - Dynamic Array support
> - DuckDB 1.5+
> - MinGW-w64 (w64devkit)

---

# Quick Summary

✅ Native Excel XLL

✅ No .NET Runtime

✅ No VSTO

✅ No COM Registration

✅ No Office Add-in Installer

✅ Two-file deployment (`.xll` + `duckdb.dll`)

✅ Parameter Binding

✅ Async Execution

✅ Multi-Statement SQL Execution

✅ DuckDB 1.5+

✅ Drop-in DuckDB upgrades

---

# Why This Project?

This project was inspired by **xlduckdb**, which demonstrated integration between DuckDB and Microsoft Excel through an XLL add-in.

The design goals of this project are slightly different.

## Parameter Binding

Supports DuckDB prepared statements and positional placeholders.

```excel
=DUCKDB.EXEC(
"SELECT *
 FROM xlrange(1)
 WHERE cif = ?",
A1:D100,
10001
)
```

Benefits:

- Safer query construction
- No string concatenation in formulas
- Reusable SQL templates
- Natural DuckDB workflow

---

## Asynchronous Execution

Supports asynchronous worksheet functions.

```excel
=DUCKDB.EXEC.ASYNC(...)
```

```excel
=DUCKDB.QUERY.ASYNC(...)
```

Long-running queries do not block Excel recalculation.

---

## Native DuckDB Workflows

With DuckDB 1.5+, parameterized execution enables more natural use of DuckDB's dynamic capabilities and external data sources.

This is particularly useful when working with:

- Dynamic table names
- Dynamic column selection
- Dynamic database objects
- External data sources
- Reusable SQL templates

Examples:

```sql
SELECT *
FROM read_duckdb(?, table_name=?)
```

```sql
SELECT columns(?)
FROM read_csv(?)
```

```sql
SELECT *
FROM query_table(?)
```

This avoids many string-based SQL construction patterns.

---

## Lightweight Native Deployment

The add-in is implemented entirely in native C.

Benefits:

- No .NET Runtime
- No VSTO
- No COM registration
- No Office Add-in installer
- Fast startup
- Small deployment footprint

Deployment typically consists of:

```text
DuckDBExcelAddIn.xll
duckdb.dll
```

---

## Drop-In DuckDB Upgrades

DuckDB is loaded dynamically at runtime.

Upgrading DuckDB generally requires only replacing:

```text
duckdb.dll
```

with a newer compatible version.

No recompilation of the XLL is required.

---

## Excel-Native Table Integration

Similar to xlduckdb, Excel ranges are directly exposed as DuckDB tables.

```sql
SELECT *
FROM xlrange(1)
```

If using parameter binding:

```sql
SELECT *
FROM xlrange(?)
```

with configurable schema inference:

```excel
=DUCKDB.SETSAMPLE(50)
```

---

# Features

- Excel range table function (`xlrange`)
- Prepared statements
- Parameter binding
- Multiple SQL statements
- Async execution
- Dynamic-array results
- Configurable type inference
- Direct query mode for advanced DuckDB features
- Runtime configurable sampling

---

# Requirements

## Microsoft Excel

The add-in requires:

- Microsoft Excel 64-bit
- Dynamic Array (Spill Range) support

Supported versions include:

- Microsoft 365 Excel (64-bit)
- Excel 2021 (64-bit)
- Excel 2024 (64-bit)

Older Excel versions without Dynamic Arrays are not supported.

---

## Platform

- Windows x64
- Excel x64
- DuckDB x64

---

# Dependencies

## Runtime

- Microsoft Excel 64-bit
- Dynamic Array support
- DuckDB 1.5.0 or later (`duckdb.dll`)

## Build Requirements

- Excel XLL SDK
  - `XLCALL`
  - `FRAMEWRK`
- DuckDB C API
  - `duckdb.h`
  - `duckdb.dll`
- C11 compiler with atomics support

---

# Compiler Support

Development and testing are performed primarily using:

- MinGW-w64
- w64devkit

Other toolchains such as Visual Studio (MSVC) may work but are currently unverified.

Contributions and testing reports are welcome.

---

# Build Notes

## XLCALL.H Compatibility

Some versions of `XLCALL.H` contain a struct member named:

```c
bool
```

which conflicts with the C99/C11 keyword.

You may need to modify the SDK header locally.

Example:

```c
bool
```

rename to:

```c
xbool
```

and update the corresponding references.

This modification only affects local compilation and does not affect runtime behavior.

---

## FRAMEWRK Linker Requirement

`FRAMEWRK` depends on legacy Excel4-family APIs.

When building with MinGW-w64 or w64devkit, unresolved external references may occur even though those functions are never used by the add-in.

A simple workaround is providing stub implementations:

```c
int PASCAL Excel4(int xlfn, LPXLOPER pxRes, int count, ...)
{
    return 0;
}

int PASCAL Excel4v(int xlfn, LPXLOPER pxRes, int count, LPXLOPER opers[])
{
    return 0;
}
```

These functions exist only to satisfy linker requirements introduced by `FRAMEWRK`.

---

# Installation

Copy:

```text
DuckDBExcelAddIn.xll
duckdb.dll
```

Open the XLL from Excel.

No installer is required.

No COM registration is required.

No .NET runtime is required.

---

# Functions Reference

## DUCKDB.EXEC

Executes one or more SQL statements using prepared statements.

### Supports

- Parameter binding (`?`)
- Multiple statements
- DDL
- DML
- Queries
- Excel ranges via `xlrange()`
- PIVOT without dynamic pivot value

### Syntax

```excel
=DUCKDB.EXEC(
    sql,
    [range1],
    [range2],
    ...,
    [param1],
    [param2]
)
```

### Example

```excel
=DUCKDB.EXEC(
"SELECT *
 FROM xlrange(1)
 WHERE cif = ?",
A1:D100,
10001
)
```

---

## DUCKDB.EXEC.ASYNC

Asynchronous version of `DUCKDB.EXEC`.

### Syntax

```excel
=DUCKDB.EXEC.ASYNC(sql, ...)
```

---

## DUCKDB.QUERY

Executes SQL directly through DuckDB.

### Supports

- PIVOT
- Advanced DuckDB SQL features
- Planner-generated internal types
- Excel ranges via `xlrange()`

### Does Not Support

- Parameter binding

### Syntax

```excel
=DUCKDB.QUERY(
    sql,
    [range1],
    [range2]
)
```

### Example

```excel
=DUCKDB.QUERY(
"PIVOT xlrange(1)
 ON group
 USING SUM(amount)
 GROUP BY cif",
A1:C100
)
```

---

## DUCKDB.QUERY.ASYNC

Asynchronous version of `DUCKDB.QUERY`.

### Syntax

```excel
=DUCKDB.QUERY.ASYNC(sql, ...)
```

---

## DUCKDB.SETSAMPLE

Sets the global sample size used for `xlrange()` schema inference.

### Syntax

```excel
=DUCKDB.SETSAMPLE(nsample)
```

### Examples

```excel
=DUCKDB.SETSAMPLE(1)
```

Infer types using a single sampled row.

```excel
=DUCKDB.SETSAMPLE(30)
```

Use the default setting.

```excel
=DUCKDB.SETSAMPLE(100)
```

Inspect up to 100 sampled rows.

```excel
=DUCKDB.SETSAMPLE(0)
```

Inspect all available rows.

### Special Value

```excel
=DUCKDB.SETSAMPLE(0)
```

Disables sampling and scans all available data rows during schema inference.

### Return Value

Returns the currently configured sample size.

---

# Table Function Reference

## xlrange(index)

Exposes Excel ranges as DuckDB tables.

### Example

```excel
=DUCKDB.EXEC(
"SELECT *
 FROM xlrange(1)",
A1:D100
)
```

### Multiple Ranges with Parameter Binding

```excel
=DUCKDB.EXEC(
"SELECT *
 FROM xlrange(?) a
 JOIN xlrange(?) b
   ON a.cif = b.cif",
A1:D100,
F1:H100,
1,
2
)
```

### Rules

- First row is treated as column names.
- Column names must be non-empty.
- Ranges must appear before scalar parameters.
- Range numbering starts at 1.

---

# Type Inference

Supported inferred DuckDB types:

| Excel Value | DuckDB Type |
|------------|-------------|
| Number | DOUBLE |
| Boolean | BOOLEAN |
| Text | VARCHAR |
| Empty/Null | Ignored during inference |

Default sample count:

```text
30 rows
```

Configured globally using:

```excel
=DUCKDB.SETSAMPLE(...)
```

---

# Choosing Between EXEC and QUERY

| Feature | EXEC | QUERY |
|----------|------|--------|
| Parameters (`?`) | ✅ | ❌ |
| Multiple statements | ✅ | ❌ |
| Prepared statements | ✅ | ❌ |
| PIVOT | ⚠️ | ✅ |
| Advanced planner features | ⚠️ | ✅ |
| xlrange() | ✅ | ✅ |
| Async support | ✅ | ✅ |

Use **EXEC** when parameter binding or multiple statements are required.

Use **QUERY** when working with dynamic PIVOT or advanced DuckDB SQL features.

---

# Limitations

## Excel Worksheet Limits

Results are returned as Excel Dynamic Arrays.

Excel limits apply:

| Limit | Value |
|---------|---------|
| Rows | 1,048,576 |
| Columns | 16,384 |

Queries exceeding these limits are not supported.

---

## Excel Range-Based Data Exchange

Input data is supplied through Excel ranges and output data is returned through Excel ranges.

Consequently:

- Excel worksheet limits apply.
- Large datasets may consume significant memory.
- Both source and result data must fit within Excel's worksheet model.

For large-scale analytics, it is generally more efficient to query:

- DuckDB databases
- Parquet files
- CSV files
- Other external DuckDB-supported data sources

directly through DuckDB rather than loading all data into Excel worksheets.

Example:

```sql
SELECT *
FROM read_parquet('large_dataset.parquet')
LIMIT 1000
```

Or only fetch aggregate data to the worksheet for the next step in your workflow.

---

## Not Intended as a Large Data Storage Engine

This add-in is optimized for:

- Excel-centric workflows
- Interactive analysis
- Ad-hoc queries
- Reporting

It is not intended to replace dedicated database systems or data engineering pipelines.

---

# Architecture

```text
Excel
  |
  v
XLL Worksheet Function
  |
  +---- EXEC
  |       |
  |       +-- Extract statements
  |       +-- Prepare
  |       +-- Bind
  |       +-- Execute
  |
  +---- QUERY
  |       |
  |       +-- Direct DuckDB Query
  |
  +---- xlrange()
  |
  +---- Async Worker
  |
  v
DuckDB
  |
  v
Excel Dynamic Array
```

---

# Motivation

DuckDB has fundamentally changed what is possible for local and offline analytics.

Many analytical tasks that previously required:

- Database servers
- Complex ETL pipelines
- Heavy frameworks
- Python programming
- Notebooks
- Specialized BI tools

that is complex to setup and unfamilier to office users, managers.

These task can now be performed locally using a single embedded analytical database.

One of the goals of this project is to bring that capability closer to everyday Excel users.

Excel remains one of the most widely used analytical tools, especially among finance, accounting, auditing, operations, risk management, and business users. However, many analytical workloads have outgrown what traditional Excel formulas, PivotTables, and worksheets can efficiently handle.

By combining Excel with DuckDB, users can:

- Query large datasets using SQL
- Join data sources efficiently
- Perform analytical aggregations
- Work with millions of records outside Excel's traditional calculation engine
- Remain inside a familiar Excel environment

The intention is not to replace data engineering or data science tools, but to lower the barrier for office users who need more analytical power than Excel alone can provide.

Many office users already possess strong domain expertise but are often limited by tooling complexity.

The goal of this project is to provide a practical path from traditional spreadsheet analysis to modern analytical workflows, enabling users to leverage SQL and DuckDB without requiring Python, complex frameworks, database servers, or specialized data engineering tools.

Hopefully this add-in helps more people discover DuckDB and enables Excel users to work with larger datasets that would otherwise require complicated tooling, frameworks, or programming knowledge.

---

# Acknowledgements

Special thanks to the DuckDB team and contributors.

DuckDB is an extraordinary project that brings tremendous value to local and offline analytics. Its performance, simplicity, and embedded architecture make advanced analytical processing accessible to a much wider audience.

This project would not exist without the work of the DuckDB community.

Special thanks xlduckdb that I have used in real life and inspired me about formula intergation path and xlrange mechanism.

Addition thanks to:

- Microsoft Excel XLL SDK

Parts of this documentation were drafted with AI assistance and reviewed manually.