#![no_main]
use libfuzzer_sys::fuzz_target;
fuzz_target!(|data: &[u8]| {
    let mut data = data.to_vec();
    let _ = purity_sv2::wire::validate_plain_frame(&mut data);
    if data.len() >= 6 && data.len() - 6 < 16 * 1024 * 1024 {
        let length = (data.len() - 6) as u32;
        data[3..6].copy_from_slice(&length.to_le_bytes()[..3]);
        let _ = purity_sv2::wire::validate_plain_frame(&mut data);
    }
});
