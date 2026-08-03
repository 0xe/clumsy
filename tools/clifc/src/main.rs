use std::env;
use std::fs;
use std::process::exit;

use anyhow::{anyhow, bail, Result};
use cranelift_codegen::isa::{self, CallConv};
use cranelift_codegen::settings;
use cranelift_codegen::{ir, Context};
use cranelift_control::ControlPlane;
use cranelift_module::{Linkage, Module, default_libcall_names, DataDescription};
use cranelift_object::{ObjectBuilder, ObjectModule};
use cranelift_reader::{parse_test, ParseOptions};

/// Derive the linker symbol name from a CLIF function name.
/// `%main` -> `main` (the Mach-O object writer adds the leading `_` itself).
fn symbol_name(func_name: &ir::UserFuncName) -> String {
    match func_name {
        ir::UserFuncName::Testcase(tc) => String::from_utf8_lossy(tc.raw()).to_string(),
        ir::UserFuncName::User(u) => format!("u{}:{}", u.namespace, u.index),
    }
}

/// One data declaration from the `;; data` section.
/// Line format:
///   `<pub|local> <name> <w|r> <size> <hex bytes... | ->`   (defined data)
///   `extern <name> <w|r> <size>`                           (cross-file import)
struct DataDecl {
    name: String,
    public: bool,
    writable: bool,
    bytes: Vec<u8>, // empty if zero-initialized
    zero: bool,
    import: bool,
}

fn parse_data(text: &str) -> Result<Vec<DataDecl>> {
    let mut out = Vec::new();
    for line in text.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with(';') {
            continue;
        }
        let mut it = line.split_whitespace();
        let linkage = it.next().ok_or_else(|| anyhow!("data line missing linkage"))?;
        let name = it.next().ok_or_else(|| anyhow!("data line missing name"))?;
        let wr = it.next().ok_or_else(|| anyhow!("data line missing writable"))?;
        let size: usize = it
            .next()
            .ok_or_else(|| anyhow!("data line missing size"))?
            .parse()?;
        let rest: Vec<&str> = it.collect();
        let (bytes, zero) = if linkage == "extern" {
            (Vec::new(), true)
        } else if rest.len() == 1 && rest[0] == "-" {
            (Vec::new(), true)
        } else {
            let mut bytes = Vec::new();
            for tok in rest {
                let b = u8::from_str_radix(tok.trim_start_matches("0x"), 16)
                    .map_err(|_| anyhow!("bad data byte {tok}"))?;
                bytes.push(b);
            }
            if bytes.len() != size {
                bail!("data {name}: expected {size} bytes, got {}", bytes.len());
            }
            (bytes, false)
        };
        out.push(DataDecl {
            name: name.to_string(),
            public: linkage == "pub",
            writable: wr == "w",
            bytes,
            zero,
            import: linkage == "extern",
        });
    }
    Ok(out)
}

fn main() {
    if let Err(e) = run() {
        eprintln!("clifc: {e:#}");
        exit(1);
    }
}

