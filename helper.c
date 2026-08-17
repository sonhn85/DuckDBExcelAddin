#include "helper.h"

#include <stdlib.h>
#include <wctype.h>
#include <wchar.h>

void show_error(HWND hwnd, const wchar_t *msg)
{
    MessageBoxW(hwnd, msg, L"Error", MB_OK | MB_ICONERROR);
}

int xlstr_to_utf8(char **dest, const wchar_t *src, size_t *n)
{
    if (!dest) return 0;

    *dest = NULL;

    if (n)
        *n = 0;

    /* NULL Excel string */
    if (!src) return 1;

    char *utf8;

    /* Excel strings are length-prefixed:
     * src[0] contains the character count (max 32767). */
    int wchar_count = (unsigned short)src[0];

    /* Empty string */
    if (wchar_count == 0)
    {
        utf8 = malloc(1);

        if (!utf8) return 0;

        utf8[0] = '\0';

        *dest = utf8;

        return 1;
    }

    const wchar_t *start = src + 1;

    int utf8_size = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        start,
        wchar_count,
        NULL,
        0,
        NULL,
        NULL
    );

    if (utf8_size == 0)
        return 0;

    utf8 = malloc((size_t)utf8_size + 1);

    if (!utf8)
        return 0;

    int chars_written = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        start,
        wchar_count,
        utf8,
        utf8_size,
        NULL,
        NULL
    );

    if (chars_written == 0)
    {
        free(utf8);
        return 0;
    }

    /* Return a null-terminated UTF-8 string */
    utf8[chars_written] = '\0';

    if (n) *n = chars_written;

    *dest = utf8;

    return 1;
}

int utf8_to_xlstr(wchar_t **dest, const char *src, int n)
{

    if (!dest) return 0;

    *dest = NULL;

    if (n < -1) return 0;

    if (!src) return 1;

    wchar_t *xlstr;

    /* Empty Excel string */
    if (n == 0)
    {
        /* One wchar for length prefix, one for trailing L'\0' */
        xlstr = malloc(2 * sizeof(*xlstr));

        if (!xlstr)
            return 0;

        /* Excel strings are length-prefixed */
        xlstr[0] = 0;
        xlstr[1] = L'\0';

        *dest = xlstr;

        return 1;
    }

    int wchar_count = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        src,
        n,
        NULL,
        0
    );

    if (wchar_count == 0)
        return 0;

    /* Excel strings are limited to XLSTR_MAX_LEN characters */
    int char_only_len = (n == -1) ? wchar_count - 1 : wchar_count;

    if (char_only_len > XLSTR_MAX_LEN)
        return 0;

    /* Allocate space for UTF-16 characters plus Excel length prefix */
    xlstr = malloc(((size_t)wchar_count + 1) * sizeof(*xlstr));

    if (!xlstr)
        return 0;

    int chars_written = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        src,
        n,
        xlstr+1,
        wchar_count
    );

    if (chars_written == 0)
    {
        free(xlstr);
        return 0;
    }

    /* Exclude terminating L'\0' from Excel string length */
    if (n == -1) --chars_written;

    /* Store Excel length prefix */
    xlstr[0] = (unsigned short)chars_written;

    *dest = xlstr;

    return 1;
}

LPXLOPER12 make_string_cell(const char *utf8str)
{
    if (!utf8str)
        return NULL;

    wchar_t *xlstr = NULL;

    if (utf8_to_xlstr(&xlstr, utf8str, -1) == 0 || !xlstr)
    {
        free(xlstr);
        return NULL;
    }

    LPXLOPER12 result = malloc(sizeof(*result));

    if (!result)
    {
        free(xlstr);
        return NULL;
    }

    /* To be freed with xlAutoFree12 */
    result->xltype = xltypeStr | xlbitDLLFree; 
    result->val.str = xlstr;

    return result;
}

int is_null_or_whitespace_xlstr(const wchar_t *xlstr)
{

    if (!xlstr) return 1;
    
    // Excel strings are length-prefixed
    size_t n = (unsigned short)xlstr[0];

    for (size_t i = 1; i <= n; i++)
    {
        if (!iswspace((wint_t)xlstr[i]))
            return 0;
    }

    return 1;
}

