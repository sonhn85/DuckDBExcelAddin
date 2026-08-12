#ifndef HELPER_H
#define HELPER_H

#include <windows.h>
#include <stddef.h>
#include "XLCALL.H"
#include "config.h"

// Preprocessor stringification and wide-string helpers
#define TO_STR(x)               #x
#define TO_STR_DELAY(x)         TO_STR(x)
#define WIDEN(x)                L##x
#define WIDEN_DELAY(x)          WIDEN(x)
#define TO_WSTR(x)              WIDEN_DELAY(TO_STR_DELAY(x))

// XLOPER12 type helpers
#define LPXLOPER12_TYPE(P)      ((P)->xltype & XLTYPEMASK)
#define XLOPER12_TYPE(V)        ((V).xltype & XLTYPEMASK)
#define LPXLOPER12_DLL_FREE(P)  (((P)->xltype & xlbitDLLFree) != 0)
#define XLOPER12_DLL_FREE(V)    (((V).xltype & xlbitDLLFree) != 0)

// String conversion
int xlstr_to_utf8(char **dest, const wchar_t *src, size_t *n);
int utf8_to_xlstr(wchar_t **dest, const char *src, int n);

// UI
void show_error(HWND hwnd, const wchar_t *msg);

// XLOPER12 creation and validation
LPXLOPER12 make_string_cell(const char *utf8str);
int is_null_or_whitespace_xlstr(const wchar_t *xlstr);

// XLOPER12 deep-copy and cleanup
int xloper12_deep_copy(XLOPER12 *dst, const XLOPER12 *src);
void xloper12_free_array(LPXLOPER12 lparray, size_t n);
void xloper12_free(LPXLOPER12 pxFree);
void xloper12_free_members(LPXLOPER12 pxFree);

#endif // HELPER_H
