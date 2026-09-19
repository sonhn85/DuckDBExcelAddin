DUCKDB_INC_PATH ?= libduckdb-windows-amd64
EXCEL_SDK_PATH ?= Excel2013XLLSDK
ADDIN_VERSION ?= dev

SRC_DIR := src
INC_DIR := include
BUILD_DIR := build

EXCEL_SDK_SRC_PATH := $(EXCEL_SDK_PATH)/SRC
EXCEL_SDK_INC_PATH := $(EXCEL_SDK_PATH)/INCLUDE
FRAMEWRK_PATH := $(EXCEL_SDK_PATH)/SAMPLES/FRAMEWRK
FRAMEWRK_SRC_PATH := $(FRAMEWRK_PATH)
FRAMEWRK_INC_PATH := $(FRAMEWRK_PATH)
UTHASH_INC_PATH := lib/uthash

CPPFLAGS := -I$(INC_DIR) -I$(SRC_DIR) -I$(EXCEL_SDK_INC_PATH) -I$(DUCKDB_INC_PATH) -I$(FRAMEWRK_INC_PATH) -I$(UTHASH_INC_PATH)
CFLAGS := -O2 -DADDIN_VERSION=\"$(ADDIN_VERSION)\"

.DEFAULT_GOAL := help

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/memorypool.o: $(FRAMEWRK_SRC_PATH)/memorypool.cpp | $(BUILD_DIR)
	g++ $(CFLAGS) -c -o $@ -I$(FRAMEWRK_INC_PATH) $<

$(BUILD_DIR)/memorymanager.o: $(FRAMEWRK_SRC_PATH)/memorymanager.cpp | $(BUILD_DIR)
	g++ $(CFLAGS) -c -o $@ -I$(FRAMEWRK_INC_PATH) $<

$(BUILD_DIR)/framewrk.o: $(FRAMEWRK_SRC_PATH)/framewrk.c | $(BUILD_DIR)
	gcc $(CFLAGS) -c -o $@ -I$(FRAMEWRK_INC_PATH) -I$(EXCEL_SDK_INC_PATH) -I$(EXCEL_SDK_SRC_PATH) $<

$(BUILD_DIR)/excel4workaround.o: $(SRC_DIR)/excel4workaround.c | $(BUILD_DIR)
	gcc $(CFLAGS) -c $(CPPFLAGS) -o $@ $<

$(BUILD_DIR)/helper.o: $(SRC_DIR)/helper.c $(INC_DIR)/helper.h $(SRC_DIR)/helper.c | $(BUILD_DIR)
	gcc $(CFLAGS) -c $(CPPFLAGS) -o $@ $<

$(BUILD_DIR)/db_lib_loader.o: $(SRC_DIR)/db_lib_loader.c $(INC_DIR)/db_lib_loader.h $(INC_DIR)/helper.h $(INC_DIR)/config.h | $(BUILD_DIR)
	gcc $(CFLAGS) -c $(CPPFLAGS) -o $@ $<

$(BUILD_DIR)/db_xlrange.o: $(SRC_DIR)/db_xlrange.c $(INC_DIR)/db_xlrange.h $(INC_DIR)/helper.h $(INC_DIR)/db_lib_loader.h $(INC_DIR)/config.h | $(BUILD_DIR)
	gcc $(CFLAGS) -c $(CPPFLAGS) -I$(FRAMEWRK_INC_PATH) -I$(UTHASH_INC_PATH) -o $@ $<

$(BUILD_DIR)/db_scalar_funcs.o: $(SRC_DIR)/db_scalar_funcs.c $(INC_DIR)/db_scalar_funcs.h $(INC_DIR)/db_lib_loader.h $(INC_DIR)/helper.h $(INC_DIR)/config.h | $(BUILD_DIR)
	gcc $(CFLAGS) -c $(CPPFLAGS) -o $@ $<

$(BUILD_DIR)/db_fetch.o: $(SRC_DIR)/db_fetch.c $(INC_DIR)/db_fetch.h $(INC_DIR)/helper.h $(INC_DIR)/db_lib_loader.h $(INC_DIR)/config.h | $(BUILD_DIR)
	gcc $(CFLAGS) -c $(CPPFLAGS) -o $@ $<

$(BUILD_DIR)/DuckDBExcelAddin.o: $(SRC_DIR)/DuckDBExcelAddin.c $(INC_DIR)/DuckDBExcelAddin.h $(INC_DIR)/helper.h $(INC_DIR)/db_lib_loader.h $(INC_DIR)/db_xlrange.h $(INC_DIR)/db_fetch.h $(INC_DIR)/config.h | $(BUILD_DIR)
	gcc $(CFLAGS) -c $(CPPFLAGS) -I$(FRAMEWRK_INC_PATH) -o $@ $<

DuckDBExcelAddin.xll: $(BUILD_DIR)/memorypool.o $(BUILD_DIR)/memorymanager.o $(BUILD_DIR)/framewrk.o $(BUILD_DIR)/excel4workaround.o $(BUILD_DIR)/helper.o $(BUILD_DIR)/db_lib_loader.o $(BUILD_DIR)/db_scalar_funcs.o $(BUILD_DIR)/db_xlrange.o $(BUILD_DIR)/db_fetch.o $(BUILD_DIR)/DuckDBExcelAddin.o
	gcc -shared -o $@ $^ -lpathcch -lstdc++

xll: DuckDBExcelAddin.xll

clean:
	rm -rf $(BUILD_DIR)
	rm -f *.xll

help:
	@echo "DuckDB Excel Add-in Build"
	@echo ""
	@echo "Targets:"
	@echo "  xll     Build DuckDBExcelAddin.xll"
	@echo "  clean   Remove generated files"
	@echo "  help    Show this help message"
	@echo ""
	@echo "Configuration:"
	@echo "  ADDIN_VERSION (default to dev)"
	@echo "  DUCKDB_INC_PATH"
	@echo "  EXCEL_SDK_PATH"

.PHONY: help xll clean
