#include <windows.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "XLCALL.H"
#include "FRAMEWRK.H"

#include "uthash.h"

#include "db_xlrange.h"
#include "helper.h"
#include "db_lib_loader.h"
#include "config.h"

#define ERR_MSG_XLRANGE_INTERNAL            "An internal error occurred."
#define ERR_MSG_XLRANGE_INVALID_PARAM       "Invalid parameter."
#define ERR_MSG_XLRANGE_INVALID_COL_NAME    "Invalid column name. Column names must be non-empty and valid DuckDB identifiers."
#define ERR_MSG_XLRANGE_DOUBLE              "Failed to convert value to DOUBLE."
#define ERR_MSG_XLRANGE_INT                 "Failed to convert value to INTEGER."
#define ERR_MSG_XLRANGE_VARCHAR             "Failed to convert value to VARCHAR."
#define ERR_MSG_XLRANGE_BOOL	            "Failed to convert value to BOOLEAN."

#define GENERATED_COLNAME_SIZE 				30
#define SUFFIX_LEN							20

/* Tracks column-name occurrences for duplicate renaming. */
typedef struct colname_hash_t
{
    char *name;             	/* key */
    size_t count;           	/* occurrences */
    UT_hash_handle hh;
} colname_hash_t;

typedef struct xlrange_context_t
{
    LPXLOPER12      ranges;     /* Excel ranges supplied by caller */
    size_t          nrange;     /* Number of ranges */
} xlrange_context_t;

typedef struct xlrange_bind_data_t
{
    LPXLOPER12      lparray;    /* First data cell (header excluded) */
    size_t          ncols;      /* Number of columns */
    size_t          nrows;      /* Number of data rows */
    duckdb_type     *types;     /* Inferred DuckDB types */
    char            **colnames; /* UTF-8 column names (owned) */
    bool            has_header;
	bool			ignore_errors;
} xlrange_bind_data_t;

typedef struct xlrange_scan_state_t
{
    LPXLOPER12      lparray;    /* First data cell (header excluded) */
    size_t          ncols;      /* Number of columns */
    size_t          nrows;      /* Number of data rows */
    size_t          next_row;   /* Next row to scan */
    duckdb_type     *types;     /* Inferred DuckDB types */
    char            **colnames; /* UTF-8 column names (owned) */
    idx_t           vec_size;   /* Duckdb vector size */
    bool            has_header;
	bool			ignore_errors;
} xlrange_scan_state_t;

/* Return an allocated unique name using numeric suffixes. */
static inline char *make_unique_name
(
    colname_hash_t 	**hash,
    const char 		*name
)
{
    colname_hash_t *entry = NULL;

    HASH_FIND_STR(*hash, name, entry);
    if (!entry)
    {
        entry = malloc(sizeof(*entry));
        if (!entry)
            return NULL;

        entry->name = _strdup(name);
        if (!entry->name)
        {
            free(entry);
            return NULL;
        }

        entry->count = 1;

        HASH_ADD_KEYPTR(
            hh,
            *hash,
            entry->name,
            strlen(entry->name),
            entry
        );

        return _strdup(name);
    }

    entry->count++;

    size_t len =
        strlen(name)
        + 1                  /* '_' */
        + SUFFIX_LEN
        + 1;                 /* '\0' */

    char *new_name = malloc(len);
    if (!new_name)
        return NULL;

    snprintf(
        new_name,
        len,
        "%s_%zu",
        name,
        entry->count - 1
    );

    return new_name;
}

static void format_error_message
(
    char           *buf,
    size_t         buf_size,
    const char     *action,
    const char     *colname,
    long long	   col_idx,
    long long      row_idx,
    const char     *msg,
    bool           has_header
)
{
    if (!buf || buf_size == 0 || !action || !msg)
        return;

    if (has_header)
    {
		if (colname)
		{
			if (row_idx >= 0)
				snprintf(
					buf,
					buf_size,
					"Error %s: Column %s, row %lld: %s",
					action,
					colname,
					row_idx + 2,	/* +1 for 0-based index, +1 for header row */
					msg
				);
			else
				snprintf(
					buf,
					buf_size,
					"Error %s: Column %s: %s",
					action,
					colname,
					msg
				);
		}
		else
		{
			if (row_idx >= 0)
				snprintf(
					buf,
					buf_size,
					"Error %s: Column %lld, row %lld: %s",
					action,
					col_idx + 1,
					row_idx + 2,	/* +1 for 0-based index, +1 for header row */
					msg
				);
			else
				snprintf(
					buf,
					buf_size,
					"Error %s: Column %lld: %s",
					action,
					col_idx + 1,
					msg
				);
		}
    }
    else if (col_idx >= 0)
    {
        if (row_idx >= 0)
            snprintf(
                buf,
                buf_size,
                "Error %s: Column #%lld, row %lld: %s",
                action,
                col_idx + 1,
                row_idx + 1, 	/* +1 for 0-based index */
                msg
            );
        else
            snprintf(
                buf,
                buf_size,
                "Error %s: Column #%lld: %s",
                action,
                col_idx + 1,
                msg
            );
    }
    else
    {
        snprintf(
            buf,
            buf_size,
            "Error %s: %s",
            action,
            msg
        );
    }
}

static void free_bind_data(void *p)
{
    if (!p)
        return;

    xlrange_bind_data_t *bind_data = p;

    free(bind_data->types);

    if (bind_data->colnames)
    {
        for (size_t i = 0; i < bind_data->ncols; i++)
            free(bind_data->colnames[i]);

        free(bind_data->colnames);
    }
	
    free(bind_data);
}

/* Return true for INT32-compatible whole numbers. */
static inline bool is_whole_number(double num)
{
	double n = round(num);

    return fabs(num - n) < EPSILON
            && n >= INT32_MIN
            && n <= INT32_MAX;
}

#define SET_BIND_ERROR(BUF, LEN, MSG) \
    format_error_message( \
        BUF, \
        LEN, \
        "binding xlrange", \
        NULL, \
        -1, \
        -1, \
        MSG, \
        false \
    )

