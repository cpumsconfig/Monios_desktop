# MoniOS MinGW Workspace

This directory is the first MoniOS-targeted C toolchain sample.

The build has three explicit stages:

1. `x86_64-w64-mingw32-gcc` compiles `hello.c` with `-nostdinc` and only the
   headers in `include/` and `user/lib/`.
2. `as` assembles `hello.S` into a PE/COFF object.
3. `ld` links the objects with `user/apps/app.ld`, the MoniOS runtime objects,
   and only the MoniOS DLL import libraries.

No host libc, libm, compiler default libraries, MinGW headers, or Windows SDK
libraries are used by the sample application.

Build from the repository root:

```text
make mingw-sample
```

The result is:

```text
out/mingw_hello.exe
```

The top-level image build copies the executable and the source files to:

```text
C:\Monios\Apps\mingw\
```

The sample uses only `appsys.h`, `stdio.h`, `stdint.h`, `string.h`, the
MoniOS runtime, and the `console.dll`, `windows.dll`, `osui.dll`, and
`monios.dll` import libraries.

## Self-written tiny toolchain

The same directory also contains a self-written first-stage toolchain:

```text
gcc.exe -> .mas -> as.exe -> .mobj -> ld.exe -> PE32+ .exe
```

These are MoniOS user applications, not wrappers around the host GNU tools.
The current front-end is intentionally small and understands ordered
`puts("...")`, `printf("...")`, and `putchar('x')` calls plus an integer
`return` statement; output calls are optional. The assembler creates the
MoniOS `MOBJ` format, accepts line comments, and the linker emits a PE32+
image with no external imports. The generated program writes through the
MoniOS syscall ABI when needed and then exits.

Inside MoniOS:

```text
C:\Monios\Apps\mingw\gcc.exe C:\Monios\Apps\mingw\selftest.c -S -o C:\Monios\Apps\mingw\selftest.mas
C:\Monios\Apps\mingw\as.exe C:\Monios\Apps\mingw\selftest.mas -o C:\Monios\Apps\mingw\selftest.mobj
C:\Monios\Apps\mingw\ld.exe C:\Monios\Apps\mingw\selftest.mobj -o C:\Monios\Apps\mingw\selftest.exe
run C:\Monios\Apps\mingw\selftest.exe
```

This is stage one of a freestanding compiler, not GNU GCC compatibility yet.
