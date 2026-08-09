#ifndef DB_LIB_LOADER_H
#define DB_LIB_LOADER_H

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include "duckdb.h"

#define DUCKDB_REQUIRED_VERSION         "1.5.0"

#define TO_DUCKDB_FUNC_TYPE(x) x##_t

// duckdb function typedef
typedef const char *(*TO_DUCKDB_FUNC_TYPE(duckdb_library_version))(void);
typedef duckdb_state (*TO_DUCKDB_FUNC_TYPE(duckdb_open))(const char *path, duckdb_database *out_database); 
typedef void (*TO_DUCKDB_FUNC_TYPE(duckdb_close))(duckdb_database *database); 
typedef duckdb_state (*TO_DUCKDB_FUNC_TYPE(duckdb_connect))(duckdb_database database, duckdb_connection *out_connection); 
typedef void (*TO_DUCKDB_FUNC_TYPE(duckdb_disconnect))(duckdb_connection *connection); 
typedef void (*TO_DUCKDB_FUNC_TYPE(duckdb_destroy_result))(duckdb_result *result);
typedef duckdb_data_chunk (*TO_DUCKDB_FUNC_TYPE(duckdb_fetch_chunk))(duckdb_result result);
typedef idx_t (*TO_DUCKDB_FUNC_TYPE(duckdb_data_chunk_get_size))(duckdb_data_chunk chunk);
typedef void (*TO_DUCKDB_FUNC_TYPE(duckdb_destroy_data_chunk))(duckdb_data_chunk *chunk);
typedef const char *(*TO_DUCKDB_FUNC_TYPE(duckdb_column_name))(duckdb_result *result, idx_t col);
typedef duckdb_vector (*TO_DUCKDB_FUNC_TYPE(duckdb_data_chunk_get_vector))(duckdb_data_chunk chunk, idx_t col_idx);
typedef void *(*TO_DUCKDB_FUNC_TYPE(duckdb_vector_get_data))(duckdb_vector vector);
typedef uint64_t *(*TO_DUCKDB_FUNC_TYPE(duckdb_vector_get_validity))(duckdb_vector vector);
typedef duckdb_type (*TO_DUCKDB_FUNC_TYPE(duckdb_column_type))(duckdb_result *result, idx_t col);
typedef duckdb_logical_type (*TO_DUCKDB_FUNC_TYPE(duckdb_column_logical_type))(duckdb_result *result, idx_t col);
typedef bool (*TO_DUCKDB_FUNC_TYPE(duckdb_string_is_inlined))(duckdb_string_t string);
typedef uint8_t (*TO_DUCKDB_FUNC_TYPE(duckdb_decimal_scale))(duckdb_logical_type type);
typedef const char *(*TO_DUCKDB_FUNC_TYPE(duckdb_result_error))(duckdb_result *result);
typedef duckdb_type (*TO_DUCKDB_FUNC_TYPE(duckdb_decimal_internal_type))(duckdb_logical_type type);
typedef void (*TO_DUCKDB_FUNC_TYPE(duckdb_destroy_logical_type))(duckdb_logical_type *type);
typedef idx_t (*TO_DUCKDB_FUNC_TYPE(duckdb_column_count))(duckdb_result *result);
typedef void (*TO_DUCKDB_FUNC_TYPE(duckdb_destroy_prepare))(duckdb_prepared_statement *prepared_statement);
typedef const char *(*TO_DUCKDB_FUNC_TYPE(duckdb_prepare_error))(duckdb_prepared_statement prepared_statement);
typedef duckdb_state (*TO_DUCKDB_FUNC_TYPE(duckdb_execute_prepared))(duckdb_prepared_statement prepared_statement, duckdb_result *out_result);
typedef idx_t (*TO_DUCKDB_FUNC_TYPE(duckdb_nparams))(duckdb_prepared_statement prepared_statement);
typedef duckdb_state (*TO_DUCKDB_FUNC_TYPE(duckdb_bind_double))(duckdb_prepared_statement prepared_statement, idx_t param_idx, double val);
typedef duckdb_state (*TO_DUCKDB_FUNC_TYPE(duckdb_bind_int32))(duckdb_prepared_statement prepared_statement, idx_t param_idx, int32_t val);
typedef duckdb_state (*TO_DUCKDB_FUNC_TYPE(duckdb_bind_boolean))(duckdb_prepared_statement prepared_statement, idx_t param_idx, bool val);
typedef duckdb_state (*TO_DUCKDB_FUNC_TYPE(duckdb_bind_null))(duckdb_prepared_statement prepared_statement, idx_t param_idx);
typedef duckdb_state (*TO_DUCKDB_FUNC_TYPE(duckdb_bind_varchar))(duckdb_prepared_statement prepared_statement, idx_t param_idx, const char *val);
typedef duckdb_logical_type (*TO_DUCKDB_FUNC_TYPE(duckdb_create_logical_type))(duckdb_type type);
typedef void (*TO_DUCKDB_FUNC_TYPE(duckdb_bind_set_bind_data))(duckdb_bind_info info, void *bind_data, duckdb_delete_callback_t destroy);
typedef duckdb_value (*TO_DUCKDB_FUNC_TYPE(duckdb_bind_get_parameter))(duckdb_bind_info info, idx_t index);
typedef idx_t (*TO_DUCKDB_FUNC_TYPE(duckdb_bind_get_parameter_count))(duckdb_bind_info info);
typedef void (*TO_DUCKDB_FUNC_TYPE(duckdb_bind_set_error))(duckdb_bind_info info, const char *error);
typedef duckdb_logical_type (*TO_DUCKDB_FUNC_TYPE(duckdb_get_value_type))(duckdb_value val);
typedef int32_t (*TO_DUCKDB_FUNC_TYPE(duckdb_get_int32))(duckdb_value val);
typedef void (*TO_DUCKDB_FUNC_TYPE(duckdb_destroy_value))(duckdb_value *val);
typedef void (*TO_DUCKDB_FUNC_TYPE(duckdb_table_function_set_extra_info))(duckdb_table_function table_function, void *extra_info, duckdb_delete_callback_t destroy);
typedef void *(*TO_DUCKDB_FUNC_TYPE(duckdb_bind_get_extra_info))(duckdb_bind_info info);
typedef duckdb_table_function (*TO_DUCKDB_FUNC_TYPE(duckdb_create_table_function))();
typedef void (*TO_DUCKDB_FUNC_TYPE(duckdb_table_function_set_name))(duckdb_table_function table_function, const char *name);
typedef void (*TO_DUCKDB_FUNC_TYPE(duckdb_table_function_add_parameter))(duckdb_table_function table_function, duckdb_logical_type type);
typedef void (*TO_DUCKDB_FUNC_TYPE(duckdb_table_function_set_bind))(duckdb_table_function table_function, duckdb_table_function_bind_t bind);
typedef void (*TO_DUCKDB_FUNC_TYPE(duckdb_table_function_set_init))(duckdb_table_function table_function, duckdb_table_function_init_t init);
typedef void (*TO_DUCKDB_FUNC_TYPE(duckdb_table_function_set_function))(duckdb_table_function table_function, duckdb_table_function_t function);
typedef duckdb_state (*TO_DUCKDB_FUNC_TYPE(duckdb_register_table_function))(duckdb_connection con, duckdb_table_function function);
typedef duckdb_type (*TO_DUCKDB_FUNC_TYPE(duckdb_get_type_id))(duckdb_logical_type type);
typedef void (*TO_DUCKDB_FUNC_TYPE(duckdb_bind_add_result_column))(duckdb_bind_info info, const char *name, duckdb_logical_type type);
typedef void (*TO_DUCKDB_FUNC_TYPE(duckdb_bind_set_cardinality))(duckdb_bind_info info, idx_t cardinality, bool is_exact);
typedef void *(*TO_DUCKDB_FUNC_TYPE(duckdb_init_get_bind_data))(duckdb_init_info info);
typedef void *(*TO_DUCKDB_FUNC_TYPE(duckdb_function_get_init_data))(duckdb_function_info info);
typedef void (*TO_DUCKDB_FUNC_TYPE(duckdb_validity_set_row_invalid))(uint64_t *validity, idx_t row);
typedef void (*TO_DUCKDB_FUNC_TYPE(duckdb_data_chunk_set_size))(duckdb_data_chunk chunk, idx_t size);
typedef void (*TO_DUCKDB_FUNC_TYPE(duckdb_init_set_init_data))(duckdb_init_info info, void *init_data, duckdb_delete_callback_t destroy);
typedef void (*TO_DUCKDB_FUNC_TYPE(duckdb_init_set_error))(duckdb_init_info info, const char *error);
typedef void (*TO_DUCKDB_FUNC_TYPE(duckdb_destroy_table_function))(duckdb_table_function *table_function);
typedef void (*TO_DUCKDB_FUNC_TYPE(duckdb_vector_assign_string_element))(duckdb_vector vector, idx_t index, const char *str);
typedef void (*TO_DUCKDB_FUNC_TYPE(duckdb_vector_ensure_validity_writable))(duckdb_vector vector);
typedef void (*TO_DUCKDB_FUNC_TYPE(duckdb_function_set_error))(duckdb_function_info info, const char *error);
typedef idx_t (*TO_DUCKDB_FUNC_TYPE(duckdb_vector_size))();
typedef uint32_t (*TO_DUCKDB_FUNC_TYPE(duckdb_string_t_length))(duckdb_string_t string);
typedef const char *(*TO_DUCKDB_FUNC_TYPE(duckdb_string_t_data))(duckdb_string_t *string);
typedef double (*TO_DUCKDB_FUNC_TYPE(duckdb_hugeint_to_double))(duckdb_hugeint val);
typedef double (*TO_DUCKDB_FUNC_TYPE(duckdb_uhugeint_to_double))(duckdb_uhugeint val);
typedef duckdb_time_tz_struct (*TO_DUCKDB_FUNC_TYPE(duckdb_from_time_tz))(duckdb_time_tz micros);
typedef idx_t (*TO_DUCKDB_FUNC_TYPE(duckdb_extract_statements))(duckdb_connection connection, const char *query, duckdb_extracted_statements *out_extracted_statements);
typedef void (*TO_DUCKDB_FUNC_TYPE(duckdb_destroy_extracted))(duckdb_extracted_statements *extracted_statements);
typedef const char *(*TO_DUCKDB_FUNC_TYPE(duckdb_extract_statements_error))(duckdb_extracted_statements extracted_statements);
typedef duckdb_state (*TO_DUCKDB_FUNC_TYPE(duckdb_prepare_extracted_statement))(duckdb_connection connection, duckdb_extracted_statements extracted_statements, idx_t index, duckdb_prepared_statement *out_prepared_statement);
typedef duckdb_state (*TO_DUCKDB_FUNC_TYPE(duckdb_query))(duckdb_connection connection, const char *query, duckdb_result *out_result);