/*
 * Read a required INTEGER positional parameter.
 * Return 1 on success, 0 on null, and -1 on error.
 */
static int get_int_param
(
	duckdb_bind_info	info,
	idx_t 				index,
	int32_t				*result,
	char 				*errmsg,
	size_t				err_buf_size
)
{
	int ok = -1;
	
	duckdb_value val = DUCKDB_BIND_GET_PARAMETER(info, index);
    if (!val)
    {
        SET_BIND_ERROR(errmsg, err_buf_size, ERR_MSG_XLRANGE_INTERNAL);
        goto cleanup;
    }

    if (DUCKDB_IS_NULL_VALUE(val))
    {
        SET_BIND_ERROR(errmsg, err_buf_size, ERR_MSG_XLRANGE_INVALID_PARAM);
		ok = 0;
        goto cleanup;
    }

	/* Owned by val. */
	duckdb_logical_type lt = DUCKDB_GET_VALUE_TYPE(val);
    if(!lt)
    {
        SET_BIND_ERROR(errmsg, err_buf_size, ERR_MSG_XLRANGE_INTERNAL);
        goto cleanup;
    }

    if(DUCKDB_GET_TYPE_ID(lt) != DUCKDB_TYPE_INTEGER)
    {
        SET_BIND_ERROR(errmsg, err_buf_size, ERR_MSG_XLRANGE_INVALID_PARAM);
        goto cleanup;
    }

    *result = DUCKDB_GET_INT32(val);
	ok = 1;

cleanup:

	DUCKDB_DESTROY_VALUE(&val);

	return ok;
}

/*
 * Generate readers for optional named parameters.
 * Return 1 on success, 0 if omitted, and -1 on error.
 */
#define DEFINE_GENERATE_GET_NAMED_PARAM_FUNC(TYPE, TYPE_ENUM, GETTER)				\
static int get_##TYPE##_named_param													\
(																					\
	duckdb_bind_info	info,														\
	const char 			*name,														\
	TYPE 				*result,													\
	char 				*errmsg,													\
	size_t				err_buf_size												\
)																					\
{																					\
	int ok = 0;																		\
																					\
    duckdb_value val = DUCKDB_BIND_GET_NAMED_PARAMETER(info, name);					\
    if (val)																		\
    {																				\
		ok = -1;																	\
																					\
        if (DUCKDB_IS_NULL_VALUE(val))												\
        {																			\
            SET_BIND_ERROR(errmsg, err_buf_size, ERR_MSG_XLRANGE_INVALID_PARAM); 	\
            goto cleanup;															\
        }																			\
																					\
		duckdb_logical_type lt = DUCKDB_GET_VALUE_TYPE(val);						\
        if (!lt)																	\
        {																			\
            SET_BIND_ERROR(errmsg, err_buf_size, ERR_MSG_XLRANGE_INTERNAL); 		\
            goto cleanup;															\
        }																			\
																					\
        if (DUCKDB_GET_TYPE_ID(lt) != TYPE_ENUM)									\
        {																			\
            SET_BIND_ERROR(errmsg, err_buf_size, ERR_MSG_XLRANGE_INVALID_PARAM);	\
            goto cleanup;															\
        }																			\
																					\
        *result = GETTER(val);														\
		ok = 1;																		\
																					\
	cleanup:																		\
																					\
		DUCKDB_DESTROY_VALUE(&val);													\
    }																				\
																					\
	return ok;																		\
}

DEFINE_GENERATE_GET_NAMED_PARAM_FUNC(int,  DUCKDB_TYPE_INTEGER, DUCKDB_GET_INT32)
DEFINE_GENERATE_GET_NAMED_PARAM_FUNC(bool, DUCKDB_TYPE_BOOLEAN, DUCKDB_GET_BOOL)

/* Parse and validate xlrange() parameters. */
static int parse_params
(
	duckdb_bind_info 	info,
	xlrange_context_t	*ctx,
	int32_t             *range_idx,
	size_t              *nsample,
	bool				*all_varchar,
	bool				*has_header,
	bool				*is_strict,
	bool				*ignore_errors,
	char				*errmsg,
	size_t				err_buf_size
)
{
    /* xlrange(index) */
	int32_t range_idx_tmp;
	
    if (DUCKDB_BIND_GET_PARAMETER_COUNT(info) != 1)
    {
        SET_BIND_ERROR(errmsg, err_buf_size, ERR_MSG_XLRANGE_INVALID_PARAM);
        return 0;
    }

    if (get_int_param(info, 0, &range_idx_tmp, errmsg, err_buf_size) != 1)
        return 0;

    if (range_idx_tmp <= 0 || (size_t)range_idx_tmp > ctx->nrange)
    {
        SET_BIND_ERROR(errmsg, err_buf_size, ERR_MSG_XLRANGE_INVALID_PARAM);
        return 0;
    }

    /* xlrange(..., header = true) */
    bool has_header_tmp = true;
    /* xlrange(..., strict = true) */
    bool is_strict_tmp = true;
    /* xlrange(..., all_varchar = false) */
    bool all_varchar_tmp = false;
    /* xlrange(..., ignore_errors = false) */
    bool ignore_errors_tmp = false;

	if (get_bool_named_param(info, "header", &has_header_tmp, errmsg, err_buf_size) == -1
		|| get_bool_named_param(info, "strict", &is_strict_tmp, errmsg, err_buf_size) == -1
		|| get_bool_named_param(info, "all_varchar", &all_varchar_tmp, errmsg, err_buf_size) == -1
		|| get_bool_named_param(info, "ignore_errors", &ignore_errors_tmp, errmsg, err_buf_size) == -1)
    {
        return 0;
    }

    /* xlrange(..., sample = XLRANGE_DEFAULT_SAMPLE_COUNT) */
    int32_t nsample_tmp = XLRANGE_DEFAULT_SAMPLE_COUNT;
    if (!all_varchar_tmp)
    {
		if (get_int_named_param(info, "sample", &nsample_tmp, errmsg, err_buf_size) == -1)
			return 0;
		
		if (nsample_tmp < 0)
		{
			SET_BIND_ERROR(errmsg, err_buf_size, ERR_MSG_XLRANGE_INVALID_PARAM);
			return 0;
		}
    }

	*range_idx		= range_idx_tmp;
    *nsample 		= (size_t)nsample_tmp;
	*has_header 	= has_header_tmp;
	*is_strict 		= is_strict_tmp;
	*all_varchar 	= all_varchar_tmp;
	*ignore_errors	= ignore_errors_tmp;
	
	return 1;
}

