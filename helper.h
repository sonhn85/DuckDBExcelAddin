#ifndef HELPER_H
#define HELPER_H

#include <windows.h>
#include <stddef.h>     // size_t
#include "XLCALL.H"     // LPXLOPER12

// useful macro
#define TO_STR(x)               #x
#define TO_STR_DELAY(x)         TO_STR(x)
#define WIDEN(x)                L##x
#define WIDEN_DELAY(x)          WIDEN(x)
#define TO_WSTR(x)              WIDEN_DELAY(TO_STR_DELAY(x))

// function prototypes

void show_error(HWND hwnd, const wchar_t *msg);
int xlstr_2_utf8(char **dest, const wchar_t *src, size_t *n);
int utf8_2_xlstr(wchar_t **dest, const char *src, int n);
LPXLOPER12 make_string_cell(const char *utf8str);
int is_null_or_whitespace_xlstr(const wchar_t *xlstr);
int xloper12_deep_copy(XLOPER12 *dst, const XLOPER12 *src);
void free_xloper12_array(LPXLOPER12 lparray, size_t n);
void free_xloper12(LPXLOPER12 pxFree);
void xloper12_member_deep_free(LPXLOPER12 pxFree);

#endif // HELPER_H