fn run() -> Result<()> {
    let args: Vec<String> = env::args().collect();
    if args.len() != 3 {
        bail!("usage: clifc <input.clif> <output.o>");
    }
    let input = &args[1];
    let output = &args[2];

    let text = fs::read_to_string(input)?;

    // Variadic externs: the emitter marks them so we can emit x8-zeroing
    // assembly stubs (`_<name>_stub` tail-jumps to `_<name>` with x8 = 0,
    // which the Apple AArch64 variadic ABI requires but cranelift 0.134 has
    // no way to express).
    let mut variadic: Vec<String> = Vec::new();
    for line in text.lines() {
        if let Some(rest) = line.trim_start().strip_prefix(";; variadic ") {
            let name = rest.trim();
            if !name.is_empty() && !variadic.iter().any(|n| n == name) {
                variadic.push(name.to_string());
            }
        }
    }

    // Split at the `;; data` marker line: the function part goes to the CLIF
    // reader, the data part to our own mini-parser. The data section may come
    // first (a data-only file with no functions), so also match a marker at
    // the very start of the file.
    let (func_text, data_text) = if text.starts_with(";; data") {
        ("", text.as_str())
    } else {
        match text.find("\n;; data") {
            Some(idx) => (&text[..idx], &text[idx..]),
            None => (text.as_str(), ""),
        }
    };

    let funcs = {
        if func_text.trim().is_empty() {
            Vec::new()
        } else {
            let options = ParseOptions {
                default_calling_convention: CallConv::AppleAarch64,
                ..Default::default()
            };
            parse_test(func_text, options)?
                .functions
                .into_iter()
                .map(|(f, _)| f)
                .collect::<Vec<_>>()
        }
    };

    let data = parse_data(data_text)?;

    // ISA for the host (aarch64-apple-darwin on Apple Silicon).
    let flags = settings::Flags::new(settings::builder());
    let isa = isa::lookup(target_lexicon::Triple::host())?.finish(flags)?;

    let builder = ObjectBuilder::new(isa, "a.out", default_libcall_names())?;
    let mut module = ObjectModule::new(builder);

    // Pass 1: declare all functions in file order. Externs are empty-body
    // functions -> Linkage::Import.
    let mut func_ids = Vec::new();
    for f in &funcs {
        let name = symbol_name(&f.name);
        let extern_fn = f.layout.blocks().next().is_none();
        let linkage = if extern_fn {
            Linkage::Import
        } else {
            Linkage::Export
        };
        let id = module.declare_function(&name, linkage, &f.signature)?;
        func_ids.push(id);
    }

    // Pass 1b: declare data in file order.
    let mut data_ids = Vec::new();
    for d in &data {
        let linkage = if d.import {
            Linkage::Import
        } else if d.public {
            Linkage::Preemptible
        } else {
            Linkage::Local
        };
        let id = module.declare_data(&d.name, linkage, d.writable, false)?;
        data_ids.push(id);
    }

    // Pass 2: define each function body.
    for (i, f) in funcs.iter().enumerate() {
        if f.layout.blocks().next().is_none() {
            continue; // extern
        }
        let mut ctx = Context::new();
        ctx.func = f.clone();
        module.define_function_with_control_plane(
            func_ids[i],
            &mut ctx,
            &mut ControlPlane::default(),
        )?;
    }

    // Define data.
    for (i, d) in data.iter().enumerate() {
        if d.import {
            continue; // cross-file import: declaration only
        }
        let mut dc = DataDescription::new();
        if d.zero {
            dc.define_zeroinit(d.bytes.len());
        } else {
            dc.define(d.bytes.clone().into_boxed_slice());
        }
        module.define_data(data_ids[i], &dc)?;
    }

    let product = module.finish();
    let bytes = product.emit()?;
    fs::write(output, bytes)?;

    if !variadic.is_empty() {
        let stubs = write_stubs(output, &variadic)?;
        eprintln!("clifc: wrote {}", stubs.display());
    }
    Ok(())
}

/// Write `{output}.stubs.s` with variadic-call stubs for variadic externs.
///
/// On Apple AArch64 the variadic ABI passes varargs on the stack (at [sp],
/// [sp+8], ...), but cranelift 0.134 has no variadic support, so the emitter
/// calls these externs through a fixed 8-arg signature (x0-x7). Each stub
/// shuffles x1-x7 down to the stack, calls the real function, and unwinds.
/// Returns the path written.
fn write_stubs(output: &str, variadic: &[String]) -> Result<std::path::PathBuf> {
    let stub_path = std::path::Path::new(output)
        .with_file_name(format!(
            "{}.stubs.s",
            std::path::Path::new(output)
                .file_stem()
                .ok_or_else(|| anyhow!("bad output path"))?
                .to_string_lossy()
        ));
    let mut s = String::new();
    for name in variadic {
        // The emitter names the CLIF function `<name>_stub`, so the object
        // symbol is `_<name>_stub`; the real symbol is `_<name>`.
        s.push_str("    .text\n");
        s.push_str("    .p2align    2\n");
        s.push_str(&format!("    .globl  _{name}_stub\n"));
        s.push_str(&format!("_{name}_stub:\n"));
        s.push_str("    stp x29, x30, [sp, #-16]!\n");
        s.push_str("    mov x29, sp\n");
        s.push_str("    sub sp, sp, #64\n");
        s.push_str("    str x1, [sp]\n");
        s.push_str("    str x2, [sp, #8]\n");
        s.push_str("    str x3, [sp, #16]\n");
        s.push_str("    str x4, [sp, #24]\n");
        s.push_str("    str x5, [sp, #32]\n");
        s.push_str("    str x6, [sp, #40]\n");
        s.push_str("    str x7, [sp, #48]\n");
        s.push_str("    mov x8, #0\n");
        s.push_str(&format!("    bl   _{name}\n"));
        s.push_str("    add sp, sp, #64\n");
        s.push_str("    ldp x29, x30, [sp], #16\n");
        s.push_str("    ret\n\n");
    }
    fs::write(&stub_path, s)?;
    Ok(stub_path)
}
