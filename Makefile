# Toolchain; override on the command line when needed:
#   make CC=clang CXX=clang++
CC ?= gcc
CXX ?= g++

DUCKDB_INC_PATH ?= lib/libduckdb-windows-amd64
EXCEL_SDK_PATH ?= lib/Excel2013XLLSDK
UTF8PROC_PATH ?= lib/utf8proc-2.12.0
UTHASH_PATH ?= lib/uthash
ADDIN_VERSION ?= dev

SRC_DIR := src
INC_DIR := include
BUILD_DIR := build
DIST_DIR := dist
XLL_OUT := $(DIST_DIR)/DuckDBExcelAddin.xll

EXCEL_SDK_SRC_PATH := $(EXCEL_SDK_PATH)/SRC
EXCEL_SDK_INC_PATH := $(EXCEL_SDK_PATH)/INCLUDE
FRAMEWRK_PATH := $(EXCEL_SDK_PATH)/SAMPLES/FRAMEWRK
FRAMEWRK_SRC_PATH := $(FRAMEWRK_PATH)
FRAMEWRK_INC_PATH := $(FRAMEWRK_PATH)

INCLUDES := \
	-I$(INC_DIR) \
	-I$(EXCEL_SDK_INC_PATH) \
	-I$(EXCEL_SDK_SRC_PATH) \
	-I$(DUCKDB_INC_PATH) \
	-I$(FRAMEWRK_INC_PATH) \
	-I$(UTHASH_PATH) \
	-I$(UTF8PROC_PATH)

CFLAGS := -O2 -DUTF8PROC_STATIC -DADDIN_VERSION=\"$(ADDIN_VERSION)\" $(INCLUDES)
LDFLAGS := -shared
LDLIBS := -lpathcch -lstdc++

OBJECTS := \
	$(BUILD_DIR)/memorypool.o \
	$(BUILD_DIR)/memorymanager.o \
	$(BUILD_DIR)/framewrk.o \
	$(BUILD_DIR)/excel4workaround.o \
	$(BUILD_DIR)/helper.o \
	$(BUILD_DIR)/db_lib_loader.o \
	$(BUILD_DIR)/db_xlrange.o \
	$(BUILD_DIR)/db_scalar_funcs.o \
	$(BUILD_DIR)/db_fetch.o \
	$(BUILD_DIR)/DuckDBExcelAddin.o \
	$(BUILD_DIR)/utf8proc.o

HEADERS := \
	$(DUCKDB_INC_PATH)/duckdb.h \
	$(UTHASH_PATH)/uthash.h \
	$(UTF8PROC_PATH)/utf8proc.h \
	$(EXCEL_SDK_INC_PATH)/XLCALL.H \
	$(FRAMEWRK_INC_PATH)/MemoryManager.h \
	$(FRAMEWRK_INC_PATH)/MemoryPool.h \
	$(FRAMEWRK_INC_PATH)/FRAMEWRK.H \
	$(INC_DIR)/helper.h \
	$(INC_DIR)/db_lib_loader.h \
	$(INC_DIR)/db_xlrange.h \
	$(INC_DIR)/db_scalar_funcs.h \
	$(INC_DIR)/db_fetch.h \
	$(INC_DIR)/DuckDBExcelAddin.h \
	$(INC_DIR)/config.h

.DEFAULT_GOAL := help

.PHONY: all xll clean help

all: xll

$(BUILD_DIR) $(DIST_DIR):
	mkdir -p $@

$(BUILD_DIR)/utf8proc.o: $(UTF8PROC_PATH)/utf8proc.c $(UTF8PROC_PATH)/utf8proc_data.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/memorypool.o: $(FRAMEWRK_SRC_PATH)/MemoryPool.cpp $(HEADERS) | $(BUILD_DIR)
	$(CXX) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/memorymanager.o: $(FRAMEWRK_SRC_PATH)/MemoryManager.cpp $(HEADERS) | $(BUILD_DIR)
	$(CXX) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/framewrk.o: $(FRAMEWRK_SRC_PATH)/FRAMEWRK.C $(EXCEL_SDK_SRC_PATH)/XLCALL.CPP $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -x c -c -o $@ $<

$(BUILD_DIR)/excel4workaround.o: $(SRC_DIR)/excel4workaround.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/helper.o: $(SRC_DIR)/helper.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/db_lib_loader.o: $(SRC_DIR)/db_lib_loader.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/db_xlrange.o: $(SRC_DIR)/db_xlrange.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/db_scalar_funcs.o: $(SRC_DIR)/db_scalar_funcs.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/db_fetch.o: $(SRC_DIR)/db_fetch.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/DuckDBExcelAddin.o: $(SRC_DIR)/DuckDBExcelAddin.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(XLL_OUT): $(OBJECTS) | $(DIST_DIR)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

xll: $(XLL_OUT)

clean:
	rm -rf $(BUILD_DIR) $(XLL_OUT)

help:
	@echo "DuckDB Excel Add-in Build"
	@echo ""
	@echo "Targets:"
	@echo "  all     Build the add-in"
	@echo "  xll     Build $(XLL_OUT)"
	@echo "  clean   Remove generated files"
	@echo "  help    Show this help message"
	@echo ""
	@echo "Configuration:"
	@echo "  ADDIN_VERSION (default: dev)"
	@echo "  DUCKDB_INC_PATH"
	@echo "  EXCEL_SDK_PATH"
	@echo "  CC"
	@echo "  CXX"
