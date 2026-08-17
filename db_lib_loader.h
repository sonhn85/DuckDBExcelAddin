/*
 * Dynamic DuckDB loader.
 *
 * Defines function pointer types and imported function declarations
 * used to load DuckDB APIs at runtime.
 */

#ifndef DB_LIB_LOADER_H
#define DB_LIB_LOADER_H

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include "duckdb.h"

/* Function pointer type name generator.
   Example: duckdb_open -> duckdb_open_t */
#define TO_DUCKDB_FUNCTION_TYPE(x) x##_t

/* Version */
typedef const char *(*TO_DUCKDB_FUNCTION_TYPE(duckdb_library_version))(void);

/* Database lifecycle */
typedef duckdb_state (*TO_DUCKDB_FUNCTION_TYPE(duckdb_open))(
    const char *path,
    duckdb_database *out_database
);
typedef void (*TO_DUCKDB_FUNCTION_TYPE(duckdb_close))(
    duckdb_database *database
);
typedef duckdb_state (*TO_DUCKDB_FUNCTION_TYPE(duckdb_connect))(
    duckdb_database database,
    duckdb_connection *out_connection
);
typedef void (*TO_DUCKDB_FUNCTION_TYPE(duckdb_disconnect))(
    duckdb_connection *connection
);

/* Query execution */
typedef duckdb_state (*TO_DUCKDB_FUNCTION_TYPE(duckdb_query))(
    duckdb_connection connection,
    const char *query,
    duckdb_result *out_result
);
typedef const char *(*TO_DUCKDB_FUNCTION_TYPE(duckdb_result_error))(
    duckdb_result *result
);
typedef void (*TO_DUCKDB_FUNCTION_TYPE(duckdb_destroy_result))(
    duckdb_result *result
);

/* Statement extraction */
typedef idx_t (*TO_DUCKDB_FUNCTION_TYPE(duckdb_extract_statements))(
    duckdb_connection connection,
    const char *query,
    duckdb_extracted_statements *out_extracted_statements
);
typedef const char *(*TO_DUCKDB_FUNCTION_TYPE(duckdb_extract_statements_error))(
    duckdb_extracted_statements extracted_statements
);
typedef void (*TO_DUCKDB_FUNCTION_TYPE(duckdb_destroy_extracted))(
    duckdb_extracted_statements *extracted_statements
);

/* Prepared statements */
typedef duckdb_state (*TO_DUCKDB_FUNCTION_TYPE(duckdb_prepare_extracted_statement))(
    duckdb_connection connection,
    duckdb_extracted_statements extracted_statements,
    idx_t index,
    duckdb_prepared_statement *out_prepared_statement
);
typedef const char *(*TO_DUCKDB_FUNCTION_TYPE(duckdb_prepare_error))(
    duckdb_prepared_statement prepared_statement
);
typedef duckdb_state (*TO_DUCKDB_FUNCTION_TYPE(duckdb_execute_prepared))(
    duckdb_prepared_statement prepared_statement,
    duckdb_result *out_result
);
typedef idx_t (*TO_DUCKDB_FUNCTION_TYPE(duckdb_nparams))(
    duckdb_prepared_statement prepared_statement
);
typedef void (*TO_DUCKDB_FUNCTION_TYPE(duckdb_destroy_prepare))(
    duckdb_prepared_statement *prepared_statement
);

/* Parameter binding */
typedef duckdb_state (*TO_DUCKDB_FUNCTION_TYPE(duckdb_bind_double))(
    duckdb_prepared_statement prepared_statement,
    idx_t param_idx,
    double val
);
typedef duckdb_state (*TO_DUCKDB_FUNCTION_TYPE(duckdb_bind_int32))(
    duckdb_prepared_statement prepared_statement,
    idx_t param_idx,
    int32_t val
);
typedef duckdb_state (*TO_DUCKDB_FUNCTION_TYPE(duckdb_bind_boolean))(
    duckdb_prepared_statement prepared_statement,
    idx_t param_idx,
    bool val
);
typedef duckdb_state (*TO_DUCKDB_FUNCTION_TYPE(duckdb_bind_varchar))(
    duckdb_prepared_statement prepared_statement,
    idx_t param_idx,
    const char *val
);
typedef duckdb_state (*TO_DUCKDB_FUNCTION_TYPE(duckdb_bind_null))(
    duckdb_prepared_statement prepared_statement,
    idx_t param_idx
);

