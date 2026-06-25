#!/usr/bin/env python3
"""
MoniOS ISO 镜像生成工具
用法: python tools/mkiso.py [--output hd.iso] [--limine-dir <path>]
要求: --limine-dir 必须指向已下载的 Limine 二进制目录
      下载地址: https://github.com/limine-bootloader/limine
      或使用 --no-limine 仅生成 BIOS-only ISO
"""

import struct
import sys
import os
import stat
import argparse
import hashlib

SECTOR_SIZE = 2048
ISO_BLOCK_SIZE = 2048
FAT12_BLOCK_SIZE = 512

VD_OFFSET = 0x40
VD_SEQUENCE_EXTENT = 0
VD_DATA_TYPE = 1
VD_DATA_TYPE_DESC = b"\x01"
VD_VERSION = 1
VD_FLAGS = 0
VD_SYSTEM_ID = b"MONIOS    "
VD_VOLUME_ID = b"MONIOS-ISO"
VD_VOLUME_SPACE_SIZE = 0
VD_SET_SIZE = 1
VD_SEQ_NUM = 1
VD_LOGICAL_BLOCK_SIZE = struct.pack("<H", ISO_BLOCK_SIZE)
VD_EXPANSION = 0
VOLUME_SET_SIZE = struct.pack("<H", 1)
VOLUME_SEQ_NUM = struct.pack("<H", 1)
VOL_LOGICAL_BLOCK_SIZE = struct.pack("<H", ISO_BLOCK_SIZE)


def iso9660_datetime():
    """返回 ISO9660 时间戳格式 (17字节)"""
    import datetime
    now = datetime.datetime.now()
    return now.strftime("%Y%m%d%H%M%S")[:16].encode("ascii") + b"\x00"


def pack_le16_le32(value):
    """打包 little-endian 16+32"""
    return struct.pack("<HI", value & 0xFFFF, value >> 16)


def build_pvd():
    """构建 Primary Volume Descriptor"""
    vd_type = 0
    vd_id = b"CD001"
    vd_version = 1
    data = b"\x00" * 1
    data += vd_id
    data += struct.pack("B", vd_version)
    data += b"\x00"
    data += VD_SYSTEM_ID
    data += VD_VOLUME_ID.ljust(32, b"\x20")
    data += b"\x00" * 8
    data += pack_le16_le32(1)  # volume space size
    data += VD_LOGICAL_BLOCK_SIZE
    data += b"\x00" * 8
    data += VOLUME_SET_SIZE
    data += VOLUME_SEQ_NUM
    data += VOL_LOGICAL_BLOCK_SIZE
    data += struct.pack("<I", 88)
    data += iso9660_datetime()
    data += (b"0" * 16)
    data += iso9660_datetime()
    data += (b"0" * 16)
    data += b"\x01"
    data += b"\x00" * 32
    data += b"\x00" * 32
    data += b"\x00" * 1552
    return data


def build_svd():
    """Build Supplementary Volume Descriptor (for Joliet)"""
    vd_type = 1
    vd_id = b"CD001"
    vd_version = 1
    data = b"\x00" * 1
    data += vd_id
    data += struct.pack("B", vd_version)
    data += b"\x00"
    data += VD_SYSTEM_ID
    data += VD_VOLUME_ID.ljust(32, b"\x20")
    data += b"\x00" * 8
    data += pack_le16_le32(1)
    data += VD_LOGICAL_BLOCK_SIZE
    data += b"\x00" * 8
    data += VOLUME_SET_SIZE
    data += VOLUME_SEQ_NUM
    data += VOL_LOGICAL_BLOCK_SIZE
    data += struct.pack("<I", 88)
    data += iso9660_datetime()
    data += (b"0" * 16)
    data += iso9660_datetime()
    data += (b"0" * 16)
    data += b"\x01"
    data += b"\x00" * 32
    data += b"\x00" * 32
    # Joliet uses UCS-2BE (2-byte unicode) for these fields
    joliet_sys = "MONIOS".encode("utf-16-be").ljust(32, b"\x00")
    joliet_vol = "MONIOS-ISO".encode("utf-16-be").ljust(32, b"\x00")
    data += joliet_sys
    data += joliet_vol
    data += b"\x00" * 64
    data += b"\x00" * 1552
    return data


