#ifndef HELPER_H
#define HELPER_H

#include <windows.h>
#include <stddef.h>
#include "XLCALL.H"
#include "config.h"

/* Preprocessor stringification and wide-string helpers */
#define TO_STR(x)               #x
#define TO_STR_DELAY(x)         TO_STR(x)
#define WIDEN(x)                L##x
#define WIDEN_DELAY(x)          WIDEN(x)
#define TO_WSTR(x)              WIDEN_DELAY(TO_STR_DELAY(x))

/* XLOPER12 type and ownership helpers. */
#define LPXLOPER12_TYPE(P)              ((P)->xltype & XLTYPEMASK)
#define XLOPER12_TYPE(V)                ((V).xltype & XLTYPEMASK)
#define LPXLOPER12_DLL_FREE(P)          (((P)->xltype & xlbitDLLFree) != 0)
#define XLOPER12_DLL_FREE(V)            (((V).xltype & xlbitDLLFree) != 0)
#define XLOPER12_CLEAR_OWNER_FLAGS(V)   ((V).xltype &= ~(xlbitDLLFree | xlbitXLFree))
#define XLOPER12_SET_DLL_FREE(V)        ((V).xltype |= xlbitDLLFree)

/* Excel and UTF-8 string conversion. */

/* Convert a length-prefixed Excel string to allocated UTF-8. */
int xlstr_to_utf8(char **dest, const wchar_t *src, size_t *n);
/* Convert UTF-8 to an allocated length-prefixed Excel string. */
int utf8_to_xlstr(wchar_t **dest, const char *src, int n);

/* UI */
void show_error(HWND hwnd, const wchar_t *msg);

/* XLOPER12 creation and validation. */

/* Create an add-in-owned Excel string result. */
LPXLOPER12 make_string_cell(const char *utf8str);
/* Return true for a NULL, empty, or whitespace-only Excel string. */
int is_null_or_whitespace_xlstr(const wchar_t *xlstr);

/* XLOPER12 copying and cleanup. */

/*
 * Deep-copy src into an empty destination.
 * The copy is owned by the add-in and released through xlAutoFree12().
 */
int xloper12_deep_copy(XLOPER12 *dst, const XLOPER12 *src);
/* Release an XLOPER12 array and all add-in-owned members. */
void xloper12_free_array(LPXLOPER12 lparray, size_t n);
/* Release an add-in-owned XLOPER12 and its members. */
void xloper12_free(LPXLOPER12 pxFree);
/* Release add-in-owned members without freeing the outer XLOPER12. */
void xloper12_free_members(LPXLOPER12 pxFree);

#endif /* HELPER_H */
