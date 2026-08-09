#ifndef DB_FETCH_H
#define DB_FETCH_H

#include <windows.h>
#include <stdint.h>
#include "XLCALL.H"
#include "duckdb.h"

// link list structure to store duckdb chunks
typedef struct chunk_node {
    duckdb_data_chunk chunk;
    void **vectors;
    uint64_t **valid_masks;
    struct chunk_node *next;
    idx_t nrows;
} chunk_node;
typedef struct chunk_list {
    chunk_node *head;
    chunk_node *tail;
    idx_t nrows;
    idx_t ncols;
    idx_t nchunks;
    const char **col_names;
    duckdb_type *col_types;
    duckdb_type *base_types;
    uint8_t *dec_scales;
} chunk_list;

// function prototypes
int fetch_chunks(duckdb_result *pqresult, chunk_list *chunklist, const char **errmsg);
void free_and_reset_chunk_list(chunk_list *chunklist);
LPXLOPER12 chunks_to_range(chunk_list *chunklist);

#endif // DB_FETCH_H