def build_boot_record(boot_catalog_sector):
    """Build Boot Record Volume Descriptor (El Torito)"""
    data = b"\x00" * 1
    data += b"CD001"
    data += struct.pack("B", 1)
    data += b"\x00"
    data += b"CDROM EL TORITO SPECIFICATION"
    data += b"\x00" * 32
    data += struct.pack("<I", boot_catalog_sector)
    data += b"\x00" * 1729
    return data


def build_eltorito_entry(image_path_relative, media_type=0x01, load_segment=0x00):
    """Build a single El Torito boot entry.
    media_type: 0x01 = no emulation (fd 1.44), 0x00 = fd 1.2M, 0x02 = fd 720K, 0x03 = fd 1.44
    """
    data = struct.pack("B", media_type)          # boot indicator
    data += struct.pack("B", 0x00)                # media type
    data += struct.pack("<H", 0x01)               # number of 512-byte sectors to load
    data += struct.pack("<H", load_segment)        # load segment (0x07C0 for floppy)
    data += struct.pack("B", 0x00)                 # system type (0=floppy, 1=hard disk)
    data += b"\x00"                               # reserved
    data += struct.pack("B", 0x55)                 # sector count (0 = auto)
    data += struct.pack("B", 0xAA)                 # magic
    data += image_path_relative.encode("ascii").ljust(32, b"\x00")
    return data


def build_boot_catalog(isohybrid_entry_path):
    """Build the El Torito boot catalog.
    Returns (boot_catalog_sector, catalog_data)
    """
    # Validation entry (mandatory first entry)
    header_entry = struct.pack("B", 0x01)         # header ID
    header_entry += b"\x00" * 1                    # platform (0=x86)
    header_entry += struct.pack("<H", 0x00)       # reserved
    header_entry += b"\x43\x44\x2D\x52\x4F\x4D\x20\x45\x4C\x20\x54\x4F\x52\x49\x54\x4F"  # "CD-ROM EL TORITO"
    header_entry += b"\x20\x53\x50\x45\x43\x49\x46\x49\x43\x41\x54\x49\x4F\x4E"
    header_entry += struct.pack("<I", 0x01)       # number of entries
    header_entry += b"\x20" * 32                  # id string

    # Bootable entry
    boot_entry = build_eltorito_entry(isohybrid_entry_path, media_type=0x01, load_segment=0x7C0)
    # padding to 32 bytes
    boot_entry = boot_entry.ljust(32, b"\x00")

    # Section header (for additional entries if any)
    section_header = struct.pack("B", 0x90)       # section header (no extension)
    section_header += b"\x00" * 1
    section_header += struct.pack("<H", 0x00)
    section_header += b"\x00" * 28

    catalog_data = header_entry + boot_entry + section_header
    catalog_data = catalog_data.ljust(SECTOR_SIZE, b"\x00")
    return catalog_data


def align(data, size=SECTOR_SIZE):
    """Pad data to sector boundary"""
    if len(data) % size:
        data += b"\x00" * (size - len(data) % size)
    return data


def build_iso9660_path_table(parent_id, dir_id, filename=b""):
    """Build a directory record for the Path Table"""
    name_len = len(filename)
    record = struct.pack("B", name_len)
    record += struct.pack("<B", 0)               # extended attribute len
    record += struct.pack("<I", 0)                # location (LE)
    record += struct.pack("<H", 0)                # parent dir (LE)
    record += name_len.to_bytes(1, "little")
    record += filename
    record = record.ljust(10 + name_len + (name_len % 2), b"\x00")
    return record


def iso_write_dword_le(data, offset, value):
    """Write a 32-bit LE dword into a mutable bytearray at offset"""
    struct.pack_into("<I", data, offset, value)


def build_hybrid_mbr():
    """Build a BIOS/Limine isohybrid MBR"""
    mbr = bytearray(512)
    # MBR partition table (4 entries × 16 bytes)
    # Entry 0: FAT12 partition (or ISO9660)
    mbr[0x80] = 0x80                          # bootable flag
    mbr[0x81] = 0x01                          # start head
    mbr[0x82] = 0x01                          # start sector (bits 0-5), high bits of cylinder
    mbr[0x83] = 0x00                          # start cylinder
    mbr[0x84] = 0x01                          # partition type: FAT12
    mbr[0x85] = 0x01                          # end head
    mbr[0x86] = 0x12                          # end sector + high cylinder bits
    mbr[0x87] = 0x09                          # end cylinder
    # LBA of partition (sector 0 = boot sector, but for isohybrid we use LBA of ISO start)
    struct.pack_into("<I", mbr, 0x88, 0x00)  # will be updated later
    struct.pack_into("<I", mbr, 0x8C, 0x10)  # num sectors (placeholder: 16 sectors for FAT12)

    # Actually, for a proper isohybrid, let's put Limine boot code here
    # Signature
    struct.pack_into("<H", mbr, 0x1FE, 0x55AA)
    return bytes(mbr)