/*
 * Read or generate UTF-8 column names.
 *
 * Results must be freed by caller.
 */
static int get_column_names
(
	LPXLOPER12	cell,
	char		**colnames,
	size_t		ncols,
	bool		has_header,
	bool		is_strict,
	char		*errmsg,
	size_t		err_buf_size
)
{
    int				ok = 0;
	size_t			unnamed_idx = 0;
	colname_hash_t 	*hash = NULL;
	XLOPER12 		str_cell;

	str_cell.xltype = xltypeNil;

    for (size_t i=0; i < ncols; i++, cell++)
    {
        char *colname = NULL;

        if (!has_header)
        {
            colname = malloc(GENERATED_COLNAME_SIZE);
            if (!colname)
                goto internal_error;

            snprintf(
                colname,
                GENERATED_COLNAME_SIZE,
                "column_%zu",
                i
            );
        }
		else
		{
			wchar_t *xlstr = NULL;
			
			if (LPXLOPER12_TYPE(cell) == xltypeStr)
			{
				xlstr = cell->val.str;
			}
			else
			{
				if (Excel12f(xlCoerce, &str_cell, 2, cell, TempInt12(xltypeStr)) != xlretSuccess
					|| XLOPER12_TYPE(str_cell) != xltypeStr)
				{
					goto name_error;
				}
				
				xlstr = str_cell.val.str;
			}
			
			if (is_strict)
			{
				if (is_null_or_whitespace_xlstr(xlstr)
					|| (xlstr_to_utf8(&colname, xlstr, NULL) == 0)
					|| !colname)
				{
					goto name_error;
				}
			}
			else
			{
				if (is_null_or_whitespace_xlstr(xlstr))
				{
					colname = malloc(GENERATED_COLNAME_SIZE);
					if (!colname)
						goto internal_error;

					snprintf(
						colname,
						GENERATED_COLNAME_SIZE,
						"unnamed_%zu",
						unnamed_idx
					);

					unnamed_idx++;
				}
				else if (xlstr_to_utf8(&colname, xlstr, NULL) == 0
						 || !colname)
				{
					goto name_error;
				}

				/* Rename duplicate columns in non-strict mode. */
				char *name = colname;
				colname = make_unique_name(&hash, name);
				free(name);
				if (!colname)
					goto internal_error;
			}
		}

		colnames[i] = colname;
		colname = NULL;

		if (XLOPER12_TYPE(str_cell) != xltypeNil)
		{
			Excel12f(xlFree, NULL, 1, &str_cell);
			str_cell.xltype = xltypeNil;
		}

		continue;

	internal_error:

        SET_BIND_ERROR(errmsg, err_buf_size, ERR_MSG_XLRANGE_INTERNAL);
		goto cleanup;

	name_error:

		format_error_message(
			errmsg,
			err_buf_size,
			"binding xlrange",
			NULL,
			(long long)i,
			-1,
			ERR_MSG_XLRANGE_INVALID_COL_NAME,
			has_header
		);
		
		goto cleanup;
		
	cleanup:

		if (XLOPER12_TYPE(str_cell) != xltypeNil)
			Excel12f(xlFree, NULL, 1, &str_cell);
		
		goto free_hash;
	}
	
	ok = 1;

free_hash:

	colname_hash_t *hash_entry, *hash_tmp;
	
	HASH_ITER(hh, hash, hash_entry, hash_tmp)
	{
		HASH_DEL(hash, hash_entry);
		free(hash_entry->name);
		free(hash_entry);
	}

	return ok;
}

/*
 * Return a non-owning trimmed view of s.
 * out_len receives the trimmed length.
 */
static inline char *trim_whitespace(char *s, size_t *out_len)
{
    while (*s && isspace((unsigned char)*s))
        s++;

    if (*s == '\0')
	{
        if (out_len) *out_len = 0;
        return s;
    }

    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end))
        end--;

    if (out_len)
        *out_len = (size_t)(end - s + 1);

    return s;
}

/* Infer the Excel type represented by a string value. */
static inline WORD get_xlstr_represented_type(wchar_t *xlstr, bool *zero_or_one)
{
	if (zero_or_one)
		*zero_or_one = false;

	char *s = NULL;
	if (!xlstr
		|| xlstr_to_utf8(&s, xlstr, NULL) == 0
		|| !s)
	{
		return xltypeNil;
	}
	
	WORD type;
	
	size_t n;
	char *trimmed = trim_whitespace(s, &n);

	switch (n)
	{
		case 0:
			type = xltypeNil;
			goto cleanup;

		case 1:
			switch (trimmed[0])
			{
				case 'Y':
				case 'y':
				case 'T':
				case 't':
				case 'N':
				case 'n':
				case 'F':
				case 'f':
				{
					type = xltypeBool;
					goto cleanup;
				}

				default:
					break;
			}
			
			break;

		case 2:
			if (strncasecmp(trimmed, "no", n) == 0)
			{
				type = xltypeBool;
				goto cleanup;
			}

			break;

		case 3:
			if (strncasecmp(trimmed, "yes", n) == 0)
			{
				type = xltypeBool;
				goto cleanup;
			}
			else if (strncasecmp(trimmed, "n/a", n) == 0)
			{
				type = xltypeNil;
				goto cleanup;
			}

			break;

		case 4:
			if (strncasecmp(trimmed, "true", n) == 0)
			{
				type = xltypeBool;
				goto cleanup;
			}
			else if (strncasecmp(trimmed, "null", n) == 0)
			{
				type = xltypeNil;
				goto cleanup;
			}

			break;

		case 5:
			if (strncasecmp(trimmed, "false", n) == 0)
			{
				type = xltypeBool;
				goto cleanup;
			}

			break;

		default:
			break;
	}

	char *endptr = NULL;
	errno = 0;
	double d = strtod(trimmed, &endptr);
	if (errno != ERANGE 
		&& endptr != trimmed
		&& endptr == trimmed + n
		&& isfinite(d))
	{
		if (is_whole_number(d))
		{
			type = xltypeInt;
			if (zero_or_one)
				*zero_or_one = ((d == 0.0) || (d == 1.0)) ? true : false;

			goto cleanup;
		}
		else
		{
			type = xltypeNum;
			goto cleanup;
		}
	}

	type = xltypeStr;

cleanup:

	free(s);

	return type;
}

