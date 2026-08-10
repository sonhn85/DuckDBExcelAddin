#include <windows.h>
#include "XLCALL.H"

int _cdecl Excel4(int xlfn, LPXLOPER operRes, int count,... )
{
   return xlretAbort;
}

int pascal Excel4v(int xlfn, LPXLOPER operRes, int count, LPXLOPER opers[])
{
   return xlretAbort;
}