def build_fat12_boot_sector():
    """Build a FAT12 boot sector + FAT + root dir + kernel data"""
    boot = bytearray(512)

    # Jump instruction
    boot[0:3] = b"\xEB\xFE\x90"

    # OEM name
    boot[0x03:0x0B] = b"MONIOS   "

    # Bytes per sector (512)
    struct.pack_into("<H", boot, 0x0B, 512)

    # Sectors per cluster (1)
    boot[0x0D] = 1

    # Reserved sectors (1 for boot sector)
    struct.pack_into("<H", boot, 0x0E, 1)

    # Number of FATs (2)
    boot[0x10] = 2

    # Root entries (224 for FAT12)
    struct.pack_into("<H", boot, 0x11, 224)

    # Total sectors (2880 for 1.44M floppy)
    struct.pack_into("<H", boot, 0x13, 0)

    # Media descriptor (0xF0)
    boot[0x15] = 0xF0

    # Sectors per FAT (9)
    struct.pack_into("<H", boot, 0x16, 9)

    # Sectors per track (18)
    struct.pack_into("<H", boot, 0x18, 18)

    # Number of heads (2)
    struct.pack_into("<H", boot, 0x1A, 2)

    # Hidden sectors (0)
    struct.pack_into("<I", boot, 0x1C, 0)

    # Total sectors large
    struct.pack_into("<I", boot, 0x20, 2880)

    # Drive number (0x80 for HDD)
    boot[0x24] = 0x80

    boot[0x26] = 0x29  # extended boot signature

    # Volume serial
    struct.pack_into("<I", boot, 0x27, 0x12345678)

    # Volume label
    boot[0x2B:0x36] = b"MONIOS    "

    # File system type
    boot[0x36:0x3E] = b"FAT12   "

    # Boot code
    boot[0x3E:0x1FE] = b"\x90" * (0x1FE - 0x3E)

    # Boot signature
    struct.pack_into("<H", boot, 0x1FE, 0xAA55)

    return bytes(boot)


def build_fat12_image(kernel_bin_data, loader_bin_data):
    """Build a complete FAT12 floppy image with kernel and loader"""
    FAT12_SIZE = 2880 * 512  # 1.44MB
    image = bytearray(FAT12_SIZE)

    # Reserved sector (boot sector) at offset 0
    fat_boot = build_fat12_boot_sector()
    image[0:512] = fat_boot

    # FAT table 1 starts at sector 1 (offset 512), 9 sectors = 4608 bytes
    fat_offset = 512
    fat1 = bytearray(4608)
    fat1[0] = 0xF0
    fat1[1] = 0xFF
    fat1[2] = 0xFF
    image[fat_offset:fat_offset + 4608] = fat1

    # FAT table 2 starts at sector 10 (offset 5120)
    fat2_offset = 5120
    image[fat2_offset:fat2_offset + 4608] = fat1

    # Root directory at sector 19 (offset 19*512 = 9728), 14 sectors = 7168 bytes
    root_offset = 9728
    root = bytearray(7168)

    # Root directory entry for LOADER  BIN
    root[0:11] = b"LOADER  BIN"
    root[0x0B] = 0x20    # attributes: archive
    root[0x0C:0x15] = b"\x00" * 9  # reserved
    # Cluster high (0)
    struct.pack_into("<H", root, 0x14, 0)
    # Cluster low (2) - first cluster of data area
    struct.pack_into("<H", root, 0x1A, 2)
    # File size
    struct.pack_into("<I", root, 0x1C, len(loader_bin_data))

    # Root directory entry for KERNEL  BIN
    entry2_offset = 32
    root[entry2_offset:entry2_offset + 11] = b"KERNEL  BIN"
    root[entry2_offset + 0x0B] = 0x20
    struct.pack_into("<H", root, entry2_offset + 0x14, 0)
    struct.pack_into("<H", root, entry2_offset + 0x1A, 3)  # cluster 3
    struct.pack_into("<I", root, entry2_offset + 0x1C, len(kernel_bin_data))

    image[root_offset:root_offset + 7168] = root

    # Data area starts at sector 33 (offset 33*512 = 16896)
    data_offset = 16896

    # Cluster 2: loader.bin
    image[data_offset:data_offset + len(loader_bin_data)] = loader_bin_data
    if len(loader_bin_data) % 512:
        pad = 512 - len(loader_bin_data) % 512
        image[data_offset + len(loader_bin_data):data_offset + len(loader_bin_data) + pad] = b"\x00" * pad

    # Cluster 3: kernel.bin
    kern_offset = data_offset + 512
    image[kern_offset:kern_offset + len(kernel_bin_data)] = kernel_bin_data

    return bytes(image)