/* Result metadata */
typedef idx_t (*TO_DUCKDB_FUNCTION_TYPE(duckdb_column_count))(
    duckdb_result *result
);
typedef const char *(*TO_DUCKDB_FUNCTION_TYPE(duckdb_column_name))(
    duckdb_result *result,
    idx_t col
);
typedef duckdb_type (*TO_DUCKDB_FUNCTION_TYPE(duckdb_column_type))(
    duckdb_result *result,
    idx_t col
);
typedef duckdb_logical_type (*TO_DUCKDB_FUNCTION_TYPE(duckdb_column_logical_type))(
    duckdb_result *result,
    idx_t col
);

/* Logical types */
typedef duckdb_logical_type (*TO_DUCKDB_FUNCTION_TYPE(duckdb_create_logical_type))(
    duckdb_type type
);
typedef duckdb_type (*TO_DUCKDB_FUNCTION_TYPE(duckdb_get_type_id))(
    duckdb_logical_type type
);
typedef duckdb_type (*TO_DUCKDB_FUNCTION_TYPE(duckdb_decimal_internal_type))(
    duckdb_logical_type type
);
typedef uint8_t (*TO_DUCKDB_FUNCTION_TYPE(duckdb_decimal_scale))(
    duckdb_logical_type type
);
typedef void (*TO_DUCKDB_FUNCTION_TYPE(duckdb_destroy_logical_type))(
    duckdb_logical_type *type
);

/* Value API */
typedef duckdb_logical_type (*TO_DUCKDB_FUNCTION_TYPE(duckdb_get_value_type))(
    duckdb_value val
);
typedef int32_t (*TO_DUCKDB_FUNCTION_TYPE(duckdb_get_int32))(
    duckdb_value val
);
typedef bool (*TO_DUCKDB_FUNCTION_TYPE(duckdb_get_bool))(
    duckdb_value val
);
typedef bool (*TO_DUCKDB_FUNCTION_TYPE(duckdb_is_null_value))(
    duckdb_value value
);
typedef void (*TO_DUCKDB_FUNCTION_TYPE(duckdb_destroy_value))(
    duckdb_value *val
);

/* Data chunks and vectors */
typedef duckdb_data_chunk (*TO_DUCKDB_FUNCTION_TYPE(duckdb_fetch_chunk))(
    duckdb_result result
);
typedef idx_t (*TO_DUCKDB_FUNCTION_TYPE(duckdb_data_chunk_get_size))(
    duckdb_data_chunk chunk
);
typedef void (*TO_DUCKDB_FUNCTION_TYPE(duckdb_data_chunk_set_size))(
    duckdb_data_chunk chunk,
    idx_t size
);
typedef void (*TO_DUCKDB_FUNCTION_TYPE(duckdb_destroy_data_chunk))(
    duckdb_data_chunk *chunk
);

typedef duckdb_vector (*TO_DUCKDB_FUNCTION_TYPE(duckdb_data_chunk_get_vector))(
    duckdb_data_chunk chunk,
    idx_t col_idx
);
typedef void *(*TO_DUCKDB_FUNCTION_TYPE(duckdb_vector_get_data))(
    duckdb_vector vector
);
typedef uint64_t *(*TO_DUCKDB_FUNCTION_TYPE(duckdb_vector_get_validity))(
    duckdb_vector vector
);
typedef void (*TO_DUCKDB_FUNCTION_TYPE(duckdb_vector_assign_string_element))(
    duckdb_vector vector,
    idx_t index,
    const char *str
);
typedef void (*TO_DUCKDB_FUNCTION_TYPE(duckdb_vector_ensure_validity_writable))(
    duckdb_vector vector
);
typedef idx_t (*TO_DUCKDB_FUNCTION_TYPE(duckdb_vector_size))(void);
typedef void (*TO_DUCKDB_FUNCTION_TYPE(duckdb_validity_set_row_invalid))(
    uint64_t *validity,
    idx_t row
);

/* String helpers */
typedef uint32_t (*TO_DUCKDB_FUNCTION_TYPE(duckdb_string_t_length))(
    duckdb_string_t string
);
typedef const char *(*TO_DUCKDB_FUNCTION_TYPE(duckdb_string_t_data))(
    duckdb_string_t *string
);

