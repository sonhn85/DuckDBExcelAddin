#ifndef DB_FETCH_H
#define DB_FETCH_H

#include <windows.h>
#include <stdint.h>
#include "XLCALL.H"
#include "duckdb.h"

/* Linked-list node containing a DuckDB result chunk */
typedef struct chunk_node 
{
    duckdb_data_chunk chunk;    /* Owned */
    void **vectors;             /* Pointer array owned, vectors borrowed from chunk */
    uint64_t **valid_masks;     /* Pointer array owned, masks borrowed from chunk */
    struct chunk_node *next;
    idx_t nrows;                /* Number of rows in this chunk */
} chunk_node;

/* Materialized DuckDB result represented as a chunk list */
typedef struct chunk_list 
{
    chunk_node *head;
    chunk_node *tail;
    idx_t nrows;                /* Total row count */
    idx_t ncols;                /* Column count */
    idx_t nchunks;              /* Number of chunks */
    const char **col_names;     /* Pointer array owned, strings borrowed from duckdb_result */
    duckdb_type *col_types;     /* Owned */
    duckdb_type *base_types;    /* Owned (DECIMAL base types) */
    uint8_t *dec_scales;        /* Owned (DECIMAL scales) */
} chunk_list;

/*
 * Materialize all chunks from a DuckDB result.
 *
 * On success, chunklist owns all allocated resources and must be
 * released with free_and_reset_chunk_list().
 *
 * Returns:
 *      1 on success.
 *      0 on failure.
 */
int fetch_chunks(duckdb_result *pqresult, chunk_list *chunklist, char *errmsg, size_t buf_size);

/* Release all memory owned by a chunk list and reset its state */
void free_and_reset_chunk_list(chunk_list *chunklist);

/* Convert a materialized result set to an Excel xltypeMulti range. */
LPXLOPER12 chunks_to_range(chunk_list *chunklist);

#endif /* DB_FETCH_H */
