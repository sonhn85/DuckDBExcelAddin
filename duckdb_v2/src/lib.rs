mod xll;

#[unsafe(no_mangle)]
pub extern "system" fn xlAutoOpen() {
    // initialization code
}

#[unsafe(no_mangle)]
pub extern "system" fn xlAutoClose() {
    // cleanup code
}

#[unsafe(no_mangle)]
pub extern "system" fn xlAutoFree12(pxFree: xll::LPXLOPER12) {
    // cleanup code
}

fn create_str_xloper12(s: &str) {
	
}