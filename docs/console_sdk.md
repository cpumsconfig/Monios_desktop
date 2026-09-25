# MoniOS Console SDK

MoniOS Console applications are PE32+ images with the `console` subsystem.
Their standard output is connected to the process stdout handle and is routed
through `console.dll`.

## Minimal application

```c
#include <stdio.h>

int main(int argc, char **argv)
{
    (void) argc;
    (void) argv;
    printf("Hello World\n");
    return 0;
}
```

The complete sample is in `examples/hello.c`.

## Build with the drivers

From the repository root:

```text
make app-runtime
python3 tools/monios-gcc.py -c examples/hello.c -o out/hello.o
python3 tools/monios-ld.py --subsystem console -o out/hello.exe out/hello.o
```

Or use the integrated target:

```text
make hello
make hd.img
```

The resulting application is `C:\Monios\Apps\hello.exe` in the FAT32 image. Start it
from the MoniOS shell with:

```text
run C:\Monios\Apps\hello.exe
```

Console applications receive a process console window automatically. Its
initial title is the resolved program path. A program can change it through
`console.dll`:

```c
#include "console_dll.h"

console_set_title("My application");
```

`monios-gcc.py` supplies the freestanding x86_64 flags and MoniOS include
paths. `monios-ld.py` supplies `user/apps/app.ld`, `_start`, the Console
subsystem, the runtime objects, and the three system DLL import libraries.

The Windows command wrappers are:

```text
tools/monios-gcc.cmd
tools/monios-ld.cmd
```

The linker also accepts `--subsystem windows` and `--subsystem native` for
future GUI and native images.

## MinGW Workspace

The first three-stage MoniOS toolchain sample is kept in
`user/apps/mingw/`. It compiles `hello.c` with GCC using `-nostdinc`, assembles
`hello.S` with `as`, and links the final PE32+ image with `ld`.

```text
make mingw-sample
```

The sample links only the MoniOS runtime objects and the
`console.dll`/`windows.dll`/`osui.dll`/`monios.dll` import libraries. It does
not use the host C library, MinGW headers, Windows SDK libraries, `libgcc`, or
any other default toolchain library. The generated program is
`out/mingw_hello.exe`; `make hd.img` copies it and the source files to
`C:\Monios\Apps\mingw\`.

The directory also ships MoniOS-native first-stage replacements for the three
toolchain commands:

```text
make mingw-tools
```

This builds `out/mingw_gcc.exe`, `out/mingw_as.exe`, and `out/mingw_ld.exe`.
They are regular MoniOS console applications. Their pipeline is
`C -> .mas -> .mobj -> PE32+`, where `MOBJ` is a small MoniOS object format and
the generated executable has no DLL imports. The current C front-end supports
ordered `puts("...")`, `printf("...")`, and `putchar('x')` calls plus an
integer `return`; output calls are optional. It is the freestanding bootstrap
stage for a larger compiler, not GNU GCC compatibility.
