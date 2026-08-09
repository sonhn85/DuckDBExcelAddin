#include "helper.h"

#include <stdlib.h>     // malloc, free
#include <wctype.h>     // iswspace
#include <wchar.h>

#define XLSTR_MAX_LEN           0x7fff
#define XLTYPEMASK              0x0fff

void show_error(HWND hwnd, const wchar_t *msg)
{
    MessageBoxW(hwnd, msg, L"Error", MB_OK | MB_ICONERROR);
}

int xlstr_2_utf8(char **dest, const wchar_t *src, size_t *n)
{
    if (!dest) return 0;
    *dest = NULL;
    if (n) *n = 0;

    // null string
    if (!src) return 1;

    char *result;

    // length is expected to max at 0x7fff
    int nwchar = (unsigned short)src[0];

    // empty string
    if (nwchar == 0)
    {
        result = malloc(1);
        if (!result) return 0;
        result[0] = '\0';
        *dest = result;
        return 1;
    }

    const wchar_t *start = src + 1;

    int buff_size = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        start,
        nwchar,
        NULL,
        0,
        NULL,
        NULL
    );
    if (buff_size == 0) return 0;

    result = malloc((size_t)buff_size + 1);
    if (!result) return 0;

    int written = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        start,
        nwchar,
        result,
        buff_size,
        NULL,
        NULL
    );
    if (written == 0)
    {
        free(result);
        return 0;
    }

    // output is 0 terminated
    result[written] = '\0';
    if (n) *n = written;

    *dest = result;

    return 1;
}

int utf8_2_xlstr(wchar_t **dest, const char *src, int n)
{

    if (!dest) return 0;
    *dest = NULL;

    if (n < -1) return 0;

    if (!src) return 1;

    wchar_t *result;

    // empty string
    if (n == 0)
    {
        // allocate 2 for safety
        result = malloc(2*sizeof(*result));
        if (!result) return 0;
        // write length
        result[0] = 0;
        result[1] = L'\0';
        *dest = result;
        return 1;
    }

    int nwchar = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        src,
        n,
        NULL,
        0
    );
    if (nwchar == 0) return 0;

    // check for string length
    int char_only_len = (n == -1) ? nwchar - 1 : nwchar;
    if (char_only_len > XLSTR_MAX_LEN) return 0;

    // add one for length prefix
    result = malloc(((size_t)nwchar+1)*sizeof(*result));
    if (!result) return 0;

    int written = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        src,
        n,
        result+1,
        nwchar
    );
    if (written == 0)
    {
        free(result);
        return 0;
    }

    // if 0 terminated string, dont count 0
    if (n == -1) --written;

    // write length prefix
    result[0] = (unsigned short)written;

    *dest = result;
    return 1;
}

LPXLOPER12 make_string_cell(const char *utf8str)
{

    if (!utf8str) return NULL;

    wchar_t *xlstr = NULL;
    if (utf8_2_xlstr(&xlstr, utf8str, -1) == 0 || !xlstr)
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

    result->xltype = xltypeStr | xlbitDLLFree; 
    result->val.str = xlstr;

    return result;
}

int is_null_or_whitespace_xlstr(const wchar_t *xlstr)
{

    if (!xlstr) return 1;
    
    size_t n = (unsigned short)xlstr[0];

    for (size_t i = 1; i <= n; i++)
    {
        // If any character is not whitespace, return false
        if (!iswspace((wint_t)xlstr[i]))
            return 0;
    }

    return 1;
}

