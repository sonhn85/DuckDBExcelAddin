DUCKDB_INC_PATH = libduckdb-windows-amd64
EXCEL_SDK_PATH = Excel2013XLLSDK

EXCEL_SDK_SRC_PATH = $(EXCEL_SDK_PATH)/SRC
EXCEL_SDK_INC_PATH = $(EXCEL_SDK_PATH)/INCLUDE
FRAMEWRK_PATH = $(EXCEL_SDK_PATH)/SAMPLES/FRAMEWRK
FRAMEWRK_SRC_PATH = $(FRAMEWRK_PATH)
FRAMEWRK_INC_PATH = $(FRAMEWRK_PATH)

ADDIN_VERSION := dev

CFLAGS = -DADDIN_VERSION=\"$(ADDIN_VERSION)\"

memorypool.o:
	g++ -O2 -c -o $@ -I$(FRAMEWRK_INC_PATH) $(FRAMEWRK_SRC_PATH)/memorypool.cpp

memorymanager.o:
	g++ -O2 -c -o $@ -I$(FRAMEWRK_INC_PATH) $(FRAMEWRK_SRC_PATH)/memorymanager.cpp

framewrk.o:
	gcc -O2 -c -o $@ -I$(FRAMEWRK_INC_PATH) -I$(EXCEL_SDK_INC_PATH) -I$(EXCEL_SDK_SRC_PATH) $(FRAMEWRK_SRC_PATH)/framewrk.c

excel4workaround.o:
	gcc -O2 -c -o $@ -I$(EXCEL_SDK_INC_PATH) excel4workaround.c

helper.o: helper.c helper.h config.h
	gcc -O2 -c $(CFLAGS) -o $@ -I. -I$(EXCEL_SDK_INC_PATH) $<

db_lib_loader.o: db_lib_loader.c db_lib_loader.h helper.h config.h
	gcc -O2 -c $(CFLAGS) -o $@ -I. -I$(EXCEL_SDK_INC_PATH) -I$(DUCKDB_INC_PATH) $<

db_xlrange.o: db_xlrange.c db_xlrange.h helper.h db_lib_loader.h config.h
	gcc -O2 -c $(CFLAGS) -o $@ -I. -I$(EXCEL_SDK_INC_PATH) -I$(DUCKDB_INC_PATH) $<

db_fetch.o: db_fetch.c db_xlrange.h helper.h db_lib_loader.h config.h
	gcc -O2 -c $(CFLAGS) -o $@ -I. -I$(EXCEL_SDK_INC_PATH) -I$(DUCKDB_INC_PATH) $<

DuckDBExcelAddin.o: DuckDBExcelAddin.c DuckDBExcelAddin.h helper.h db_lib_loader.h db_xlrange.h db_fetch.h config.h
	gcc -O2 -c $(CFLAGS) -o $@ -I. -I$(EXCEL_SDK_INC_PATH) -I$(FRAMEWRK_INC_PATH) -I$(DUCKDB_INC_PATH) $<

DuckDBExcelAddin.xll: memorypool.o memorymanager.o framewrk.o excel4workaround.o helper.o db_lib_loader.o db_xlrange.o db_fetch.o DuckDBExcelAddin.o
	gcc -shared -o $@ $^ -lpathcch -lstdc++

all: DuckDBExcelAddin.xll

clean:
	rm -f *.o
	rm -f *.xll