/* Numeric and date/time helpers */
typedef double (*TO_DUCKDB_FUNCTION_TYPE(duckdb_hugeint_to_double))(
    duckdb_hugeint val
);
typedef double (*TO_DUCKDB_FUNCTION_TYPE(duckdb_uhugeint_to_double))(
    duckdb_uhugeint val
);
typedef duckdb_time_tz_struct (*TO_DUCKDB_FUNCTION_TYPE(duckdb_from_time_tz))(
    duckdb_time_tz micros
);

/* Table function registration */
typedef duckdb_table_function (*TO_DUCKDB_FUNCTION_TYPE(duckdb_create_table_function))(void);
typedef void (*TO_DUCKDB_FUNCTION_TYPE(duckdb_destroy_table_function))(
    duckdb_table_function *table_function
);
typedef duckdb_state (*TO_DUCKDB_FUNCTION_TYPE(duckdb_register_table_function))(
    duckdb_connection con,
    duckdb_table_function function
);

typedef void (*TO_DUCKDB_FUNCTION_TYPE(duckdb_table_function_set_name))(
    duckdb_table_function table_function,
    const char *name
);
typedef void (*TO_DUCKDB_FUNCTION_TYPE(duckdb_table_function_add_parameter))(
    duckdb_table_function table_function,
    duckdb_logical_type type
);
typedef void (*TO_DUCKDB_FUNCTION_TYPE(duckdb_table_function_add_named_parameter))(
    duckdb_table_function table_function,
    const char *name,
    duckdb_logical_type type
);
typedef void (*TO_DUCKDB_FUNCTION_TYPE(duckdb_table_function_set_bind))(
    duckdb_table_function table_function,
    duckdb_table_function_bind_t bind
);
typedef void (*TO_DUCKDB_FUNCTION_TYPE(duckdb_table_function_set_init))(
    duckdb_table_function table_function,
    duckdb_table_function_init_t init
);
typedef void (*TO_DUCKDB_FUNCTION_TYPE(duckdb_table_function_set_function))(
    duckdb_table_function table_function,
    duckdb_table_function_t function
);
typedef void (*TO_DUCKDB_FUNCTION_TYPE(duckdb_table_function_set_extra_info))(
    duckdb_table_function table_function,
    void *extra_info,
    duckdb_delete_callback_t destroy
);

/* Table function bind API */
typedef void *(*TO_DUCKDB_FUNCTION_TYPE(duckdb_bind_get_extra_info))(
    duckdb_bind_info info
);
typedef void (*TO_DUCKDB_FUNCTION_TYPE(duckdb_bind_set_bind_data))(
    duckdb_bind_info info,
    void *bind_data,
    duckdb_delete_callback_t destroy
);
typedef duckdb_value (*TO_DUCKDB_FUNCTION_TYPE(duckdb_bind_get_parameter))(
    duckdb_bind_info info,
    idx_t index
);
typedef duckdb_value (*TO_DUCKDB_FUNCTION_TYPE(duckdb_bind_get_named_parameter))(
    duckdb_bind_info info,
    const char *name
);
typedef idx_t (*TO_DUCKDB_FUNCTION_TYPE(duckdb_bind_get_parameter_count))(
    duckdb_bind_info info
);
typedef void (*TO_DUCKDB_FUNCTION_TYPE(duckdb_bind_add_result_column))(
    duckdb_bind_info info,
    const char *name,
    duckdb_logical_type type
);
typedef void (*TO_DUCKDB_FUNCTION_TYPE(duckdb_bind_set_cardinality))(
    duckdb_bind_info info,
    idx_t cardinality,
    bool is_exact
);
typedef void (*TO_DUCKDB_FUNCTION_TYPE(duckdb_bind_set_error))(
    duckdb_bind_info info,
    const char *error
);

/* Table function init/scan API */
typedef void *(*TO_DUCKDB_FUNCTION_TYPE(duckdb_init_get_bind_data))(
    duckdb_init_info info
);
typedef void (*TO_DUCKDB_FUNCTION_TYPE(duckdb_init_set_init_data))(
    duckdb_init_info info,
    void *init_data,
    duckdb_delete_callback_t destroy
);
typedef void (*TO_DUCKDB_FUNCTION_TYPE(duckdb_init_set_error))(
    duckdb_init_info info,
    const char *error
);
typedef void *(*TO_DUCKDB_FUNCTION_TYPE(duckdb_function_get_init_data))(
    duckdb_function_info info
);
typedef void (*TO_DUCKDB_FUNCTION_TYPE(duckdb_function_set_error))(
    duckdb_function_info info,
    const char *error
);