/*
 * Infer each column type from sampled data.
 *
 * Results must be freed by caller.
 */
static int infer_types
(
	LPXLOPER12			data,
	size_t				ncols,
	size_t     			nsample,
	size_t				ndatarows,
	bool				all_varchar,
	bool				has_header,
	duckdb_type			*types,
	duckdb_logical_type *logical_types,
	char				*errmsg,
	size_t				err_buf_size
)
{
    for (size_t i=0; i < ncols; i++, data++)
    {
        WORD xltype = xltypeStr;

        if (!all_varchar && ndatarows > 0)
        {
            LPXLOPER12 cell = has_header ? data + ncols : data;

            /* Find the first non-null type candidate. */
            size_t sample_idx;

            for (sample_idx = 0; sample_idx < nsample; sample_idx++, cell += ncols)
            {
                WORD cell_type = LPXLOPER12_TYPE(cell);

                if (cell_type == xltypeInt)
                {
                    xltype = xltypeInt;
                    break;
                }
                else if (cell_type == xltypeNum)
                {
					xltype = is_whole_number(cell->val.num) ? xltypeInt : xltypeNum;
                    break;
                }
				else if (cell_type == xltypeBool)
				{
                    xltype = xltypeBool;
                    break;
				}
                else if (cell_type == xltypeStr)
                {
					WORD type = get_xlstr_represented_type(cell->val.str, NULL);
					if (type != xltypeNil)
					{
						xltype = type;
						break;
					}
                }
            }

            /* Reconcile the candidate with remaining sampled values. */
            if (xltype != xltypeStr)
            {
                cell += ncols;
                sample_idx++;

                for (size_t j = sample_idx; j < nsample; j++, cell += ncols)
                {
                    WORD cell_type = LPXLOPER12_TYPE(cell);

                    if (xltype == xltypeInt)
                    {
						if (cell_type == xltypeInt
							|| cell_type == xltypeNil
							|| cell_type == xltypeMissing
							|| cell_type == xltypeErr)
						{
							continue;
						}
                        if (cell_type == xltypeNum)
                        {
                            if (!is_whole_number(cell->val.num))
                                xltype = xltypeNum;

							continue;
                        }
						else if (cell_type == xltypeStr)
						{
							WORD type = get_xlstr_represented_type(cell->val.str, NULL);
							
							if (type == xltypeNil)
							{
								continue;
							}
							else if (type == xltypeNum)
							{
								xltype = xltypeNum;
								continue;
							}
							else if (type != xltypeInt)
							{
							    xltype = xltypeStr;
								break;
							}
						}
						else
						{
							xltype = xltypeStr;
							break;
						}
                    }
					else if (xltype == xltypeNum)
					{
						if (cell_type == xltypeInt
							|| cell_type == xltypeNum
							|| cell_type == xltypeNil
							|| cell_type == xltypeMissing
							|| cell_type == xltypeErr)
						{
							continue;
						}
						else if (cell_type == xltypeStr)
						{
							WORD type = get_xlstr_represented_type(cell->val.str, NULL);
							
							if (type == xltypeInt
								|| type == xltypeNum
								|| type == xltypeNil)
							{
								continue;
							}
							else
							{
								xltype = xltypeStr;
								break;
							}
						}
						else
						{
							xltype = xltypeStr;
							break;
						}
					}
                    else if (xltype == xltypeBool)
                    {
                        if (cell_type == xltypeBool
							|| cell_type == xltypeNil 
                            || cell_type == xltypeMissing 
                            || cell_type == xltypeErr)
                        {
                            continue;
                        }
						else if (cell_type == xltypeInt)
                        {
                            int v = cell->val.w;
                            if (v == 0 || v == 1)
							{
								continue;
							}
							else
							{
								xltype = xltypeStr;
								break;
							}
                        }
                        else if (cell_type == xltypeNum)
                        {
                            double v = cell->val.num;
                            if ((v == 0.0) || (v == 1.0))
							{
                                continue;
							}
							else
							{
								xltype = xltypeStr;
								break;
							}
                        }
						else if (cell_type == xltypeStr)
						{
							bool zero_or_one;
							WORD type = get_xlstr_represented_type(cell->val.str, &zero_or_one);
							if (type == xltypeBool || type == xltypeNil || zero_or_one)
							{
								continue;
							}
							else
							{
								xltype = xltypeStr;
								break;
							}
						}
						else
						{
							xltype = xltypeStr;
							break;
						}
                    }
                    else if (cell_type == xltypeNil
                             || cell_type == xltypeMissing
                             || cell_type == xltypeErr)
                    {
                        continue;
                    }
                    else
                    {
						xltype = xltypeStr;
						break;
                    }
                }
            }
        }

        /* Map the inferred Excel type to a DuckDB type. */
        duckdb_type type;
    
        switch (xltype)
        {
            case xltypeInt:
                type = DUCKDB_TYPE_INTEGER;
                break;

            case xltypeNum:
                type = DUCKDB_TYPE_DOUBLE;
                break;

            case xltypeBool:
                type = DUCKDB_TYPE_BOOLEAN;
                break;

            case xltypeStr:
            default:
                type = DUCKDB_TYPE_VARCHAR;
                break;
        }

		duckdb_logical_type lt = DUCKDB_CREATE_LOGICAL_TYPE(type);
		if (!lt)
		{
			SET_BIND_ERROR(errmsg, err_buf_size, ERR_MSG_XLRANGE_INTERNAL);
			goto fail;
		}

		types[i] = type;
        logical_types[i] = lt;
		
		continue;
	
	fail:
		
		return 0;
	}
	
	return 1;
}

