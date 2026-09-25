# Task 10 & 12 Delivery Report — Image Viewer + Settings Panel

## 1. Files created

| File | Purpose |
|------|---------|
| `user/apps/imageview.c` | Task 10: graphical image viewer (~27 KB) |
| `user/apps/settings.c` | Task 12: system settings panel (~18 KB) |

## 2. Files modified (kernel)

| File | Change |
|------|--------|
| `include/syscall.h` | Added `#define SYS_KEYBOARD_READ_EVENT 72` (after existing max 71) |
| `kernel/syscall/syscall.c` | Added one dispatch `case SYS_KEYBOARD_READ_EVENT` (~line 1636) |

No other kernel files touched. No new image-decode library was needed — BMP/PNG/JPG logic is self-contained in `imageview.c`.

### Why a new syscall was required
`keyboard_read_char()` (and thus stdin `read(0,...)`) only delivers printable chars + Ctrl+C; it **discards** arrows, F-keys and Esc (`kernel/input/keyboard.c:229`). The viewer needs arrows/F5/Esc. The new syscall drains the raw queue via `keyboard_poll_event()` and copies an 8-byte record to user space:

```
uint32 type; char ch; uint8 mods; uint8 pad[3];
```
returns 1 = event copied, 0 = queue empty, -1 = error. Both apps poll it directly with `syscall1(SYS_KEYBOARD_READ_EVENT, &ev)`. Key-type constants are mirrored locally in each .c.

## 3. Task 10 — imageview.c

**Formats**
- **BMP**: full 24/32-bit `BITMAPINFOHEADER`, bottom-up flip handled (top-down too), 4-byte row padding.
- **PNG**: signature + chunk walk (IHDR/PLTE/IDAT/IEND), 8-bit color types 0/2/3/4/6 (gray/RGB/palette/RGBA). zlib: skips 2-byte header and decodes **stored (BTYPE 00)** blocks; scanline filters None/Sub/Up/Average/Paeth reconstructed. Compressed (Huffman) PNGs show a `[com]` note and are not pixel-decoded (per task's "at minimum uncompressed PNG" allowance).
- **JPG**: parses SOI/APP0(JFIF)/SOF0, extracts width/height, shows file info; no DCT decode (optional per task). Decoded pixels flagged `[jpg]`.

**Viewing**
- `imageview <file>`; no arg → help screen.
- `+`/`-` zoom 10–500%, `0` = 100%, `F` = fit window.
- `R` clockwise, `L` counter-clockwise 90° (orientation tracked, sampled at draw time — no second buffer).
- Arrow keys / left-mouse drag pan.
- `F5` slideshow: lists same-dir `.bmp/.png/.jpg`, advances every 300 ticks (~3 s); `Esc` stops show, then quits.
- Bottom-left statusbar: file name, resolution (post-rotation), file size, zoom %, decode note.

**Rendering**: decoded buffer is a static `uint32 g_pixels[1024*768]` (0x00RRGGBB); drawn through `app_graphics_fill_rect()` blocks whose size scales with zoom (1 syscall/source block). No bitmap-blit syscall exists in the kernel, so this is the supported path; large images may be slow but functional (task explicitly allows this).

## 4. Task 12 — settings.c

Left `osui_navrail` with 6 pages; clickable controls + keys 1–6; `Esc`/`Q` quits.

- **Appearance**: theme radios (Default/Dark/Light/High contrast), wallpaper list (wallpaper.jpg/boot.bmp from `C:\Monios\System\Media`), font-size slider.
- **Display**: resolution list (800×600 … 1920×1080) + Apply button; refresh rate shown as unavailable.
- **Network**: live IP/gateway/MAC/online badge from `app_get_system_status()`, DNS input (display-only), proxy toggle.
- **Sound**: volume slider, mute toggle, output device radios, audio-device callout.
- **Power**: sleep-time radios (Never/5/10/30 min/1 h), power-button behavior, energy-saver toggle.
- **System**: version chip, CPU logical count, process count, uptime (ticks/100), memory/storage labels.

Persistence via `app_registry_get/set` on every change (`settings/theme`, `settings/dns`, etc.). Controls without backing kernel action are drawn but inert rather than faked.

## 5. Makefile changes required (NOT applied — per constraint)

The Makefile was only temporarily edited during verification and then restored. To wire the apps in permanently, add:

```make
# near APP_SMOKETEST_OBJS (~line 85):
APP_IMAGEVIEW_OBJS = $(APP_RUNTIME_OBJS) out/app_imageview.pe.o out/app_resource.pe.o
APP_SETTINGS_OBJS  = $(APP_RUNTIME_OBJS) out/app_settings.pe.o out/app_resource.pe.o

# link rules (after out/smoke_test.exe rule ~line 442):
out/imageview.exe : user/apps/app.ld $(APP_IMAGEVIEW_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,windows -o out/imageview.exe $(APP_IMAGEVIEW_OBJS) $(APP_LD_LIBS)
out/settings.exe : user/apps/app.ld $(APP_SETTINGS_OBJS)
	$(APP_LD) $(APP_LDFLAGS) -Wl,--subsystem,windows -o out/settings.exe $(APP_SETTINGS_OBJS) $(APP_LD_LIBS)
```

Then add `out/imageview.exe out/settings.exe` to `APP_USER_OBJS`, `APP_EXE_TARGETS`, `ISO_PAYLOAD_TARGETS`, `OUT_TARGETS`, and the `hd.img` / `hd_uefi.img` prerequisite lists, plus the two image-copy entries on the `mkfat32.py` lines:

```
--copy out/imageview.exe:/Monios/Apps/imageview.exe
--copy out/settings.exe:/Monios/Apps/settings.exe
```

(The generic rule `out/app_%.pe.o : user/apps/%.c` already compiles `imageview.c`/`settings.c` with no extra work.)

## 6. Verification performed

- `x86_64-w64-mingw32-gcc` (same flags as Makefile) compiles both apps to `.pe.o` with **0 errors**.
- Linked `out/imageview.exe` (49,661 B) and `out/settings.exe` (50,613 B) via `make` with the temp rules — **link clean**.
- `make out/kernel.exe` rebuilds with the new syscall; `out/syscall.o` compiled and `out/kernel.exe` (1,025,464 B) relinked.
- Host regression suites all exit 0: `test_vfs`, `test_net`, `test_fs`, `test_gui`, `test_net_proto`, `test_gfx_logic`, `test_wav`.
- Makefile restored to its prior state (no permanent edits left); temp patch scripts removed.

### Notes / limitations
- Apps run from a fixed ~3.75 MB user image region with no malloc; decoded pixel buffer capped at 1024×768 static BSS.
- PNG uses stored-block inflate only (Huffman-compressed PNGs show `[com]` and no pixels); JPG is header/info-only.
- Mouse hit-testing uses the same fill/sample primitives; on-screen drawing is block-based and may be slow for very large images.
