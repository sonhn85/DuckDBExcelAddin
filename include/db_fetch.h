#ifndef DB_FETCH_H
#define DB_FETCH_H

#include <windows.h>
#include <stdint.h>
#include "XLCALL.H"
#include "duckdb.h"

/* Linked-list node containing a DuckDB result chunk. */
typedef struct chunk_node 
{
    duckdb_data_chunk chunk;    /* Owned by this node. */
    void **vectors;             /* Owned array of chunk-borrowed pointers. */
    uint64_t **valid_masks;     /* Owned array of chunk-borrowed masks. */
    struct chunk_node *next;
    idx_t nrows;                /* Number of rows in this chunk */
} chunk_node;

/* Materialized DuckDB result stored as a linked list of chunks. */
typedef struct chunk_list 
{
    chunk_node *head;
    chunk_node *tail;
    idx_t nrows;                /* Total row count */
    idx_t ncols;                /* Column count */
    idx_t nchunks;              /* Number of chunks */
    const char **col_names;     /* Owned array of result-borrowed names. */
    duckdb_type *col_types;     /* Owned */
    duckdb_type *base_types;    /* Owned (DECIMAL base types) */
    uint8_t *dec_scales;        /* Owned (DECIMAL scales) */
} chunk_list;

/*
 * Materialize all chunks from a DuckDB result.
 *
 * chunklist must be zero-initialized before its first use.
 * result must remain valid until chunks_to_range() completes because
 * column names are borrowed from the result.
 *
 * On success, release chunklist with free_and_reset_chunk_list().
 *
 * Returns 1 on success and 0 on failure.
 */
int fetch_chunks(duckdb_result *pqresult, chunk_list *chunklist, char *errmsg, size_t buf_size);

/* Release all owned resources and reset the chunk list. */
void free_and_reset_chunk_list(chunk_list *chunklist);

/*
 * Convert a materialized result to an add-in-owned Excel range.
 * The returned value is released through xlAutoFree12().
 */
LPXLOPER12 chunks_to_range(chunk_list *chunklist);

#endif /* DB_FETCH_H */