/* Scalar function API */
typedef duckdb_state (*TO_DUCKDB_FUNCTION_TYPE(duckdb_register_scalar_function))(
    duckdb_connection con,
    duckdb_scalar_function scalar_function
);
typedef duckdb_scalar_function (*TO_DUCKDB_FUNCTION_TYPE(duckdb_create_scalar_function))();
typedef void (*TO_DUCKDB_FUNCTION_TYPE(duckdb_scalar_function_set_name))(
    duckdb_scalar_function scalar_function,
    const char *name
);
typedef void (*TO_DUCKDB_FUNCTION_TYPE(duckdb_scalar_function_add_parameter))(
    duckdb_scalar_function scalar_function,
    duckdb_logical_type type
);
typedef void (*TO_DUCKDB_FUNCTION_TYPE(duckdb_scalar_function_set_return_type))(
    duckdb_scalar_function scalar_function,
    duckdb_logical_type type
);
typedef void (*TO_DUCKDB_FUNCTION_TYPE(duckdb_destroy_scalar_function))(
    duckdb_scalar_function *scalar_function
);
typedef void (*TO_DUCKDB_FUNCTION_TYPE(duckdb_scalar_function_set_function))(
    duckdb_scalar_function scalar_function,
    duckdb_scalar_function_t function
);
typedef void (*TO_DUCKDB_FUNCTION_TYPE(duckdb_scalar_function_set_error))(
    duckdb_function_info info,
    const char *error
);

/* Version */
#define DUCKDB_VERSION_FUNCTIONS(X) \
X(duckdb_library_version, DUCKDB_LIBRARY_VERSION)

/* Database lifecycle */
#define DUCKDB_DATABASE_FUNCTIONS(X) \
X(duckdb_open, DUCKDB_OPEN) \
X(duckdb_close, DUCKDB_CLOSE) \
X(duckdb_connect, DUCKDB_CONNECT) \
X(duckdb_disconnect, DUCKDB_DISCONNECT)

/* Query execution */
#define DUCKDB_QUERY_FUNCTIONS(X) \
X(duckdb_query, DUCKDB_QUERY) \
X(duckdb_result_error, DUCKDB_RESULT_ERROR) \
X(duckdb_destroy_result, DUCKDB_DESTROY_RESULT)

/* Statement extraction */
#define DUCKDB_EXTRACT_FUNCTIONS(X) \
X(duckdb_extract_statements, DUCKDB_EXTRACT_STATEMENTS) \
X(duckdb_extract_statements_error, DUCKDB_EXTRACT_STATEMENTS_ERROR) \
X(duckdb_destroy_extracted, DUCKDB_DESTROY_EXTRACTED)

/* Prepared statements */
#define DUCKDB_PREPARE_FUNCTIONS(X) \
X(duckdb_prepare_extracted_statement, DUCKDB_PREPARE_EXTRACTED_STATEMENT) \
X(duckdb_prepare_error, DUCKDB_PREPARE_ERROR) \
X(duckdb_execute_prepared, DUCKDB_EXECUTE_PREPARED) \
X(duckdb_nparams, DUCKDB_NPARAMS) \
X(duckdb_destroy_prepare, DUCKDB_DESTROY_PREPARE)

/* Parameter binding */
#define DUCKDB_BINDING_FUNCTIONS(X) \
X(duckdb_bind_double, DUCKDB_BIND_DOUBLE) \
X(duckdb_bind_int32, DUCKDB_BIND_INT32) \
X(duckdb_bind_boolean, DUCKDB_BIND_BOOLEAN) \
X(duckdb_bind_null, DUCKDB_BIND_NULL) \
X(duckdb_bind_varchar, DUCKDB_BIND_VARCHAR)

/* Result metadata */
#define DUCKDB_RESULT_META_FUNCTIONS(X) \
X(duckdb_column_count, DUCKDB_COLUMN_COUNT) \
X(duckdb_column_name, DUCKDB_COLUMN_NAME) \
X(duckdb_column_type, DUCKDB_COLUMN_TYPE) \
X(duckdb_column_logical_type, DUCKDB_COLUMN_LOGICAL_TYPE)

