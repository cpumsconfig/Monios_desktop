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
python tools/monios-gcc.py -c examples/hello.c -o out/hello.o
python tools/monios-ld.py --subsystem console -o out/hello.exe out/hello.o
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