static void xlrange_bind(duckdb_bind_info info)
{
    xlrange_bind_data_t	*bind_data = NULL;
    duckdb_logical_type *logical_types = NULL;
    duckdb_type 		*types = NULL;
    char                **colnames = NULL;
    int32_t             range_idx = 0;
    size_t              nsample = 0;
    size_t              ndatarows = 0;
    size_t              nrows = 0;
    size_t              ncols = 0;
	bool				all_varchar;
	bool				has_header;
	bool				is_strict;
	bool				ignore_errors;

    char errmsg[ERR_MSG_MAX_LEN];
    errmsg[0] = '\0';

    xlrange_context_t *ctx = DUCKDB_BIND_GET_EXTRA_INFO(info);

    if (!ctx)
    {
        SET_BIND_ERROR(errmsg, sizeof(errmsg), ERR_MSG_XLRANGE_INTERNAL);
        goto fail;
    }

	if (parse_params(
		info,
		ctx,
		&range_idx,
		&nsample,
		&all_varchar,
		&has_header,
		&is_strict,
		&ignore_errors,
		errmsg,
		sizeof(errmsg)
	) == 0)
	{
		goto fail;
	}

    LPXLOPER12 range = &ctx->ranges[range_idx - 1];
    if (LPXLOPER12_TYPE(range) != xltypeMulti)
    {
        SET_BIND_ERROR(errmsg, sizeof(errmsg), ERR_MSG_XLRANGE_INTERNAL);
        goto fail;
    }

    LPXLOPER12 p = range->val.array.lparray;
    ncols = (size_t)range->val.array.columns;
    nrows = (size_t)range->val.array.rows;
    if (!p || ncols == 0 || nrows == 0)
    {
        SET_BIND_ERROR(errmsg, sizeof(errmsg), ERR_MSG_XLRANGE_INTERNAL);
        goto fail;
    }

    ndatarows = has_header
        ? ((nrows > 0) ? nrows - 1 : 0)
        : nrows;

    if (nsample == 0 || nsample > ndatarows)
        nsample = ndatarows;

    types = malloc(ncols*sizeof(*types));
	/* Zero-initialize entries for partial-failure cleanup. */
	colnames = calloc(ncols, sizeof(*colnames));
    logical_types = calloc(ncols, sizeof(*logical_types));
    if (!types || !logical_types || !colnames)
    {
        SET_BIND_ERROR(errmsg, sizeof(errmsg), ERR_MSG_XLRANGE_INTERNAL);
        goto fail;
    }

	if (get_column_names(
		p,
		colnames,
		ncols,
		has_header,
		is_strict,
		errmsg,
		sizeof(errmsg)
	) == 0)
	{
		goto fail;
	}

	if (infer_types(
		p,
		ncols,
		nsample,
		ndatarows,
		all_varchar,
		has_header,
		types,
		logical_types,
		errmsg,
		sizeof(errmsg)
	) == 0)
	{
		goto fail;
	}

    bind_data = malloc(sizeof(*bind_data));
    if (!bind_data)
    {
        SET_BIND_ERROR(errmsg, sizeof(errmsg), ERR_MSG_XLRANGE_INTERNAL);
        goto fail;
    }

    bind_data->has_header = has_header;
    bind_data->lparray = has_header ? (p + ncols) : p;
    bind_data->ncols = ncols;
    bind_data->nrows = ndatarows;
    bind_data->types = types;
	/* Ownership transfer to bind_data */
    types = NULL;
    bind_data->colnames = colnames;
	/* Ownership transfer to bind_data */
    colnames = NULL;
	bind_data->ignore_errors = ignore_errors;

	for (size_t i = 0; i < ncols; i++)
		/* logical_types members are copied. */
		DUCKDB_BIND_ADD_RESULT_COLUMN(info, bind_data->colnames[i], logical_types[i]);

    DUCKDB_BIND_SET_BIND_DATA(info, bind_data, free_bind_data);
	/* Ownership transfer to table function */
    bind_data = NULL;

    DUCKDB_BIND_SET_CARDINALITY(info, (idx_t)ndatarows, true);

    goto cleanup;

fail:

    DUCKDB_BIND_SET_ERROR(
        info,
        errmsg[0] ? errmsg : ERR_MSG_XLRANGE_INTERNAL
    );

cleanup:

    free_bind_data(bind_data);
	free(types);

	if (colnames)
	{
		for (size_t i = 0; i < ncols; i++)
			free(colnames[i]);

		free(colnames);
	}

	if (logical_types)
	{
		for (size_t i = 0; i < ncols; i++)
			DUCKDB_DESTROY_LOGICAL_TYPE(&logical_types[i]);

		free(logical_types);
	}
    
}

static void free_scan_state(void *p)
{
    if (!p)
        return;

    xlrange_scan_state_t *state = p;

    free(state->types);

    if (state->colnames)
    {
        for (size_t i = 0; i < state->ncols; i++)
            free(state->colnames[i]);

        free(state->colnames);
    }

    free(state);
}

#define SET_INIT_ERROR(BUF, LEN, MSG) \
    format_error_message( \
        BUF, \
        LEN, \
        "initializing xlrange", \
        NULL, \
        -1, \
        -1, \
        MSG, \
        false \
    )