/* Logical types */
#define DUCKDB_LOGICAL_TYPE_FUNCTIONS(X) \
X(duckdb_create_logical_type, DUCKDB_CREATE_LOGICAL_TYPE) \
X(duckdb_get_type_id, DUCKDB_GET_TYPE_ID) \
X(duckdb_decimal_internal_type, DUCKDB_DECIMAL_INTERNAL_TYPE) \
X(duckdb_decimal_scale, DUCKDB_DECIMAL_SCALE) \
X(duckdb_destroy_logical_type, DUCKDB_DESTROY_LOGICAL_TYPE)

/* Value API */
#define DUCKDB_VALUE_FUNCTIONS(X) \
X(duckdb_get_value_type, DUCKDB_GET_VALUE_TYPE) \
X(duckdb_get_int32, DUCKDB_GET_INT32) \
X(duckdb_get_bool, DUCKDB_GET_BOOL) \
X(duckdb_is_null_value, DUCKDB_IS_NULL_VALUE) \
X(duckdb_destroy_value, DUCKDB_DESTROY_VALUE)

/* Chunks and vectors */
#define DUCKDB_VECTOR_FUNCTIONS(X) \
X(duckdb_fetch_chunk, DUCKDB_FETCH_CHUNK) \
X(duckdb_data_chunk_get_size, DUCKDB_DATA_CHUNK_GET_SIZE) \
X(duckdb_data_chunk_set_size, DUCKDB_DATA_CHUNK_SET_SIZE) \
X(duckdb_destroy_data_chunk, DUCKDB_DESTROY_DATA_CHUNK) \
X(duckdb_data_chunk_get_vector, DUCKDB_DATA_CHUNK_GET_VECTOR) \
X(duckdb_vector_get_data, DUCKDB_VECTOR_GET_DATA) \
X(duckdb_vector_get_validity, DUCKDB_VECTOR_GET_VALIDITY) \
X(duckdb_vector_assign_string_element, DUCKDB_VECTOR_ASSIGN_STRING_ELEMENT) \
X(duckdb_vector_ensure_validity_writable, DUCKDB_VECTOR_ENSURE_VALIDITY_WRITABLE) \
X(duckdb_vector_size, DUCKDB_VECTOR_SIZE) \
X(duckdb_validity_set_row_invalid, DUCKDB_VALIDITY_SET_ROW_INVALID)

/* String helpers */
#define DUCKDB_STRING_FUNCTIONS(X) \
X(duckdb_string_t_length, DUCKDB_STRING_T_LENGTH) \
X(duckdb_string_t_data, DUCKDB_STRING_T_DATA)

/* Numeric and date/time helpers */
#define DUCKDB_UTILITY_FUNCTIONS(X) \
X(duckdb_hugeint_to_double, DUCKDB_HUGEINT_TO_DOUBLE) \
X(duckdb_uhugeint_to_double, DUCKDB_UHUGEINT_TO_DOUBLE) \
X(duckdb_from_time_tz, DUCKDB_FROM_TIME_TZ)

/* Table function registration */
#define DUCKDB_TABLE_FUNCTION_FUNCTIONS(X) \
X(duckdb_create_table_function, DUCKDB_CREATE_TABLE_FUNCTION) \
X(duckdb_destroy_table_function, DUCKDB_DESTROY_TABLE_FUNCTION) \
X(duckdb_register_table_function, DUCKDB_REGISTER_TABLE_FUNCTION) \
X(duckdb_table_function_set_name, DUCKDB_TABLE_FUNCTION_SET_NAME) \
X(duckdb_table_function_add_parameter, DUCKDB_TABLE_FUNCTION_ADD_PARAMETER) \
X(duckdb_table_function_add_named_parameter, DUCKDB_TABLE_FUNCTION_ADD_NAMED_PARAMETER) \
X(duckdb_table_function_set_bind, DUCKDB_TABLE_FUNCTION_SET_BIND) \
X(duckdb_table_function_set_init, DUCKDB_TABLE_FUNCTION_SET_INIT) \
X(duckdb_table_function_set_function, DUCKDB_TABLE_FUNCTION_SET_FUNCTION) \
X(duckdb_table_function_set_extra_info, DUCKDB_TABLE_FUNCTION_SET_EXTRA_INFO)