#define DUCKDB_FUNCS(X) \
X(duckdb_library_version, DUCKDB_LIBRARY_VERSION) \
X(duckdb_open, DUCKDB_OPEN) \
X(duckdb_close, DUCKDB_CLOSE) \
X(duckdb_connect, DUCKDB_CONNECT) \
X(duckdb_disconnect, DUCKDB_DISCONNECT) \
X(duckdb_destroy_result, DUCKDB_DESTROY_RESULT) \
X(duckdb_fetch_chunk, DUCKDB_FETCH_CHUNK) \
X(duckdb_data_chunk_get_size, DUCKDB_DATA_CHUNK_GET_SIZE) \
X(duckdb_destroy_data_chunk, DUCKDB_DESTROY_DATA_CHUNK) \
X(duckdb_column_name, DUCKDB_COLUMN_NAME) \
X(duckdb_data_chunk_get_vector, DUCKDB_DATA_CHUNK_GET_VECTOR) \
X(duckdb_vector_get_data, DUCKDB_VECTOR_GET_DATA) \
X(duckdb_vector_get_validity, DUCKDB_VECTOR_GET_VALIDITY) \
X(duckdb_column_type, DUCKDB_COLUMN_TYPE) \
X(duckdb_column_logical_type, DUCKDB_COLUMN_LOGICAL_TYPE) \
X(duckdb_string_is_inlined, DUCKDB_STRING_IS_INLINED) \
X(duckdb_decimal_scale, DUCKDB_DECIMAL_SCALE) \
X(duckdb_result_error, DUCKDB_RESULT_ERROR) \
X(duckdb_decimal_internal_type, DUCKDB_DECIMAL_INTERNAL_TYPE) \
X(duckdb_destroy_logical_type, DUCKDB_DESTROY_LOGICAL_TYPE) \
X(duckdb_column_count, DUCKDB_COLUMN_COUNT) \
X(duckdb_destroy_prepare, DUCKDB_DESTROY_PREPARE) \
X(duckdb_prepare_error, DUCKDB_PREPARE_ERROR) \
X(duckdb_execute_prepared, DUCKDB_EXECUTE_PREPARED) \
X(duckdb_nparams, DUCKDB_NPARAMS) \
X(duckdb_bind_double, DUCKDB_BIND_DOUBLE) \
X(duckdb_bind_int32, DUCKDB_BIND_INT32) \
X(duckdb_bind_boolean, DUCKDB_BIND_BOOLEAN) \
X(duckdb_bind_null, DUCKDB_BIND_NULL) \
X(duckdb_bind_varchar, DUCKDB_BIND_VARCHAR) \
X(duckdb_create_logical_type, DUCKDB_CREATE_LOGICAL_TYPE) \
X(duckdb_bind_add_result_column, DUCKDB_BIND_ADD_RESULT_COLUMN) \
X(duckdb_bind_set_bind_data, DUCKDB_BIND_SET_BIND_DATA) \
X(duckdb_bind_get_parameter, DUCKDB_BIND_GET_PARAMETER) \
X(duckdb_bind_get_parameter_count, DUCKDB_BIND_GET_PARAMETER_COUNT) \
X(duckdb_bind_set_error, DUCKDB_BIND_SET_ERROR) \
X(duckdb_get_value_type, DUCKDB_GET_VALUE_TYPE) \
X(duckdb_get_int32, DUCKDB_GET_INT32) \
X(duckdb_destroy_value, DUCKDB_DESTROY_VALUE) \
X(duckdb_table_function_set_extra_info, DUCKDB_TABLE_FUNCTION_SET_EXTRA_INFO) \
X(duckdb_bind_get_extra_info, DUCKDB_BIND_GET_EXTRA_INFO) \
X(duckdb_create_table_function, DUCKDB_CREATE_TABLE_FUNCTION) \
X(duckdb_table_function_set_name, DUCKDB_TABLE_FUNCTION_SET_NAME) \
X(duckdb_table_function_add_parameter, DUCKDB_TABLE_FUNCTION_ADD_PARAMETER) \
X(duckdb_table_function_set_bind, DUCKDB_TABLE_FUNCTION_SET_BIND) \
X(duckdb_table_function_set_init, DUCKDB_TABLE_FUNCTION_SET_INIT) \
X(duckdb_table_function_set_function, DUCKDB_TABLE_FUNCTION_SET_FUNCTION) \
X(duckdb_register_table_function, DUCKDB_REGISTER_TABLE_FUNCTION) \
X(duckdb_get_type_id, DUCKDB_GET_TYPE_ID) \
X(duckdb_init_get_bind_data, DUCKDB_INIT_GET_BIND_DATA) \
X(duckdb_bind_set_cardinality, DUCKDB_BIND_SET_CARDINALITY) \
X(duckdb_function_get_init_data, DUCKDB_FUNCTION_GET_INIT_DATA) \
X(duckdb_data_chunk_set_size, DUCKDB_DATA_CHUNK_SET_SIZE) \
X(duckdb_init_set_init_data, DUCKDB_INIT_SET_INIT_DATA) \
X(duckdb_init_set_error, DUCKDB_INIT_SET_ERROR) \
X(duckdb_destroy_table_function, DUCKDB_DESTROY_TABLE_FUNCTION) \
X(duckdb_vector_assign_string_element, DUCKDB_VECTOR_ASSIGN_STRING_ELEMENT) \
X(duckdb_validity_set_row_invalid, DUCKDB_VALIDITY_SET_ROW_INVALID) \
X(duckdb_vector_ensure_validity_writable, DUCKDB_VECTOR_ENSURE_VALIDITY_WRITABLE) \
X(duckdb_function_set_error, DUCKDB_FUNCTION_SET_ERROR) \
X(duckdb_vector_size, DUCKDB_VECTOR_SIZE) \
X(duckdb_string_t_length, DUCKDB_STRING_T_LENGTH) \
X(duckdb_string_t_data, DUCKDB_STRING_T_DATA) \
X(duckdb_hugeint_to_double, DUCKDB_HUGEINT_TO_DOUBLE) \
X(duckdb_uhugeint_to_double, DUCKDB_UHUGEINT_TO_DOUBLE) \
X(duckdb_from_time_tz, DUCKDB_FROM_TIME_TZ) \
X(duckdb_extract_statements, DUCKDB_EXTRACT_STATEMENTS) \
X(duckdb_destroy_extracted, DUCKDB_DESTROY_EXTRACTED) \
X(duckdb_extract_statements_error, DUCKDB_EXTRACT_STATEMENTS_ERROR) \
X(duckdb_prepare_extracted_statement, DUCKDB_PREPARE_EXTRACTED_STATEMENT) \
X(duckdb_query, DUCKDB_QUERY)

#define DECLARE_FUNC(duckdb_name, func) extern TO_DUCKDB_FUNC_TYPE(duckdb_name) func;
DUCKDB_FUNCS(DECLARE_FUNC)
#undef DECLARE_FUNC

// function prototypes
HMODULE load_duckdb(const HWND hwnd, const wchar_t *caller_path, const wchar_t *dllname);

#endif // DB_LIB_LOADER_H