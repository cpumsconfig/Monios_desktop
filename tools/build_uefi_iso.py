#!/usr/bin/env python3
from __future__ import annotations

import argparse
import shutil
import struct
import sys
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mkfat32 import Fat32Image, TOTAL_SECTORS  # noqa: E402


PROJECT_DIR = Path(__file__).resolve().parent.parent
SECTOR = 2048


@dataclass
class IsoFile:
    name: bytes
    data: bytes
    lba: int = 0


def write_both16(buf: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<H", buf, offset, value & 0xFFFF)
    struct.pack_into(">H", buf, offset + 2, value & 0xFFFF)


def write_both32(buf: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<I", buf, offset, value & 0xFFFFFFFF)
    struct.pack_into(">I", buf, offset + 4, value & 0xFFFFFFFF)


def iso_datetime_7() -> bytes:
    now = datetime.now()
    return bytes([now.year - 1900, now.month, now.day, now.hour, now.minute, now.second, 0])


def iso_datetime_17() -> bytes:
    now = datetime.now()
    return now.strftime("%Y%m%d%H%M%S00").encode("ascii") + b"\x00"


def dir_record(lba: int, size: int, flags: int, name: bytes) -> bytes:
    length = 33 + len(name)
    if length & 1:
        length += 1
    rec = bytearray(length)
    rec[0] = length
    rec[1] = 0
    write_both32(rec, 2, lba)
    write_both32(rec, 10, size)
    rec[18:25] = iso_datetime_7()
    rec[25] = flags
    rec[26] = 0
    rec[27] = 0
    write_both16(rec, 28, 1)
    rec[32] = len(name)
    rec[33:33 + len(name)] = name
    return bytes(rec)


def path_table(root_lba: int, big_endian: bool) -> bytes:
    table = bytearray()
    table += b"\x01\x00"
    if big_endian:
        table += struct.pack(">I", root_lba)
        table += struct.pack(">H", 1)
    else:
        table += struct.pack("<I", root_lba)
        table += struct.pack("<H", 1)
    table += b"\x00\x00"
    return bytes(table)


def build_pvd(total_sectors: int, root_lba: int, root_size: int, path_table_size: int,
              path_l_lba: int, path_m_lba: int) -> bytes:
    pvd = bytearray(SECTOR)
    pvd[0] = 1
    pvd[1:6] = b"CD001"
    pvd[6] = 1
    pvd[8:40] = b"MONIOS".ljust(32, b" ")
    pvd[40:72] = b"MONIOS_UEFI_SETUP".ljust(32, b" ")
    write_both32(pvd, 80, total_sectors)
    write_both16(pvd, 120, 1)
    write_both16(pvd, 124, 1)
    write_both16(pvd, 128, SECTOR)
    write_both32(pvd, 132, path_table_size)
    struct.pack_into("<I", pvd, 140, path_l_lba)
    struct.pack_into("<I", pvd, 144, 0)
    struct.pack_into(">I", pvd, 148, path_m_lba)
    struct.pack_into(">I", pvd, 152, 0)
    root = dir_record(root_lba, root_size, 0x02, b"\x00")
    pvd[156:156 + len(root)] = root
    pvd[190:318] = b"MONIOS".ljust(128, b" ")
    pvd[318:446] = b"MONIOS".ljust(128, b" ")
    pvd[446:574] = b"MONIOS".ljust(128, b" ")
    pvd[574:702] = b"MONIOS".ljust(128, b" ")
    pvd[813:830] = iso_datetime_17()
    pvd[830:847] = iso_datetime_17()
    pvd[847:864] = b"0" * 16 + b"\x00"
    pvd[864:881] = b"0" * 16 + b"\x00"
    pvd[881] = 1
    return bytes(pvd)


def build_boot_record(catalog_lba: int) -> bytes:
    rec = bytearray(SECTOR)
    rec[0] = 0
    rec[1:6] = b"CD001"
    rec[6] = 1
    rec[7:39] = b"EL TORITO SPECIFICATION".ljust(32, b"\x00")
    struct.pack_into("<I", rec, 71, catalog_lba)
    return bytes(rec)


def build_terminator() -> bytes:
    term = bytearray(SECTOR)
    term[0] = 0xFF
    term[1:6] = b"CD001"
    term[6] = 1
    return bytes(term)


def build_boot_catalog(esp_lba: int, esp_size: int) -> bytes:
    catalog = bytearray(SECTOR)
    validation = bytearray(32)
    validation[0] = 1
    validation[1] = 0xEF
    validation[4:28] = b"MONIOS UEFI".ljust(24, b"\x00")
    validation[30] = 0x55
    validation[31] = 0xAA
    checksum = (-sum(struct.unpack("<16H", validation))) & 0xFFFF
    struct.pack_into("<H", validation, 28, checksum)
    catalog[0:32] = validation

    entry = bytearray(32)
    entry[0] = 0x88
    entry[1] = 0
    struct.pack_into("<H", entry, 2, 0)
    entry[4] = 0
    sector_count = (esp_size + 511) // 512
    if sector_count > 0xFFFF:
        sector_count = 0xFFFF
    struct.pack_into("<H", entry, 6, sector_count)
    struct.pack_into("<I", entry, 8, esp_lba)
    catalog[32:64] = entry
    return bytes(catalog)


def pad_sector(data: bytes) -> bytes:
    if len(data) % SECTOR == 0:
        return data
    return data + b"\x00" * (SECTOR - (len(data) % SECTOR))


def fat32_boot_sector() -> bytes:
    boot = bytearray(512)
    boot[0:3] = b"\xEB\x58\x90"
    boot[3:11] = b"MONIOS  "
    struct.pack_into("<H", boot, 11, 512)
    boot[13] = 1
    struct.pack_into("<H", boot, 14, 32)
    boot[16] = 2
    struct.pack_into("<H", boot, 17, 0)
    struct.pack_into("<H", boot, 19, 0)
    boot[21] = 0xF8
    struct.pack_into("<H", boot, 22, 0)
    struct.pack_into("<H", boot, 24, 63)
    struct.pack_into("<H", boot, 26, 255)
    struct.pack_into("<I", boot, 28, 0)
    struct.pack_into("<I", boot, 32, TOTAL_SECTORS)
    struct.pack_into("<I", boot, 36, 1576)
    struct.pack_into("<H", boot, 40, 0)
    struct.pack_into("<H", boot, 42, 0)
    struct.pack_into("<I", boot, 44, 2)
    struct.pack_into("<H", boot, 48, 1)
    struct.pack_into("<H", boot, 50, 6)
    boot[64] = 0x80
    boot[66] = 0x29
    struct.pack_into("<I", boot, 67, 0x4D4F4E49)
    boot[71:82] = b"MONIOS ESP "
    boot[82:90] = b"FAT32   "
    struct.pack_into("<H", boot, 510, 0xAA55)
    return bytes(boot)


def build_esp_image(
    esp_path: Path,
    staging: Path,
    efi_path: Path,
    kernel_path: Path,
    setup_path: Path | None,
    font_path: Path | None,
    password_path: Path | None,
) -> bytes:
    if esp_path.exists():
        esp_path.unlink()
    image = Fat32Image(esp_path, fat32_boot_sector())
    image.add_file(efi_path, "/EFI/BOOT/BOOTX64.EFI")
    image.add_file(efi_path, "/BOOTX64.EFI")
    image.add_file(kernel_path, "/KERNEL.BIN")
    if setup_path is not None and setup_path.exists():
        image.add_file(setup_path, "/SETUP.ELF")
    if font_path is not None and font_path.exists():
        image.add_file(font_path, "/FONTS/MSYH.TTC")
        image.add_file(font_path, "/MSYH.TTC")
    if password_path is not None and password_path.exists():
        image.add_file(password_path, "/pwd.txt")

    install_txt = staging / "INSTALL.TXT"
    install_txt.write_text(
        "MoniOS UEFI installer media\r\n"
        "Run SETUP.ELF from MoniOS to install BOOTX64.EFI and KERNEL.BIN.\r\n",
        encoding="ascii",
    )
    install_flg = staging / "INSTALL.FLG"
    install_flg.write_text("MONIOS_UEFI_INSTALLER=1\r\n", encoding="ascii")
    monios_ini = staging / "MONIOS.INI"
    monios_ini.write_text("boot=uefi\r\nmode=installer\r\n", encoding="ascii")
    image.add_file(install_txt, "/INSTALL.TXT")
    image.add_file(install_flg, "/INSTALL.FLG")
    image.add_file(monios_ini, "/MONIOS.INI")
    image.save()
    return esp_path.read_bytes()


def build_iso(output: Path, esp_data: bytes, root_files: list[IsoFile]) -> None:
    path_l_lba = 19
    path_m_lba = 20
    boot_catalog_lba = 21
    root_lba = 22

    dummy_root = dir_record(root_lba, 0, 0x02, b"\x00") + dir_record(root_lba, 0, 0x02, b"\x01")
    for item in root_files:
        dummy_root += dir_record(0, len(item.data), 0, item.name)
    root_size = len(dummy_root)
    root_sectors = (root_size + SECTOR - 1) // SECTOR
    root_padded_size = root_sectors * SECTOR
    data_lba = root_lba + root_sectors

    current_lba = data_lba
    for item in root_files:
        item.lba = current_lba
        current_lba += (len(item.data) + SECTOR - 1) // SECTOR

    root_dir = bytearray()
    root_dir += dir_record(root_lba, root_padded_size, 0x02, b"\x00")
    root_dir += dir_record(root_lba, root_padded_size, 0x02, b"\x01")
    for item in root_files:
        root_dir += dir_record(item.lba, len(item.data), 0, item.name)
    root_dir = bytearray(pad_sector(bytes(root_dir)))

    total_sectors = current_lba
    path_l = path_table(root_lba, False)
    path_m = path_table(root_lba, True)

    iso = bytearray(16 * SECTOR)
    iso += build_pvd(total_sectors, root_lba, root_padded_size, len(path_l), path_l_lba, path_m_lba)
    iso += build_boot_record(boot_catalog_lba)
    iso += build_terminator()
    iso += pad_sector(path_l)
    iso += pad_sector(path_m)
    iso += build_boot_catalog(root_files[0].lba, len(esp_data))
    iso += root_dir
    if len(iso) // SECTOR != data_lba:
        raise RuntimeError("ISO layout mismatch")

    for item in root_files:
        iso += pad_sector(item.data)

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(iso)


def main() -> None:
    parser = argparse.ArgumentParser(description="Build a MoniOS UEFI installer ISO")
    parser.add_argument("--output", type=Path, default=PROJECT_DIR / "out" / "monios_uefi_installer.iso")
    parser.add_argument("--esp", type=Path, default=PROJECT_DIR / "out" / "monios_uefi_esp.img")
    parser.add_argument("--efi", type=Path, default=PROJECT_DIR / "out" / "monios.efi")
    parser.add_argument("--kernel", type=Path, default=PROJECT_DIR / "out" / "kernel.bin")
    parser.add_argument("--setup", type=Path, default=PROJECT_DIR / "out" / "setup.elf")
    parser.add_argument("--font", type=Path, default=PROJECT_DIR / "out" / "msyh.ttc")
    parser.add_argument("--password", type=Path, default=PROJECT_DIR / "pwd.txt")
    args = parser.parse_args()

    if not args.efi.exists():
        raise SystemExit(f"missing UEFI loader: {args.efi}")
    if not args.kernel.exists():
        raise SystemExit(f"missing kernel: {args.kernel}")

    staging = PROJECT_DIR / "out" / "uefi_iso_staging"
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True)

    esp_data = build_esp_image(
        args.esp,
        staging,
        args.efi,
        args.kernel,
        args.setup if args.setup.exists() else None,
        args.font if args.font.exists() else None,
        args.password if args.password.exists() else None,
    )
    install_txt = (staging / "INSTALL.TXT").read_bytes()
    install_flg = (staging / "INSTALL.FLG").read_bytes()
    monios_ini = (staging / "MONIOS.INI").read_bytes()

    root_files = [
        IsoFile(b"EFI_BOOT.IMG;1", esp_data),
        IsoFile(b"BOOTX64.EFI;1", args.efi.read_bytes()),
        IsoFile(b"KERNEL.BIN;1", args.kernel.read_bytes()),
        IsoFile(b"INSTALL.TXT;1", install_txt),
        IsoFile(b"INSTALL.FLG;1", install_flg),
        IsoFile(b"MONIOS.INI;1", monios_ini),
    ]
    if args.setup.exists():
        root_files.append(IsoFile(b"SETUP.ELF;1", args.setup.read_bytes()))
    if args.font.exists():
        root_files.append(IsoFile(b"MSYH.TTC;1", args.font.read_bytes()))
    if args.password.exists():
        root_files.append(IsoFile(b"PWD.TXT;1", args.password.read_bytes()))

    build_iso(args.output, esp_data, root_files)
    print(f"UEFI ISO: {args.output} ({args.output.stat().st_size} bytes)")
    print(f"ESP image: {args.esp} ({len(esp_data)} bytes)")


if __name__ == "__main__":
    main()