/* Table function bind API */
#define DUCKDB_BIND_API_FUNCTIONS(X) \
X(duckdb_bind_get_extra_info, DUCKDB_BIND_GET_EXTRA_INFO) \
X(duckdb_bind_set_bind_data, DUCKDB_BIND_SET_BIND_DATA) \
X(duckdb_bind_get_parameter, DUCKDB_BIND_GET_PARAMETER) \
X(duckdb_bind_get_named_parameter, DUCKDB_BIND_GET_NAMED_PARAMETER) \
X(duckdb_bind_get_parameter_count, DUCKDB_BIND_GET_PARAMETER_COUNT) \
X(duckdb_bind_add_result_column, DUCKDB_BIND_ADD_RESULT_COLUMN) \
X(duckdb_bind_set_cardinality, DUCKDB_BIND_SET_CARDINALITY) \
X(duckdb_bind_set_error, DUCKDB_BIND_SET_ERROR)

/* Table function init/scan API */
#define DUCKDB_SCAN_API_FUNCTIONS(X) \
X(duckdb_init_get_bind_data, DUCKDB_INIT_GET_BIND_DATA) \
X(duckdb_init_set_init_data, DUCKDB_INIT_SET_INIT_DATA) \
X(duckdb_init_set_error, DUCKDB_INIT_SET_ERROR) \
X(duckdb_function_get_init_data, DUCKDB_FUNCTION_GET_INIT_DATA) \
X(duckdb_function_set_error, DUCKDB_FUNCTION_SET_ERROR)

/* Scalar function API */
#define DUCKDB_SCALAR_API_FUNCTIONS(X) \
X(duckdb_register_scalar_function, DUCKDB_REGISTER_SCALAR_FUNCTION) \
X(duckdb_create_scalar_function, DUCKDB_CREATE_SCALAR_FUNCTION) \
X(duckdb_scalar_function_set_name, DUCKDB_SCALAR_FUNCTION_SET_NAME) \
X(duckdb_scalar_function_add_parameter, DUCKDB_SCALAR_FUNCTION_ADD_PARAMETER) \
X(duckdb_scalar_function_set_return_type, DUCKDB_SCALAR_FUNCTION_SET_RETURN_TYPE) \
X(duckdb_destroy_scalar_function, DUCKDB_DESTROY_SCALAR_FUNCTION) \
X(duckdb_scalar_function_set_function, DUCKDB_SCALAR_FUNCTION_SET_FUNCTION) \
X(duckdb_scalar_function_set_error, DUCKDB_SCALAR_FUNCTION_SET_ERROR)


/* Aggregate all dynamically loaded DuckDB APIs.
 * Note: DUCKDB_VERSION_FUNCTIONS must be resolved first so the
 * loaded DuckDB version can be validated. */
#define DUCKDB_FUNCTION_POINTERS(X) \
DUCKDB_VERSION_FUNCTIONS(X) \
DUCKDB_DATABASE_FUNCTIONS(X) \
DUCKDB_QUERY_FUNCTIONS(X) \
DUCKDB_EXTRACT_FUNCTIONS(X) \
DUCKDB_PREPARE_FUNCTIONS(X) \
DUCKDB_BINDING_FUNCTIONS(X) \
DUCKDB_RESULT_META_FUNCTIONS(X) \
DUCKDB_LOGICAL_TYPE_FUNCTIONS(X) \
DUCKDB_VALUE_FUNCTIONS(X) \
DUCKDB_VECTOR_FUNCTIONS(X) \
DUCKDB_STRING_FUNCTIONS(X) \
DUCKDB_UTILITY_FUNCTIONS(X) \
DUCKDB_TABLE_FUNCTION_FUNCTIONS(X) \
DUCKDB_BIND_API_FUNCTIONS(X) \
DUCKDB_SCAN_API_FUNCTIONS(X) \
DUCKDB_SCALAR_API_FUNCTIONS(X)

/* Imported DuckDB function pointers */
#define DECLARE_DUCKDB_FUNCTION_POINTER(duckdb_name, func) extern TO_DUCKDB_FUNCTION_TYPE(duckdb_name) func;
DUCKDB_FUNCTION_POINTERS(DECLARE_DUCKDB_FUNCTION_POINTER)
#undef DECLARE_DUCKDB_FUNCTION_POINTER

/*
 * Load the DuckDB DLL and resolve all required APIs.
 *
 * Returns the loaded module handle on success.
 * Returns NULL on failure.
 *
 * The caller owns the returned module handle and
 * must release it with FreeLibrary().
 */
HMODULE load_duckdb(
    const HWND hwnd,
    const wchar_t *caller_path,
    const wchar_t *dllname
);

#endif /* DB_LIB_LOADER_H */
