#include <windows.h>
#include "xlcall.h"

int _cdecl Excel4(int xlfn, LPXLOPER operRes, int count,... )
{
   return xlretFailed;
}

int pascal Excel4v(int xlfn, LPXLOPER operRes, int count, LPXLOPER opers[])
{
   return xlretFailed;
}