void xloper12_free_members(LPXLOPER12 pxFree)
{
    if (!pxFree) return;

    switch (LPXLOPER12_TYPE(pxFree))
    {
        case xltypeMulti:
            /* Only free memory allocated by this add-in */
            if (LPXLOPER12_DLL_FREE(pxFree))
            {
                LPXLOPER12 cells = pxFree->val.array.lparray;

                if (cells)
                {
                    /* Total element count. Excel limits prevent overflow here */
                    size_t n = (size_t)pxFree->val.array.rows *(size_t)pxFree->val.array.columns;
                    for (size_t i=0; i < n; i++)
                    {
                        /* Recursively free nested strings and arrays
                         * Primitive values do not own heap memory */
                        switch (XLOPER12_TYPE(cells[i]))
                        {
                            case xltypeStr:
                            case xltypeMulti:
                                xloper12_free_members(&cells[i]);
                                break;

                            default:
                                break;
                        }
                    }
                }
                /* Free array storage after all elements are released */
                free(pxFree->val.array.lparray);
            }
            break;

        case xltypeStr:
            /* Only free memory allocated by this add-in */
            if (LPXLOPER12_DLL_FREE(pxFree))
                free(pxFree->val.str);

            break;

        default:
            break;
    }
}

void xloper12_free(LPXLOPER12 pxFree)
{
    if (!pxFree)
        return;

    xloper12_free_members(pxFree);

    free(pxFree);
}

void xloper12_free_array(LPXLOPER12 lparray, size_t n)
{
    if (!lparray)
        return;

    for (size_t i = 0; i < n; ++i)
    {
        xloper12_free_members(&lparray[i]);
    }

    free(lparray);
}

int xloper12_deep_copy(XLOPER12 *dst, const XLOPER12 *src)
{
    if (dst == src) return 1;

    if (!dst || !src) return 0;

    XLOPER12 dst_tmp = {0};    /* Safe default value for rollback */

    /* Start with a shallow copy of the source value */
    dst_tmp = *src;

    /* Clear ownership flag and restore it only after deep-copy succeeds */
    dst_tmp.xltype &= ~xlbitDLLFree;

    switch (LPXLOPER12_TYPE(src)) 
    {
        case xltypeStr:
        {
            wchar_t *ws_src = src->val.str;

            /* NULL string is allowed */
            if (!ws_src)
                break;

            /* Excel strings are length-prefixed */
            size_t wchar_count = (unsigned short)ws_src[0]; 

            wchar_t *ws_cpy = malloc((wchar_count + 1) * sizeof(*ws_cpy));

            if (!ws_cpy)
                return 0;

            wmemcpy(ws_cpy, ws_src, (wchar_count + 1));

            /* To be freed with xlAutoFree12 */
            dst_tmp.xltype |= xlbitDLLFree;
            dst_tmp.val.str = ws_cpy;

            break;
        }

        case xltypeMulti:
        {
            /* Total element count. Excel limits prevent overflow here */
            size_t n = (size_t)src->val.array.rows * (size_t)src->val.array.columns;

            if (n == 0)
            {
                dst_tmp.val.array.lparray = NULL;
                break;
            }

            LPXLOPER12 src_cells = src->val.array.lparray;

            /* Non-empty array requires a valid backing store */
            if (!src_cells)
                return 0;

            /* Initialize elements to safe defaults for rollback */
            LPXLOPER12 dst_cells = calloc(n, sizeof(*dst_cells));

            if (!dst_cells)
                return 0;

            for (size_t i=0; i < n; i++)
            {
                /* Deep-copy nested strings and arrays.
                 * Primitive types do not own heap memory */
                switch (XLOPER12_TYPE(src_cells[i]))
                {
                    case xltypeStr:
                    case xltypeMulti:
                        if (xloper12_deep_copy(&dst_cells[i], &src_cells[i]) == 0)
                        {
                            /* Release already-copied elements before returning failure */
                            xloper12_free_array(dst_cells, i);
                            return 0;
                        }
                        break;

                    case xltypeBool:
                    case xltypeInt:
                    case xltypeNum:
                    case xltypeMissing:
                    case xltypeNil:
                    case xltypeErr:
                        /* Shallow copy is sufficient */
                        dst_cells[i] = src_cells[i];
                        dst_cells[i].xltype &= ~xlbitDLLFree;
                        break;

                    default:
                        /* Unsupported type. Roll back and fail */
                        xloper12_free_array(dst_cells, i);
                        return 0;
                }
            }

            /* To be freed with xlAutoFree12 */
            dst_tmp.xltype |= xlbitDLLFree;
            dst_tmp.val.array.lparray = dst_cells;

            break;
        }

        case xltypeBool:
        case xltypeInt:
        case xltypeNum:
        case xltypeMissing:
        case xltypeNil:
        case xltypeErr:
            /* Primitive types are fully copied by the initial shallow copy */
            break;

        default:
            /* Unsupported XLOPER12 type */
            return 0; 
    }

    *dst = dst_tmp;

    return 1;
}
