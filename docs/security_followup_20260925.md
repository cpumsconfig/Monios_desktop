# 2026-09-25 security follow-up

This is a partial source audit, not certification that every project file has
been reviewed or that the OS is free of vulnerabilities.

## Changes this round

- DOS: correct conventional RAM size, guard interpreter reads/writes, bound COM
  loading, validate MZ header/relocation spans, read relocations from the image,
  correct COM entry offsets and RETF stack consumption, reject execution timeout,
  terminate serial character strings.
- VFS: authorize both rename paths before any mutation; authorize attribute writes.
- DNS: reject truncated/nonstandard responses, oversized decoded names and embedded
  NUL/separators; require CNAME data to match its declared length; do not cache TTL 0.
- Cache: preserve dirty data when eviction encounters failed/short writes.
- EXT: validate inode geometry and copy only known inode fields, preserving the
  on-disk extension instead of reading beyond a stack object.

## Reproduction

Run `python tests/run_security_tests.py` for ZIP, DOS, cache, EXT and DNS tests.
Run `tests\build_all_tests.bat` for the existing host suites, including new VFS
authorization regression cases. Build `out/kernel.unsigned.exe` with mingw32-make.

## Remaining coverage

The host stubs do not exercise real block-device failures, concurrent syscalls,
hardware DMA, or boot-time integration. A signed image and QEMU/hardware boot
verification have not been produced in this round. Other code, especially
permission path canonicalization/persistence, partial-block cache writes, user
credential provisioning, cryptography and device drivers, still needs detailed
review. Do not infer whole-repository coverage from successful kernel compilation.