def download_limine(tag="v9.4.2"):
    """Download Limine binaries from GitHub releases"""
    import urllib.request
    import zipfile
    import tempfile

    base_url = f"https://github.com/limine-bootloader/limine/releases/download/{tag}/"
    files = [
        "limine-bin-x86_64.zip",
    ]

    tmpdir = tempfile.mkdtemp(prefix="limine_")
    for fname in files:
        url = base_url + fname
        dest = os.path.join(tmpdir, fname)
        print(f"Downloading {url} ...")
        try:
            urllib.request.urlretrieve(url, dest)
        except Exception as e:
            print(f"  Failed: {e}")
            raise

    # Extract zip
    limine_dir = os.path.join(tmpdir, "limine")
    os.makedirs(limine_dir, exist_ok=True)
    with zipfile.ZipFile(os.path.join(tmpdir, fname), "r") as z:
        z.extractall(limine_dir)

    # List files
    for root, dirs, files_list in os.walk(limine_dir):
        for f in files_list:
            print(f"  {os.path.join(root, f)}")

    return limine_dir


def ensure_limine(limine_dir_arg):
    """Find or download Limine binaries"""
    default_dirs = [
        "C:\\limine",
        "D:\\limine",
        "C:\\Program Files\\Limine",
        os.path.expanduser("~\\Downloads\\limine"),
    ]
    if limine_dir_arg:
        if os.path.isdir(limine_dir_arg):
            return limine_dir_arg
        print(f"Limine dir not found: {limine_dir_arg}, downloading...")
    else:
        for d in default_dirs:
            if os.path.isdir(d):
                return d
        print("Limine not found in default locations, downloading...")

    return download_limine()


def build_iso_with_limine(project_dir, output_iso, limine_dir, loader_bin, kernel_bin):
    """Build ISO using Limine bootloader"""
    import shutil

    # Create a staging directory for ISO contents
    staging = os.path.join(project_dir, "out", "iso_staging")
    efi_staging = os.path.join(staging, "EFI", "BOOT")
    os.makedirs(efi_staging, exist_ok=True)

    # Find Limine binaries
    limine_sys = os.path.join(limine_dir, "limine.sys")
    limine_efi = None
    for root, _, files in os.walk(limine_dir):
        for f in files:
            if "limine" in f.lower() and f.endswith(".efi"):
                limine_efi = os.path.join(root, f)
                break

    if not os.path.exists(limine_sys):
        print(f"ERROR: limine.sys not found in {limine_dir}")
        print("Available files:", os.listdir(limine_dir))
        return False

    # Copy limine binaries
    shutil.copy(limine_sys, staging)

    # Create limine.conf
    limine_conf = f"""; MoniOS Limine Configuration
timeout: 5
default_entry: 0

:MoniOS (BIOS+UEFI)
    fallback: 1
    protocol: linux
    kernel_path: /boot/{os.path.basename(kernel_bin)}
    cmdline: root=/dev/ram0

:MoniOS (Debug)
    fallback: 1
    protocol: linux
    kernel_path: /boot/{os.path.basename(kernel_bin)}
    cmdline: root=/dev/ram0 debug
"""
    with open(os.path.join(staging, "limine.conf"), "w") as f:
        f.write(limine_conf)

    # Create /boot directory
    boot_dir = os.path.join(staging, "boot")
    os.makedirs(boot_dir, exist_ok=True)
    shutil.copy(loader_bin, os.path.join(boot_dir, os.path.basename(loader_bin)))
    shutil.copy(kernel_bin, os.path.join(boot_dir, os.path.basename(kernel_bin)))

    # Create EFI directory
    if limine_efi:
        efi_dest = os.path.join(efi_staging, "BOOTX64.EFI")
        shutil.copy(limine_efi, efi_dest)
        # Also copy limine.conf to EFI directory
        shutil.copy(os.path.join(staging, "limine.conf"), efi_staging)

    # Use xorriso if available, otherwise build manually
    xorriso_path = None
    for p in ["C:\\Program Files\\xorriso\\xorriso.bat", "xorriso", "C:\\msys64\\usr\\bin\\xorriso.exe",
              "C:\\Program Files (x86)\\xorriso\\xorriso.exe"]:
        if os.path.exists(p):
            xorriso_path = p
            break

    if xorriso_path:
        print(f"Using xorriso: {xorriso_path}")
        import subprocess
        cmd = [
            xorriso_path,
            "-as", "mkisofs",
            "-b", "limine.sys",
            "-no-emul-boot",
            "-boot-load-seg", "0x7C0",
            "-boot-info-table",
            "--efi-boot", "EFI/BOOT/BOOTX64.EFI",
            "--efi-boot-image",
            "-o", output_iso,
            staging
        ]
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            print("xorriso failed:", result.stderr)
            return False
        print(f"ISO created: {output_iso}")
        return True

    # Fallback: build ISO manually
    print("Building ISO manually (no xorriso)...")
    build_manual_iso(staging, output_iso, kernel_bin, loader_bin)
    return True