void xloper12_member_deep_free(LPXLOPER12 pxFree)
{
    if (!pxFree) return;

    switch (pxFree->xltype & XLTYPEMASK)
    {
        case xltypeMulti:
            // only free if xlbitDLLFree is set
            if (pxFree->xltype & xlbitDLLFree) {
                LPXLOPER12 p = pxFree->val.array.lparray;
                if (p)
                {
                    // won't overflow
                    size_t n = (size_t)pxFree->val.array.rows *(size_t)pxFree->val.array.columns;
                    for (size_t i=0; i < n; i++)
                    {
                        // recursively free members with xltypeStr, xltypeMulti
                        // try to reduce function call for simple type
                        switch (p[i].xltype & XLTYPEMASK)
                        {
                            case xltypeStr:
                            case xltypeMulti:
                                xloper12_member_deep_free(&p[i]);
                                break;
                            default:
                                break;
                        }
                    }
                }
                // free array itself
                free(pxFree->val.array.lparray);
            }
            break;

        case xltypeStr:
            // only free if xlbitDLLFree is set
            if (pxFree->xltype & xlbitDLLFree)
                free(pxFree->val.str);
            break;

        default:
            break;
    }
}

void free_xloper12(LPXLOPER12 pxFree)
{
    if (!pxFree) return;
    xloper12_member_deep_free(pxFree);
    free(pxFree);
}

void free_xloper12_array(LPXLOPER12 lparray, size_t n)
{
    if (!lparray) return;
    for (size_t i = 0; i < n; ++i) {
        xloper12_member_deep_free(&lparray[i]);
    }
    free(lparray);
}

int xloper12_deep_copy(XLOPER12 *dst, const XLOPER12 *src)
{
    if (dst == src) return 1;
    if (!dst || !src) return 0;

    XLOPER12 temp = {0};    // fill with safe default value

    // make shallow copy first
    temp = *src;
    // remove xlbitDLLFree, only add when succeed
    temp.xltype &= ~xlbitDLLFree;

    switch (src->xltype & XLTYPEMASK) 
    {
        case xltypeStr:
        {
            // copy string
            wchar_t *ws_src = src->val.str;
            if (!ws_src) break;                 // NULL is accept
            size_t nwchar = (unsigned short)ws_src[0]; 
            wchar_t *ws = malloc((nwchar + 1)*sizeof(*ws));
            if (!ws) return 0;
            wmemcpy(ws, ws_src, (nwchar + 1));
            temp.val.str = ws;
            temp.xltype |= xlbitDLLFree;    // mark xlbitDLLFree when succeed
            break;
        }
        case xltypeMulti:
        {
            // copy lparray
            // excel max number of cell won't overflow size_t
            size_t n = (size_t)src->val.array.rows * (size_t)src->val.array.columns;
            if (n == 0)
            {
                temp.val.array.lparray = NULL;
                break;
            }
            LPXLOPER12 src_arr = src->val.array.lparray;
            if (!src_arr) return 0;    // n > 0
            LPXLOPER12 arr = calloc(n, sizeof(*arr));   // fill with safe default value
            if (!arr) return 0;
            for (size_t i=0; i < n; i++)
            {
                // recursively copy members with xltypeStr, xltypeMulti
                // for simple type, shallow copy is enough
                switch (src_arr[i].xltype & XLTYPEMASK)
                {
                    case xltypeStr:
                    case xltypeMulti:
                        if (xloper12_deep_copy(&arr[i], &src_arr[i]) == 0)
                        {
                            // recursively rollback
                            free_xloper12_array(arr, i);
                            return 0;
                        }
                        break;
                    case xltypeBool:
                    case xltypeInt:
                    case xltypeNum:
                    case xltypeMissing:
                    case xltypeNil:
                    case xltypeErr:
                        arr[i] = src_arr[i];
                        arr[i].xltype &= ~xlbitDLLFree; // for simple type, remove xlbitDLLFree
                        break;
                    default:
                        free_xloper12_array(arr, i); //unimplement, rollback
                        return 0;
                }
            }
            temp.val.array.lparray = arr;
            temp.xltype |= xlbitDLLFree;    // mark xlbitDLLFree when succeed
            break;
        }
        case xltypeBool:
        case xltypeInt:
        case xltypeNum:
        case xltypeMissing:
        case xltypeNil:
        case xltypeErr:
            break;      // shallow copy is enough
        default:
            return 0;  // unimplement
    }
    *dst = temp;
    return 1;
}