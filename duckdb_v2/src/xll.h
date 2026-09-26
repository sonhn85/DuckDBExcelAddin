typedef void *HANDLE;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef int INT32;
typedef unsigned long DWORD;
typedef unsigned long long DWORD_PTR;
typedef unsigned short WCHAR;

typedef INT32 BOOL;
typedef WCHAR XCHAR;
typedef INT32 RW;
typedef INT32 COL;
typedef DWORD_PTR IDSHEET;

typedef struct xlref12
{
	RW rwFirst;
	RW rwLast;
	COL colFirst;
	COL colLast;
} XLREF12, *LPXLREF12;

typedef struct xlmref12
{
	WORD count;
	XLREF12 reftbl[1];
} XLMREF12, *LPXLMREF12;

typedef struct xloper12 
{
	union 
	{
		double num;
		XCHAR *str;
		BOOL xbool;
		int err;
		int w;
		struct 
		{
			WORD count;
			XLREF12 ref;
		} sref;
		struct 
		{
			XLMREF12 *lpmref;
			IDSHEET idSheet;
		} mref;
		struct 
		{
			struct xloper12 *lparray;
			RW rows;
			COL columns;
		} array;
		struct 
		{
			union
			{
				int level;
				int tbctrl;
				IDSHEET idSheet;
			} valflow;
			RW rw;
			COL col;
			BYTE xlflow;
		} flow;
		struct
		{
			union
			{
				BYTE *lpbData;
				HANDLE hdata;
			} h;
			long cbData;
		} bigdata;
	} val;
	DWORD xltype;
} XLOPER12, *LPXLOPER12;

#define xltypeNum        0x0001
#define xltypeStr        0x0002
#define xltypeBool       0x0004
#define xltypeErr        0x0010
#define xltypeMulti      0x0040
#define xltypeMissing    0x0080
#define xltypeNil        0x0100
#define xltypeInt        0x0800

#define xlbitXLFree      0x1000
#define xlbitDLLFree     0x4000

#define xlerrNull    0
#define xlerrNA      42

#define xlretSuccess        0 
#define xlretFailed         32  

#define xlSpecial    0x4000

#define xlFree          (0  | xlSpecial)
#define xlCoerce        (2  | xlSpecial)
#define xlGetHwnd       (8  | xlSpecial)
#define xlGetName       (9  | xlSpecial)
#define xlGetBinaryName	(13 | xlSpecial)
#define xlAsyncReturn	(16 | xlSpecial)
#define xlGetInstPtr	(19 | xlSpecial)

#define xlfRegister 149
#define xlfUnregister 201

int _cdecl Excel12(int xlfn, LPXLOPER12 operRes, int count,... );