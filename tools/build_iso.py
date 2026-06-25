#!/usr/bin/env python3
"""Build a proper bootable ISO with Limine for MoniOS.

Usage:
  1. python tools/mkiso.py --fat12-only -o out/fat12.img
  2. cd out && limine.exe bios-install fat12.img [--force]
  3. python tools/build_iso.py

Or run tools/mkiso.py without --fat12-only and it will call build_iso.py automatically.
"""
import struct, sys, os

SECTOR = 2048

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
OUT_DIR = os.path.join(PROJECT_DIR, "out")

# Find the FAT12 image with Limine installed
for name in ("limine_fat12.img", "fat12.img"):
    path = os.path.join(OUT_DIR, name)
    if os.path.exists(path):
        fat_img_path = path
        break
else:
    print("ERROR: FAT12 image not found. Run:")
    print("  1. python tools/mkiso.py --fat12-only -o out/fat12.img")
    print("  2. cd out && limine.exe bios-install fat12.img")
    sys.exit(1)

with open(fat_img_path, "rb") as f:
    fat_img = f.read()

print(f"FAT12 image: {fat_img_path} ({len(fat_img)} bytes)")

# Boot image starts at sector 22 (after 21 sectors of header)
BOOT_IMG_LBA = 22

iso = bytearray()

# === Sectors 0-15: System Area (reserved, zeroed) ===
iso += b"\x00" * (16 * SECTOR)
assert len(iso) == 16 * SECTOR

def sector():
    """Return a fresh 2048-byte zeroed sector"""
    return bytearray(SECTOR)

def w32(ba, offset, value):
    """Write 32-bit little-endian value to bytearray at offset"""
    struct.pack_into("<I", ba, offset, value & 0xFFFFFFFF)

def w16(ba, offset, value):
    """Write 16-bit little-endian value to bytearray at offset"""
    struct.pack_into("<H", ba, offset, value & 0xFFFF)

# === Sector 16: Primary Volume Descriptor ===
pvd = sector()
pvd[0] = 0x01
pvd[1:6] = b"CD001"
pvd[6] = 0x01
pvd[8:8+32] = b"MONIOS" + b" " * 26          # system ID (32 bytes)
pvd[40:40+32] = b"MONIOS-ISO" + b" " * 22    # volume ID (32 bytes)
w32(pvd, 88, 1)                               # volume set size
w32(pvd, 92, 1)                               # volume seq number
w16(pvd, 96, SECTOR)                          # logical block size
w32(pvd, 100, 0)                              # path table size
w32(pvd, 108, 0)                              # type-L path table LBA
w32(pvd, 112, 0)                              # type-L opt path table
w32(pvd, 116, 0)                              # type-M path table LBA
w32(pvd, 120, 0)                              # type-M opt path table
ROOT_LBA = 20
w32(pvd, 124, ROOT_LBA)                       # root dir LBA
w32(pvd, 128, 34)                             # root dir length
pvd[132:180] = b" " * 48                      # volume set ID
pvd[264:280] = b"0" * 16                      # volume set ID (alternate)
pvd[280] = 0x01                               # structure version
assert len(pvd) == SECTOR
iso += bytes(pvd)
assert len(iso) == 17 * SECTOR, f"PVD bad: {len(iso)}"

# === Sector 17: Supplementary Volume Descriptor (Joliet) ===
svd = sector()
svd[0] = 0x02
svd[1:6] = b"CD001"
svd[6] = 0x01
assert len(svd) == SECTOR
iso += bytes(svd)
assert len(iso) == 18 * SECTOR

# === Sector 18: Boot Record Volume Descriptor (El Torito) ===
boot_rec = sector()
boot_rec[0] = 0x00
boot_rec[1:6] = b"CD001"
boot_rec[6] = 0x01
boot_rec[8:8+32] = b"CDROM EL TORITO SPECIFICATION" + b"\x00" * (32 - 29)
w32(boot_rec, 40, 21)                         # LBA of boot catalog
assert len(boot_rec) == SECTOR
iso += bytes(boot_rec)
assert len(iso) == 19 * SECTOR

# === Sector 19: Volume Descriptor Set Terminator ===
term = sector()
term[0] = 0xFF
term[1:6] = b"CD001"
term[6] = 0x01
assert len(term) == SECTOR
iso += bytes(term)
assert len(iso) == 20 * SECTOR

# === Sector 20: Root Directory ===
root = sector()
root[0] = 34                                   # directory record length
w32(root, 2, ROOT_LBA)                        # LBA of root
w32(root, 10, 34)                             # data size
root[18] = 0x02                               # directory flag
w32(root, 24, ROOT_LBA)                       # LBA again
root[32] = 1                                   # name length
root[33] = 0
assert len(root) == SECTOR
iso += bytes(root)
assert len(iso) == 21 * SECTOR, f"Root bad: {len(iso)}"

# === Sector 21: El Torito Boot Catalog ===
catalog = sector()

# Validation Entry (offset 0-31, big-endian)
catalog[0] = 0x01                              # header ID
catalog[1] = 0x00                              # platform: x86
struct.pack_into(">H", catalog, 2, 0)         # reserved (BE)
catalog[4:20] = b"CD-ROM EL TORITO"           # ID (16 bytes)
struct.pack_into(">H", catalog, 20, 0x55AA)  # key (BE)
struct.pack_into(">H", catalog, 22, 1)        # version (BE)

# Initial/Default Entry (offset 32-63)
ie = 32
catalog[ie] = 0x88                             # bootable
catalog[ie + 1] = 0x00                         # media type: 1.44MB floppy emulation
struct.pack_into("<H", catalog, ie + 2, 0x0000)  # load segment
catalog[ie + 4] = 0x00                         # system type (0 = auto)
catalog[ie + 5] = 0x00                         # reserved
struct.pack_into("<H", catalog, ie + 6, 2880)  # sector count: 2880 (1.44MB floppy)
w32(catalog, ie + 8, BOOT_IMG_LBA)            # LBA of boot image
struct.pack_into("<H", catalog, ie + 0x3E, 0xAA55)  # magic

assert len(catalog) == SECTOR
iso += bytes(catalog)
assert len(iso) == 22 * SECTOR, f"Catalog bad: {len(iso)}"

# === Sector 22+: FAT12 Boot Image ===
iso += fat_img
while len(iso) % SECTOR:
    iso += b"\x00"

# === Write ISO ===
iso_path = os.path.join(OUT_DIR, "monios_limine.iso")
with open(iso_path, "wb") as f:
    f.write(iso)

print(f"ISO: {len(iso)} bytes = {len(iso)//SECTOR} sectors")
print(f"Boot catalog: sector 21 | Boot image (FAT12+Limine): sector {BOOT_IMG_LBA}")
print(f"Written: {iso_path}")