def build_manual_iso(staging_dir, output_iso, kernel_bin, loader_bin):
    """Build ISO manually without external tools (BIOS only)"""
    # Build the El Torito boot catalog
    catalog_sector = 17  # We'll put catalog at sector 17
    catalog = build_boot_catalog("limine.sys")

    # Build ISO9660 structure
    iso_data = bytearray()

    # Sector 0: Boot sector placeholder (will be replaced by limine.sys boot code)
    boot_sector = bytearray(SECTOR_SIZE)
    # Put a simple isohybrid MBR here
    mbr_code = build_hybrid_mbr()
    boot_sector[0:len(mbr_code)] = mbr_code
    iso_data += bytes(boot_sector)

    # Sectors 1-16: unused or reserved
    iso_data += b"\x00" * (16 * SECTOR_SIZE)

    # Sector 17: Boot catalog
    iso_data += catalog

    # Now build the ISO9660 filesystem
    # We need: PVD, SVD, Boot Record, Root directory entry, etc.
    # For simplicity, we'll use the limine.sys as the boot image

    # Pad to start of volume descriptors
    while len(iso_data) < 16 * SECTOR_SIZE:
        iso_data += b"\x00"
    while len(iso_data) < 17 * SECTOR_SIZE:
        iso_data += b"\x00"

    # Volume descriptors
    pvd_data = build_pvd()
    svd_data = build_svd()
    boot_rec_data = build_boot_record(catalog_sector)

    # Terminator
    term = b"\xFF" + b"CD001" + struct.pack("B", 1) + b"\x00" * 2046

    iso_data += pvd_data
    iso_data += svd_data
    iso_data += boot_rec_data
    iso_data += term

    # Pad to next sector after volume descriptors
    while len(iso_data) % SECTOR_SIZE:
        iso_data += b"\x00"

    # Now add the boot file (limine.sys) + boot directory
    # For El Torito with no-emul-boot, the boot file must be contiguous
    limine_sys_path = os.path.join(staging_dir, "limine.sys")
    if os.path.exists(limine_sys_path):
        with open(limine_sys_path, "rb") as f:
            limine_data = f.read()
    else:
        # Create a placeholder
        limine_data = b"\x00" * 512

    # Boot file extent (will be right after volume descriptors)
    boot_file_start = len(iso_data)

    iso_data += limine_data
    # Pad to SECTOR_SIZE
    while len(iso_data) % SECTOR_SIZE:
        iso_data += b"\x00"

    # Boot directory entry (directory record for the file we just added)
    # We need a proper directory structure...
    # For now, add the staging files to ISO
    file_extent = len(iso_data)
    kernel_bin_data = b""
    loader_bin_data = b""
    if os.path.exists(kernel_bin):
        with open(kernel_bin, "rb") as f:
            kernel_bin_data = f.read()
    if os.path.exists(loader_bin):
        with open(loader_bin, "rb") as f:
            loader_bin_data = f.read()

    # Put all files at the end of the ISO (for simplicity)
    # In a proper ISO we'd put them in a /boot directory
    # For now, just put them after the boot image

    if kernel_bin_data:
        iso_data += kernel_bin_data
        while len(iso_data) % SECTOR_SIZE:
            iso_data += b"\x00"

    if loader_bin_data:
        iso_data += loader_bin_data
        while len(iso_data) % SECTOR_SIZE:
            iso_data += b"\x00"

    # Write the ISO
    with open(output_iso, "wb") as f:
        f.write(iso_data)

    print(f"ISO created manually: {output_iso} ({len(iso_data)} bytes)")


