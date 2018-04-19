extern crate gcc;

fn main() {
    gcc::compile_library("libbridgesample.a", &["src/rtp.c"]);
}
