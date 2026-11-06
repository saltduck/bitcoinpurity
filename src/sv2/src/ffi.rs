// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license.
use crate::runtime::{Config, Gateway, Node};
use serde_json::Value;
use std::{
    ffi::c_void,
    panic::{catch_unwind, AssertUnwindSafe},
    ptr,
    sync::Arc,
};
#[repr(C)]
pub struct Callbacks {
    context: *mut c_void,
    request: Option<
        unsafe extern "C" fn(*mut c_void, *const u8, usize, *mut *mut u8, *mut usize) -> i32,
    >,
    release: Option<unsafe extern "C" fn(*mut u8, usize)>,
}
struct Provider {
    context: usize,
    request: unsafe extern "C" fn(*mut c_void, *const u8, usize, *mut *mut u8, *mut usize) -> i32,
    release: unsafe extern "C" fn(*mut u8, usize),
}
impl Node for Provider {
    fn request(&self, request: Value) -> Result<Value, ()> {
        let data = serde_json::to_vec(&request).map_err(|_| ())?;
        let mut buffer = ptr::null_mut();
        let mut length = 0usize;
        unsafe {
            if (self.request)(
                self.context as *mut c_void,
                data.as_ptr(),
                data.len(),
                &mut buffer,
                &mut length,
            ) != 0
            {
                return Err(());
            }
            if buffer.is_null() {
                return Err(());
            }
            let result = if length <= 32 * 1024 * 1024 {
                serde_json::from_slice(std::slice::from_raw_parts(buffer, length)).map_err(|_| ())
            } else {
                Err(())
            };
            (self.release)(buffer, length);
            result
        }
    }
}
unsafe fn config(data: *const u8, length: usize) -> Option<Config> {
    if data.is_null() || length > 65536 {
        return None;
    }
    let cfg: Config = serde_json::from_slice(std::slice::from_raw_parts(data, length)).ok()?;
    cfg.validate().ok()?;
    Some(cfg)
}
#[no_mangle]
pub unsafe extern "C" fn purity_sv2_validate(data: *const u8, length: usize) -> i32 {
    catch_unwind(|| {
        if config(data, length).is_some() {
            0
        } else {
            -1
        }
    })
    .unwrap_or(-1)
}
#[no_mangle]
pub unsafe extern "C" fn purity_sv2_start(
    data: *const u8,
    length: usize,
    callbacks: Callbacks,
) -> *mut c_void {
    catch_unwind(AssertUnwindSafe(|| {
        let cfg = config(data, length)?;
        if callbacks.context.is_null() {
            return None;
        }
        let node = Provider {
            context: callbacks.context as usize,
            request: callbacks.request?,
            release: callbacks.release?,
        };
        let gateway = Gateway::start(cfg, Arc::new(node)).ok()?;
        Some(Box::into_raw(Box::new(gateway)) as *mut c_void)
    }))
    .ok()
    .flatten()
    .unwrap_or(ptr::null_mut())
}
#[no_mangle]
pub unsafe extern "C" fn purity_sv2_status(
    handle: *const c_void,
    output: *mut u8,
    capacity: usize,
) -> usize {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() {
            return 0;
        }
        let gateway = &*(handle as *const Gateway);
        let Ok(data) = serde_json::to_vec(&gateway.status()) else {
            return 0;
        };
        if !output.is_null() && capacity >= data.len() {
            ptr::copy_nonoverlapping(data.as_ptr(), output, data.len());
        }
        data.len()
    }))
    .unwrap_or(0)
}
#[no_mangle]
pub unsafe extern "C" fn purity_sv2_interrupt(handle: *const c_void) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if !handle.is_null() {
            (&*(handle as *const Gateway)).interrupt();
        }
    }));
}
#[no_mangle]
pub unsafe extern "C" fn purity_sv2_stop(handle: *mut c_void) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if !handle.is_null() {
            Box::from_raw(handle as *mut Gateway).stop();
        }
    }));
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn invalid_ffi_inputs_do_not_unwind() {
        unsafe {
            assert_eq!(purity_sv2_validate(ptr::null(), 0), -1);
            assert_eq!(purity_sv2_validate(b"{}".as_ptr(), 2), -1);
            assert_eq!(purity_sv2_status(ptr::null(), ptr::null_mut(), 0), 0);
            purity_sv2_interrupt(ptr::null());
            purity_sv2_stop(ptr::null_mut());
        }
    }
}