def build_uefi_only_iso(project_dir, output_iso, limine_dir):
    """Build a pure UEFI ISO (no BIOS) using Limine"""
    efi_staging = os.path.join(project_dir, "out", "efi_iso_staging")
    efi_boot = os.path.join(efi_staging, "EFI", "BOOT")
    os.makedirs(efi_boot, exist_ok=True)

    # Find Limine EFI binary
    limine_efi = None
    for root, _, files in os.walk(limine_dir):
        for f in files:
            if "BOOTX64.EFI" in f.upper() or ("limine" in f.lower() and f.endswith(".EFI")):
                limine_efi = os.path.join(root, f)
                break

    if not limine_efi:
        print("ERROR: BOOTX64.EFI not found in Limine directory")
        return False

    import shutil
    shutil.copy(limine_efi, os.path.join(efi_boot, "BOOTX64.EFI"))

    # Create limine.conf
    limine_conf = """; MoniOS UEFI-only Configuration
timeout: 5

:MoniOS
    protocol: linux
    kernel_path: /boot/kernel.bin
    cmdline: root=/dev/ram0
"""
    with open(os.path.join(efi_staging, "limine.conf"), "w") as f:
        f.write(limine_conf)

    # Copy kernel
    kernel_bin = os.path.join(project_dir, "out", "kernel.bin")
    boot_dir = os.path.join(efi_staging, "boot")
    os.makedirs(boot_dir, exist_ok=True)
    if os.path.exists(kernel_bin):
        import shutil
        shutil.copy(kernel_bin, os.path.join(boot_dir, "kernel.bin"))

    # Try using xorriso for UEFI-only
    xorriso_path = None
    for p in ["xorriso", "C:\\msys64\\usr\\bin\\xorriso.exe",
              "C:\\Program Files (x86)\\xorriso\\xorriso.exe"]:
        try:
            import subprocess
            r = subprocess.run([p, "--version"], capture_output=True, text=True)
            if r.returncode == 0:
                xorriso_path = p
                break
        except:
            pass

    if xorriso_path:
        import subprocess
        cmd = [
            xorriso_path, "-as", "mkisofs",
            "-efi-boot", "EFI/BOOT/BOOTX64.EFI",
            "-o", output_iso,
            efi_staging
        ]
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode == 0:
            print(f"UEFI ISO created: {output_iso}")
            return True
        print("xorriso failed:", result.stderr)

    # Fallback: build UEFI ISO manually
    print("Building UEFI ISO manually...")
    build_uefi_iso_manual(efi_staging, output_iso)
    return True


def build_uefi_iso_manual(efi_staging, output_iso):
    """Build a minimal UEFI ISO with FAT filesystem"""
    # Create a FAT12 image with the EFI boot files
    fat_img = build_fat12_image(b"", b"")

    # For UEFI, we need an ISO with:
    # 1. ISO9660 PVD
    # 2. EFI System Partition (FAT12/16/32) in El Torito
    # This is complex - let's create a simple El Torito ISO with the FAT image

    iso = bytearray()

    # Sector 0: blank (UEFI doesn't use BIOS)
    iso += b"\x00" * SECTOR_SIZE

    # Sectors 1-16: reserved
    iso += b"\x00" * (16 * SECTOR_SIZE)

    # Volume descriptors
    pvd = build_pvd()
    svd = build_svd()
    iso += pvd
    iso += svd

    # Terminator
    iso += b"\xFF" + b"CD001" + struct.pack("B", 1) + b"\x00" * 2046
    while len(iso) % SECTOR_SIZE:
        iso += b"\x00"

    # Add EFI boot files
    efi_boot_path = os.path.join(efi_staging, "EFI", "BOOT", "BOOTX64.EFI")
    if os.path.exists(efi_boot_path):
        with open(efi_boot_path, "rb") as f:
            efi_data = f.read()
        iso += efi_data
        while len(iso) % SECTOR_SIZE:
            iso += b"\x00"

    # Add limine.conf and kernel if present
    conf_path = os.path.join(efi_staging, "limine.conf")
    if os.path.exists(conf_path):
        with open(conf_path, "rb") as f:
            conf_data = f.read()
        iso += conf_data
        while len(iso) % SECTOR_SIZE:
            iso += b"\x00"

    kernel_path = os.path.join(efi_staging, "boot", "kernel.bin")
    if os.path.exists(kernel_path):
        with open(kernel_path, "rb") as f:
            kern_data = f.read()
        iso += kern_data
        while len(iso) % SECTOR_SIZE:
            iso += b"\x00"

    with open(output_iso, "wb") as f:
        f.write(iso)

    print(f"UEFI ISO created: {output_iso} ({len(iso)} bytes)")


