extern crate cbindgen;

use std::env;

fn main() {
    let crate_dir = env::var("CARGO_MANIFEST_DIR").unwrap();
    
    cbindgen::Builder::new()
        .with_crate(crate_dir.clone())
        .generate()
        .expect("Unable to generate bindings")
        .write_to_file("../src/mudbindings.h");
    let mut ocl_path=std::path::PathBuf::from(crate_dir.clone());
    println!("cargo:rustc-link-search=native={}", ocl_path.display());
}
