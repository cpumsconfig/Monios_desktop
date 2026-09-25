MAKEFILE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
PROJECT_ROOT := $(abspath $(MAKEFILE_DIR)/../../..)
OUT_DIR := $(PROJECT_ROOT)/out

PYTHON ?= python3
MINGW_CC ?= x86_64-w64-mingw32-gcc
ifeq ($(OS),Windows_NT)
MINGW_AS ?= as
MINGW_LD ?= ld
PYTHON ?= python
else
MINGW_AS ?= x86_64-w64-mingw32-as
MINGW_LD ?= x86_64-w64-mingw32-ld
endif

MONIOS_INCLUDE = -I$(PROJECT_ROOT)/include -I$(PROJECT_ROOT)/user/lib
MINGW_CFLAGS = -c -nostdinc $(MONIOS_INCLUDE) -O0 -ffreestanding -fno-builtin \
	-fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone \
	-m64 -mabi=sysv -fno-pie
MINGW_ASFLAGS = --64
MINGW_LDFLAGS = -m i386pep -nostdlib \
	-T$(PROJECT_ROOT)/user/apps/app.ld --entry _start \
	--image-base 0x03fff000 --subsystem console
MONIOS_RUNTIME = $(addprefix $(OUT_DIR)/,app_runtime.pe.o appsys.pe.o stdio.pe.o stdlib.pe.o unistd.pe.o string.pe.o)
MONIOS_IMPORTS = $(addprefix $(OUT_DIR)/,libconsole.dll.a libosui.dll.a libwindows.dll.a libmonios.dll.a)
MONIOS_LIBS = -L$(OUT_DIR) -lconsole -losui -lwindows -lmonios

SAMPLE_C_OBJECT = $(OUT_DIR)/mingw_hello_c.pe.o
SAMPLE_AS_OBJECT = $(OUT_DIR)/mingw_hello_s.pe.o
SAMPLE_BINARY = $(OUT_DIR)/mingw_hello.exe

.PHONY: all runtime clean

all: $(SAMPLE_BINARY)

runtime:
	"$(MAKE)" -C "$(PROJECT_ROOT)" app-runtime

$(SAMPLE_C_OBJECT): hello.c
	$(MINGW_CC) $(MINGW_CFLAGS) -o "$@" "$<"

$(SAMPLE_AS_OBJECT): hello.S
	$(MINGW_AS) $(MINGW_ASFLAGS) -o "$@" "$<"

$(SAMPLE_BINARY): runtime $(SAMPLE_C_OBJECT) $(SAMPLE_AS_OBJECT) $(MONIOS_RUNTIME) $(MONIOS_IMPORTS)
	$(MINGW_LD) $(MINGW_LDFLAGS) -o "$@" \
		$(MONIOS_RUNTIME) $(SAMPLE_C_OBJECT) $(SAMPLE_AS_OBJECT) $(MONIOS_LIBS)

clean:
	"$(MAKE)" -C "$(PROJECT_ROOT)" clean
