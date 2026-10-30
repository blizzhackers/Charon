# LuaJIT

LuaJIT 2.1 by Mike Pall, vendored from
[LuaJIT/LuaJIT](https://github.com/LuaJIT/LuaJIT/tree/8ca762976ebc85ad9bde3e15f147a7e326e0dd01)
branch `v2.1` at commit `8ca762976ebc85ad9bde3e15f147a7e326e0dd01` (committed
2026-10-29). MIT license; see `upstream/COPYRIGHT`.

`upstream/` holds `COPYRIGHT`, `README`, `dynasm/` and `src/` exactly as
downloaded, plus `.relver` containing the commit timestamp (`1793275035`) that
upstream's `git archive` export would substitute. Documentation, `etc/` and the
top-level Makefile are omitted. No source changes are applied.

## Build

`CMakeLists.txt` reproduces `src/msvcbuild.bat static` for 32-bit x86 without a
Visual Studio developer shell: `minilua` runs DynASM over `vm_x86.dasc` (flags
`WIN JIT FFI ENDIAN_LE FPU`), `buildvm` generates `lj_vm.obj` and the library
definition headers, and `host/genversion.lua` stamps `luajit.h`. The core is
compiled with `/O2 /arch:SSE2` into `luajit_static`, using the same static CRT
as `Charon.dll`. Charon's no-optimization rule covers its own hook code only.

FFI is compiled in but not exposed to scripts unless `-luaffi` is given.

## Error handling contract

On 32-bit Windows LuaJIT raises errors with `longjmp`, which skips C++
destructors. Charon bindings therefore throw `Script::Error` and are wrapped by
`Script::Bind`, which raises the Lua error only after every C++ object in the
binding has been destroyed (see `headers/script_host.h`).

To update: replace `upstream/` with the new commit's files, update `.relver`
with its commit timestamp (`git show -s --format=%ct`), and update this file.
Tree SHA-256 (sorted `sha256sum` of every file in `upstream/`, hashed again):
`7e3794061fe822d2e1f21ed1b0d9cee40f1f47a348899a190fb27d5f79c47125`.