def main():
    parser = argparse.ArgumentParser(description="MoniOS ISO Generator")
    parser.add_argument("--output", "-o", default="out/monios.iso",
                        help="Output ISO file path")
    parser.add_argument("--limine-dir", "-l",
                        help="Path to Limine binaries (will download if not provided)")
    parser.add_argument("--project-dir", "-p", default=".",
                        help="Project root directory")
    parser.add_argument("--uefi-only", action="store_true",
                        help="Build UEFI-only ISO (no BIOS boot)")
    parser.add_argument("--fat12-only", action="store_true",
                        help="Output as FAT12 floppy image (.img) instead of ISO")
    parser.add_argument("--download-limine", action="store_true",
                        help="Force download Limine binaries")
    args = parser.parse_args()

    project_dir = os.path.abspath(args.project_dir)
    output_iso = os.path.abspath(args.output)

    # Ensure output dir
    os.makedirs(os.path.dirname(output_iso), exist_ok=True)

    # Kernel and loader paths
    kernel_bin = os.path.join(project_dir, "out", "kernel.bin")
    loader_bin = os.path.join(project_dir, "out", "loader.bin")
    # Check for required files
    missing = []
    if not os.path.exists(kernel_bin):
        missing.append("kernel.bin")
    if not os.path.exists(loader_bin):
        missing.append("loader.bin")

    if missing:
        print(f"WARNING: Missing build artifacts: {', '.join(missing)}")
        print("Run 'make' first to build the kernel.")
        print()

    # Handle FAT12 floppy image output
    if args.fat12_only:
        kern_data = b""
        loader_data = b""
        if os.path.exists(kernel_bin):
            with open(kernel_bin, "rb") as f:
                kern_data = f.read()
        if os.path.exists(loader_bin):
            with open(loader_bin, "rb") as f:
                loader_data = f.read()
        fat_img = build_fat12_image(kern_data, loader_data)
        img_path = output_iso.replace(".iso", ".img")
        with open(img_path, "wb") as f:
            f.write(fat_img)
        print(f"FAT12 floppy image created: {img_path} ({len(fat_img)} bytes)")
        return

    # Get Limine
    try:
        limine_dir = ensure_limine(args.limine_dir)
    except Exception as e:
        print(f"Could not get Limine: {e}")
        print("Falling back to FAT12-only ISO...")
        limine_dir = None

    if args.uefi_only:
        if limine_dir:
            build_uefi_only_iso(project_dir, output_iso, limine_dir)
        else:
            print("Limine required for UEFI boot. Use --fat12-only for basic image.")
        return

    # Standard ISO (BIOS + UEFI with Limine)
    if limine_dir:
        print(f"Building ISO with Limine from: {limine_dir}")
        success = build_iso_with_limine(
            project_dir, output_iso, limine_dir,
            loader_bin if os.path.exists(loader_bin) else None,
            kernel_bin if os.path.exists(kernel_bin) else None
        )
        if success:
            return
        print("Limine build failed, falling back to manual ISO...")

    # Manual ISO without Limine (basic BIOS-only)
    print("Building basic ISO (no bootloader)...")
    staging = os.path.join(project_dir, "out", "iso_staging_basic")
    os.makedirs(staging, exist_ok=True)
    build_manual_iso(staging, output_iso,
                     kernel_bin if os.path.exists(kernel_bin) else "",
                     loader_bin if os.path.exists(loader_bin) else "")


if __name__ == "__main__":
    main()
