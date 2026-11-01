#![no_main]
use libfuzzer_sys::fuzz_target;
fuzz_target!(|data: &[u8]| {
    unsafe {
        let _ = purity_sv2::ffi::purity_sv2_validate(data.as_ptr(), data.len());
        let _ = purity_sv2::ffi::purity_sv2_status(std::ptr::null(), std::ptr::null_mut(), 0);
        purity_sv2::ffi::purity_sv2_interrupt(std::ptr::null());
        purity_sv2::ffi::purity_sv2_stop(std::ptr::null_mut());
    }
});