static void xlrange_init(duckdb_init_info info)
{
    xlrange_scan_state_t *state = NULL;
    duckdb_type          *types = NULL;
    char                 **colnames = NULL;
    size_t               ncols = 0;

    char errmsg[ERR_MSG_MAX_LEN];
    errmsg[0] = '\0';

    xlrange_bind_data_t *bind_data = DUCKDB_INIT_GET_BIND_DATA(info);
    if (!bind_data)
    {
        SET_INIT_ERROR(errmsg, sizeof(errmsg), ERR_MSG_XLRANGE_INTERNAL);
        goto fail;
    }

    bool has_header = bind_data->has_header;
    ncols = bind_data->ncols;

    state = calloc(1, sizeof(*state));
    types = malloc(ncols * sizeof(*types));
    colnames = calloc(ncols, sizeof(*colnames));

    if (!state || !types || !colnames)
    {
        SET_INIT_ERROR(errmsg, sizeof(errmsg), ERR_MSG_XLRANGE_INTERNAL);
        goto fail;
    }

    memcpy(types, bind_data->types, ncols*sizeof(*types));

    /* Copy column names into scan-owned storage. */
    for (size_t i = 0; i < ncols; i++)
    {
		char *copy = _strdup(bind_data->colnames[i]);
		if (!copy)
		{		
			SET_INIT_ERROR(errmsg, sizeof(errmsg), ERR_MSG_XLRANGE_INTERNAL);
			goto fail;
		}

        colnames[i] = copy;
    }

    state->has_header = has_header;
    state->lparray = bind_data->lparray;
    state->ncols = ncols;
    state->nrows = bind_data->nrows;
    state->next_row = 0;
    state->types = types;
    types = NULL;
    state->colnames = colnames;
    colnames = NULL;
    state->vec_size = DUCKDB_VECTOR_SIZE();
	state->ignore_errors = bind_data->ignore_errors;

    DUCKDB_INIT_SET_INIT_DATA(info, state, free_scan_state);
    state = NULL;

	goto cleanup;

fail:

    DUCKDB_INIT_SET_ERROR(
        info,
        errmsg[0] ? errmsg : ERR_MSG_XLRANGE_INTERNAL
    );

cleanup:

    if (colnames)
    {
        for (size_t i = 0; i < ncols; i++)
            free(colnames[i]);

        free(colnames);
    }

    free(types);
    free_scan_state(state);
}

/*
 * Convert Excel cells to inferred DuckDB types.
 * Returns 1 for a value, 0 for NULL, and -1 for incompatibility.
 */
static inline int cell_to_integer(
    LPXLOPER12 	cell,
	int32_t	 	*out
)
{
	int res = -1;
	
    switch (LPXLOPER12_TYPE(cell))
	{
        case xltypeInt:
			*out = (int32_t)cell->val.w;
			res = 1;
			break;

        case xltypeNum:
            if (is_whole_number(cell->val.num))
			{
                *out = (int32_t)cell->val.num;
				res = 1;
				break;
            }
			
			break;
			
        case xltypeBool:
			break;

		case xltypeStr:
		{
			wchar_t *src = cell->val.str;
			if (!src)
			{
				res = 0;
				break;
			}
			
			char *dest = NULL;
			if (xlstr_to_utf8(&dest, src, NULL) == 0 || !dest)
				break;
			
			size_t n;
			char *trimmed = trim_whitespace(dest, &n);
			trimmed[n] = '\0';

			switch (n)
			{
				case 0:
					free(dest);
					res = 0;
					goto done;

				case 3:
					if (strncasecmp(trimmed, "n/a", n) == 0)
					{
						free(dest);
						res = 0;
						goto done;
					}

					break;

				case 4:
					if (strncasecmp(trimmed, "null", n) == 0)
					{
						free(dest);
						res = 0;
						goto done;
					}

					break;

				default:
					break;
			}

			char *endptr = NULL;
			errno = 0;
			double d = strtod(trimmed, &endptr);
			if (errno != ERANGE 
				&& endptr != trimmed
				&& *endptr == '\0'
				&& isfinite(d)
				&& is_whole_number(d))
			{
				free(dest);
				*out = (int32_t)d;
				res = 1;
				break;
			}

			free(dest);
			break;
		}

		case xltypeNil:
		case xltypeMissing:
		case xltypeErr:
			res = 0;
			break;

        default:
			break;
    }

done:

	return res;
}

static inline int cell_to_double(
    LPXLOPER12 	cell,
	double	 	*out
)
{
	int res = -1;

    switch (LPXLOPER12_TYPE(cell))
	{
        case xltypeNum:
            *out = cell->val.num;
            res = 1;
			break;

        case xltypeInt:
            *out = (double)cell->val.w;
            res = 1;
			break;

        case xltypeBool:
			break;

		case xltypeStr:
		{
			wchar_t *src = cell->val.str;
			if (!src)
			{
				res = 0;
				break;
			}
			
			char *dest = NULL;		
			if (xlstr_to_utf8(&dest, src, NULL) == 0 || !dest)
				break;

			size_t n;
			char *trimmed = trim_whitespace(dest, &n);
			trimmed[n] = '\0';

			switch (n)
			{
				case 0:
					free(dest);
					res = 0;
					goto done;

				case 3:
					if (strncasecmp(trimmed, "n/a", n) == 0)
					{
						free(dest);
						res = 0;
						goto done;
					}

					break;

				case 4:
					if (strncasecmp(trimmed, "null", n) == 0)
					{
						free(dest);
						res = 0;
						goto done;
					}

					break;

				default:
					break;
			}

			char *endptr = NULL;
			errno = 0;
			double num = strtod(trimmed, &endptr);
			if (errno != ERANGE 
				&& endptr != trimmed
				&& *endptr == '\0'
				&& isfinite(num))
			{
				free(dest);

				res = 1;
				*out = num;
				break;
			}

			free(dest);
			break;
		}

		case xltypeNil:
		case xltypeMissing:
		case xltypeErr:
            res = 0;
			break;

        default:
            res = -1;
			break;
    }

done:

	return res;
}

