DEST_DIR = target/release
SRC_DIR = src

$(DEST_DIR)/DuckDBExcelAddin.xll: $(SRC_DIR)/lib.rs
	cargo build --release
	mv $(DEST_DIR)/duckdb_excel_addin.dll $@