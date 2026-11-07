// SPDX-License-Identifier: MPL-2.0
// Offline validation using the actual pinned M-Flasher package parser. No device I/O.
#[allow(dead_code)]
#[path = "../tests/vendor/m-flasher/formats.rs"]
mod formats;

fn main() {
    use formats::*;
    let args: Vec<String> = std::env::args().collect();
    assert!(args.len() == 4, "verify_fwsc PACKAGE.fwsc BUILT_APP.bin [EXPECTED_ID]");
    let raw = std::fs::read(&args[1]).expect("read package");
    let pkg = Package::parse(&raw).expect("M-Flasher must accept the package");
    assert_eq!(pkg.identity, args[3].as_str());
    assert_eq!(pkg.model(), "FM-1");
    assert_eq!(pkg.flash.len(), 0x93000, "keep the standard app boundary");
    assert_eq!(app_files_status(&pkg.flash, 0x980F), AppStatus::Ok);
    let table = app_table(&pkg.flash, 0x980F);
    let app = table.iter().find(|e| e.name == "app.bin").expect("app.bin entry");
    let decoded = app_read(&pkg.flash, BOOT_AREA, app.addr as usize, app.len as usize, 0x980F);
    let built = std::fs::read(&args[2]).expect("read built app");
    assert!(decoded.len() >= built.len());
    assert!(decoded[..built.len()] == built, "package app must match the build");
    assert!(decoded[built.len()..].iter().all(|&b| b == 0xFF), "slot padding must be erased bytes");
    assert!(pkg.files.contains_key("ota.bin"), "keep the platform updater");
    // Audio Router's persistent FX A/B pair must stay outside the package.
    // M-Flasher ops.rs plan_firmware(false, false) writes only this app span;
    // its optional wipe clears the package-declared VM/BTIF ranges instead.
    let fx_start=0xD8000usize;
    let fx_end=0xDA000usize;
    assert!(pkg.flash.len()<=fx_start, "firmware writes must not cover FX settings");
    assert!(table.iter().any(|e| e.name=="VM" && (e.attr==0x12 || e.attr==0x92)
        && e.addr as usize<=fx_start && e.addr as usize+e.len as usize>=fx_end),
        "expected VM erase region covers FX settings");
    println!("PASS: unchecked erase preserves FX A/B sectors [0xD8000, 0xDA000); checked VM erase includes them");
    for e in table { println!("{}: offset {:#x}, length {:#x}, type {:#x}", e.name, e.addr, e.len, e.attr); }
    println!("PASS: {} accepted by M-Flasher; decrypted app matches the build; CRCs valid", pkg.identity);
    println!("M-Flasher with settings erase disabled uses [0x4000, 0x93000); boot and upper data stay outside that range.");
}