static inline int cell_to_bool
(
    LPXLOPER12 	cell,
	bool	 	*out
)
{
	int res = -1;
	
    switch (LPXLOPER12_TYPE(cell))
	{
        case xltypeBool:
            *out = cell->val.xbool;
            res = 1;
			break;

        case xltypeInt:
		{
			int val = cell->val.w;
			if (val == 1)
			{
				*out = true;
				res = 1;
				break;
			}
			else if (val == 0)
			{
				*out = false;
				res = 1;
				break;
			}

			break;
		}

        case xltypeNum:
		{
			double val = cell->val.num;
			if (val == 1.0)
			{
				*out = true;
				res = 1;
				break;
			}
			else if (val == 0.0)
			{
				*out = false;
				res = 1;
				break;
			}

			break;
		}

		case xltypeStr:
		{
			wchar_t *src = cell->val.str;
			char *dest = NULL;
				
			if (!src)
			{
				res = 0;
				break;
			}
	
			if (xlstr_to_utf8(&dest, src, NULL) == 0 || !dest)
				break;

			size_t n;
			char *trimmed = trim_whitespace(dest, &n);

			char *endptr = NULL;
			errno = 0;
			double d = strtod(trimmed, &endptr);
			if (errno != ERANGE 
				&& endptr != trimmed
				&& endptr == trimmed + n
				&& isfinite(d))
			{
				if (d == 0.0)
				{
					*out = false;
					res = 1;
					break;
				}
				else if (d == 1.0)
				{
					*out = true;
					res = 1;
					break;
				}
			}

			switch (n)
			{
				case 0:
					res = 0;
					break;

				case 1:
					switch (trimmed[0])
					{
						case 'Y':
						case 'y':
						case 'T':
						case 't':
							*out = true;
							res = 1;
							break;

						case 'N':
						case 'n':
						case 'F':
						case 'f':
							*out = false;
							res = 1;
							break;

						
						default:
							break;
					}
					
					break;

				case 2:
					if (strncasecmp(trimmed, "no", n) == 0)
					{
						*out = false;
						res = 1;
						break;
					}
					
					break;


				case 3:
					if (strncasecmp(trimmed, "yes", n) == 0)
					{
						*out = true;
						res = 1;
						break;
					}
					else if (strncasecmp(trimmed, "n/a", n) == 0)
					{
						res = 0;
						break;
					}

					res = -1;
					break;

				case 4:
					if (strncasecmp(trimmed, "true", n) == 0)
					{
						*out = true;
						res = 1;
						break;
					}
					else if (strncasecmp(trimmed, "null", n) == 0)
					{
						res = 0;
						break;
					}
					
					break;

				case 5:
					if (strncasecmp(trimmed, "false", n) == 0)
					{
						*out = false;
						res = 1;
						break;
					}
					
					break;

				default:
					break;
			}
			
			free(dest);
			
			break;
		}

		case xltypeNil:
		case xltypeMissing:
		case xltypeErr:
			res = 0;
			break;

        default:
			break;
    }
	
	return res;
}

static inline int cell_to_varchar(
    LPXLOPER12 		cell,
    duckdb_vector 	vec,
	idx_t 			out_rows
)
{
	int res = -1;
	
	switch (LPXLOPER12_TYPE(cell))
	{
		case xltypeStr:
		{
			wchar_t *src = cell->val.str;
			char *dest = NULL;
			
			if (!src)
			{
				res = 0;
				break;
			}

			if (xlstr_to_utf8(&dest, src, NULL) == 0 || !dest)
			{
				res = -1;
				break;
			}

			size_t n;
			char *trimmed = trim_whitespace(dest, &n);

			switch (n)
			{
				case 0:
					free(dest);
					res = 0;
					goto done;

				case 3:
					if (strncasecmp(trimmed, "n/a", n) == 0)
					{
						free(dest);
						res = 0;
						goto done;
					}

					break;

				case 4:
					if (strncasecmp(trimmed, "null", n) == 0)
					{
						free(dest);
						res = 0;
						goto done;
					}

					break;

				default:
					break;
			}

			DUCKDB_VECTOR_ASSIGN_STRING_ELEMENT(vec, out_rows, dest);
			free(dest);

			res = 1;
			break;
		}

		case xltypeInt:
		{
			char buf[32];
			snprintf(buf, sizeof(buf), "%d", cell->val.w);
			DUCKDB_VECTOR_ASSIGN_STRING_ELEMENT(vec, out_rows, buf);

			res = 1;
			break;
		}

		case xltypeNum:
		{
			char buf[64];
			snprintf(buf, sizeof(buf), "%.17g", cell->val.num);
			DUCKDB_VECTOR_ASSIGN_STRING_ELEMENT(vec, out_rows, buf);

			res = 1;
			break;
		}

		case xltypeBool:
			DUCKDB_VECTOR_ASSIGN_STRING_ELEMENT(
				vec,
				out_rows,
				cell->val.xbool ? "TRUE" : "FALSE"
			);

			res = 1;
			break;

		case xltypeNil:
		case xltypeMissing:
		case xltypeErr:
			res = 0;
			break;

		default:
			res = -1;
			break;
	}

done:

	return res;
}

#define SET_SCANNING_ERROR(BUF, LEN, MSG) \
    format_error_message( \
        BUF, \
        LEN, \
        "scanning xlrange", \
        NULL, \
        -1, \
        -1, \
        MSG, \
        false \
    )

static void xlrange_scan
(
	duckdb_function_info 	info,
	duckdb_data_chunk 		output
)
{
    char errmsg[ERR_MSG_MAX_LEN];
    errmsg[0] = '\0';

    xlrange_scan_state_t *state = DUCKDB_FUNCTION_GET_INIT_DATA(info);
    if (!state)
    {
        SET_SCANNING_ERROR(errmsg, sizeof(errmsg), ERR_MSG_XLRANGE_INTERNAL);
        goto fail;
    }

    bool has_header = state->has_header;
	bool ignore_errors = state->ignore_errors;
    idx_t out_rows = 0;
	
	char *msg = NULL;

    while (state->next_row < state->nrows && out_rows < state->vec_size)
    {
        for (size_t c = 0; c < state->ncols; c++)
        {
            duckdb_vector vec = DUCKDB_DATA_CHUNK_GET_VECTOR(output, c);
            if (!vec)
            {
                SET_SCANNING_ERROR(errmsg, sizeof(errmsg), ERR_MSG_XLRANGE_INTERNAL);
                goto fail;
            }

            size_t idx = state->next_row * state->ncols + c;

            LPXLOPER12 cell = &state->lparray[idx];

            switch (state->types[c])
            {
                case DUCKDB_TYPE_INTEGER:
                {
                    int32_t *data = DUCKDB_VECTOR_GET_DATA(vec);
                    if (!data)
                    {
                        SET_SCANNING_ERROR(errmsg, sizeof(errmsg), ERR_MSG_XLRANGE_INTERNAL);
                        goto fail;
                    }

					int32_t val;
                    switch (cell_to_integer(cell, &val))
					{
						case 1:
							data[out_rows] = val;
							break;
						
						case 0:
							goto sqlnull;
						
						case -1:
						default:
							msg = ERR_MSG_XLRANGE_INT;
							goto incompatible;
					}
					
                    break;
                }

                case DUCKDB_TYPE_DOUBLE:
                {
                    double *data = DUCKDB_VECTOR_GET_DATA(vec);
                    if (!data)
                    {
                        SET_SCANNING_ERROR(errmsg, sizeof(errmsg), ERR_MSG_XLRANGE_INTERNAL);
                        goto fail;
                    }

					double val;
                    switch (cell_to_double(cell, &val))
					{
						case 1:
							data[out_rows] = val;
							break;
						
						case 0:
							goto sqlnull;
						
						case -1:
						default:
							msg = ERR_MSG_XLRANGE_DOUBLE;
							goto incompatible;
					}
					
                    break;
                }

                case DUCKDB_TYPE_BOOLEAN:
                {
                    bool *data = DUCKDB_VECTOR_GET_DATA(vec);
                    if (!data)
                    {
                        SET_SCANNING_ERROR(errmsg, sizeof(errmsg), ERR_MSG_XLRANGE_INTERNAL);
                        goto fail;
                    }
                    
					bool val;
                    switch (cell_to_bool(cell, &val))
					{
						case 1:
							data[out_rows] = val;
							break;
						
						case 0:
							goto sqlnull;
						
						case -1:
						default:
							msg = ERR_MSG_XLRANGE_BOOL;
							goto incompatible;
					}
					
                    break;
                }

                case DUCKDB_TYPE_VARCHAR:
                {
                    switch (cell_to_varchar(cell, vec, out_rows))
					{
						case 1:
							break;
						
						case 0:
							goto sqlnull;
						
						case -1:
							msg = ERR_MSG_XLRANGE_VARCHAR;
							goto incompatible;
					}
					
                    break;
                }

                default:
                    goto sqlnull;
            }

            continue;

		incompatible:
		
			if (!ignore_errors)
			{
				format_error_message(
					errmsg,
					sizeof(errmsg),
					"scanning xlrange",
					state->colnames[c],
					(long long)c,
					(long long)state->next_row,
					msg,
					has_header
				);
				
				goto fail;
			}
			
        sqlnull:
			/* Write empty or ignored incompatible values as SQL NULL. */
            DUCKDB_VECTOR_ENSURE_VALIDITY_WRITABLE(vec);

            DUCKDB_VALIDITY_SET_ROW_INVALID(
                DUCKDB_VECTOR_GET_VALIDITY(vec),
                out_rows
            );
        }

        state->next_row++;

        out_rows++;
    }

    DUCKDB_DATA_CHUNK_SET_SIZE(output, out_rows);

    return;

fail:

    DUCKDB_FUNCTION_SET_ERROR(
        info,
        errmsg[0] ? errmsg : ERR_MSG_XLRANGE_INTERNAL
    );

    DUCKDB_DATA_CHUNK_SET_SIZE(output, 0);
}

int register_xlrange_func
(
    duckdb_connection con,
    LPXLOPER12 ranges,
    size_t nrange,
    duckdb_table_function *function
) 
{
    int res = 0;

    duckdb_table_function table_func = NULL;
    duckdb_logical_type int_type = NULL;
    duckdb_logical_type bool_type = NULL;

    if (!function || (!ranges && nrange > 0))
		return 0;

    *function = NULL;

    table_func = DUCKDB_CREATE_TABLE_FUNCTION();
    if (!table_func)
        goto fail;

    /* Shared borrowed context for xlrange() calls. */
    xlrange_context_t *ctx = malloc(sizeof(*ctx));
    if (!ctx)
        goto fail;

    ctx->ranges = ranges; // borrows
    ctx->nrange = nrange;

    DUCKDB_TABLE_FUNCTION_SET_EXTRA_INFO(table_func, ctx, free);
	ctx = NULL;

    int_type = DUCKDB_CREATE_LOGICAL_TYPE(DUCKDB_TYPE_INTEGER);
    bool_type = DUCKDB_CREATE_LOGICAL_TYPE(DUCKDB_TYPE_BOOLEAN);
    if (!int_type || !bool_type)
        goto fail;

    DUCKDB_TABLE_FUNCTION_SET_NAME(table_func, "xlrange");
    DUCKDB_TABLE_FUNCTION_ADD_PARAMETER(table_func, int_type);
    DUCKDB_TABLE_FUNCTION_ADD_NAMED_PARAMETER(table_func, "sample", int_type);
    DUCKDB_TABLE_FUNCTION_ADD_NAMED_PARAMETER(table_func, "all_varchar", bool_type);
	DUCKDB_TABLE_FUNCTION_ADD_NAMED_PARAMETER(table_func, "header", bool_type);
	DUCKDB_TABLE_FUNCTION_ADD_NAMED_PARAMETER(table_func, "strict", bool_type);
	DUCKDB_TABLE_FUNCTION_ADD_NAMED_PARAMETER(table_func, "ignore_errors", bool_type);
    DUCKDB_TABLE_FUNCTION_SET_BIND(table_func, xlrange_bind);
    DUCKDB_TABLE_FUNCTION_SET_INIT(table_func, xlrange_init);
    DUCKDB_TABLE_FUNCTION_SET_FUNCTION(table_func, xlrange_scan);

    if (DUCKDB_REGISTER_TABLE_FUNCTION(con, table_func) != DuckDBSuccess)
        goto fail;

    *function = table_func;
    res = 1;

    goto cleanup;

fail:

    if (table_func)
        DUCKDB_DESTROY_TABLE_FUNCTION(&table_func);

    res = 0;

cleanup:
    if (int_type)
        DUCKDB_DESTROY_LOGICAL_TYPE(&int_type);

    if (bool_type)
        DUCKDB_DESTROY_LOGICAL_TYPE(&bool_type);

    return res;
}
