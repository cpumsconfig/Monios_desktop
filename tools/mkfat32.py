from __future__ import annotations

import argparse
import math
import struct
from pathlib import Path


SECTOR_SIZE = 512
TOTAL_SECTORS = 202752
RESERVED_SECTORS = 32
FAT_COUNT = 2
SECTORS_PER_CLUSTER = 1
FAT_SIZE = 1576
ROOT_CLUSTER = 2
DATA_LBA = RESERVED_SECTORS + FAT_COUNT * FAT_SIZE
END_CLUSTER = 0x0FFFFFFF


def short_name(name: str) -> bytes:
    if name in (".", ".."):
        return name.encode("ascii").ljust(11, b" ")
    if "." in name:
        base, ext = name.rsplit(".", 1)
    else:
        base, ext = name, ""
    base = base.upper()
    ext = ext.upper()
    if not base or len(base) > 8 or len(ext) > 3:
        raise ValueError(f"path component is not FAT 8.3 compatible: {name}")
    return base.encode("ascii").ljust(8, b" ") + ext.encode("ascii").ljust(3, b" ")


def canonical_short_text(name: str) -> str | None:
    if name in (".", ".."):
        return name
    if "." in name:
        base, ext = name.rsplit(".", 1)
    else:
        base, ext = name, ""
    if not base or len(base) > 8 or len(ext) > 3:
        return None
    try:
        short_name(name)
    except (UnicodeEncodeError, ValueError):
        return None
    return base.upper() + (("." + ext.upper()) if ext else "")


def choose_short_alias(name: str, used: set[bytes]) -> tuple[bytes, bool]:
    canonical = canonical_short_text(name)
    if canonical is not None:
        alias = short_name(name)
        if alias not in used:
            return alias, canonical != name

    if "." in name:
        base, ext = name.rsplit(".", 1)
    else:
        base, ext = name, ""
    allowed = "$%'-_@~`!(){}^#&"
    cleaned = "".join(
        char for char in base.upper()
        if char.isalnum() or char in allowed
    )
    if not cleaned:
        cleaned = "FILE"
    cleaned_ext = "".join(
        char for char in ext.upper()
        if char.isalnum() or char in allowed
    )[:3]

    for index in range(1, 100000):
        suffix = f"~{index}"
        stem = cleaned[:max(1, 8 - len(suffix))] + suffix
        candidate_text = stem + (("." + cleaned_ext) if cleaned_ext else "")
        candidate = short_name(candidate_text)
        if candidate not in used:
            return candidate, True
    raise RuntimeError(f"could not allocate a short alias for {name}")


def short_name_checksum(alias: bytes) -> int:
    checksum = 0
    for value in alias:
        checksum = ((checksum & 1) << 7) + (checksum >> 1) + value
        checksum &= 0xFF
    return checksum


def long_name_entries(name: str, alias: bytes) -> list[bytes]:
    units = list(name.encode("utf-16le"))
    code_units = [
        units[index] | (units[index + 1] << 8)
        for index in range(0, len(units), 2)
    ]
    chunks = [
        code_units[index:index + 13]
        for index in range(0, len(code_units), 13)
    ] or [[]]
    entries: list[bytes] = []
    checksum = short_name_checksum(alias)
    for sequence in range(len(chunks), 0, -1):
        chars = list(chunks[sequence - 1])
        if sequence == len(chunks) and len(chars) < 13:
            chars.append(0)
        chars.extend([0xFFFF] * (13 - len(chars)))
        entry = bytearray(32)
        entry[0] = sequence | (0x40 if sequence == len(chunks) else 0)
        entry[11] = 0x0F
        entry[12] = 0
        entry[13] = checksum
        for index, value in enumerate(chars[0:5]):
            struct.pack_into("<H", entry, 1 + index * 2, value)
        for index, value in enumerate(chars[5:11]):
            struct.pack_into("<H", entry, 14 + index * 2, value)
        for index, value in enumerate(chars[11:13]):
            struct.pack_into("<H", entry, 28 + index * 2, value)
        entries.append(bytes(entry))
    return entries


class Fat32Image:
    def __init__(self, path: Path, boot_sector: bytes) -> None:
        self.path = path
        self.data = bytearray(TOTAL_SECTORS * SECTOR_SIZE)
        self.fat: list[int] = [0] * (FAT_SIZE * SECTOR_SIZE // 4)
        self.next_cluster = ROOT_CLUSTER + 1
        self.dir_records: dict[int, list[tuple[str, int, int, int, bytes]]] = {
            ROOT_CLUSTER: []
        }
        self.dir_chains: dict[int, list[int]] = {ROOT_CLUSTER: [ROOT_CLUSTER]}
        self.write_boot_sector(boot_sector)
        self.fat[0] = 0x0FFFFFF8
        self.fat[1] = 0x0FFFFFFF
        self.fat[ROOT_CLUSTER] = END_CLUSTER

    def cluster_lba(self, cluster: int) -> int:
        return DATA_LBA + (cluster - 2) * SECTORS_PER_CLUSTER

    def write_boot_sector(self, boot_sector: bytes) -> None:
        if len(boot_sector) != SECTOR_SIZE:
            raise ValueError("boot sector must be exactly 512 bytes")
        self.data[0:SECTOR_SIZE] = boot_sector
        backup_offset = 6 * SECTOR_SIZE
        self.data[backup_offset:backup_offset + SECTOR_SIZE] = boot_sector
        fsinfo = bytearray(SECTOR_SIZE)
        struct.pack_into("<I", fsinfo, 0, 0x41615252)
        struct.pack_into("<I", fsinfo, 484, 0x61417272)
        struct.pack_into("<I", fsinfo, 488, 0xFFFFFFFF)
        struct.pack_into("<I", fsinfo, 492, 0xFFFFFFFF)
        struct.pack_into("<H", fsinfo, 510, 0xAA55)
        fsinfo_offset = 1 * SECTOR_SIZE
        self.data[fsinfo_offset:fsinfo_offset + SECTOR_SIZE] = fsinfo
        backup_fsinfo_offset = 7 * SECTOR_SIZE
        self.data[backup_fsinfo_offset:backup_fsinfo_offset + SECTOR_SIZE] = fsinfo

    def alloc_cluster(self) -> int:
        cluster = self.next_cluster
        self.next_cluster += 1
        if self.next_cluster >= len(self.fat):
            raise RuntimeError("FAT is full")
        self.fat[cluster] = END_CLUSTER
        return cluster

    def write_cluster(self, cluster: int, payload: bytes) -> None:
        offset = self.cluster_lba(cluster) * SECTOR_SIZE
        self.data[offset:offset + SECTOR_SIZE] = b"\0" * SECTOR_SIZE
        self.data[offset:offset + len(payload)] = payload

    def chain_for_data(self, payload: bytes) -> int:
        clusters = max(1, math.ceil(len(payload) / SECTOR_SIZE))
        first = self.alloc_cluster()
        current = first
        for index in range(clusters):
            chunk = payload[index * SECTOR_SIZE:(index + 1) * SECTOR_SIZE]
            self.write_cluster(current, chunk)
            if index + 1 < clusters:
                nxt = self.alloc_cluster()
                self.fat[current] = nxt
                current = nxt
        return first

    def dir_entry(self, alias: bytes, attr: int, cluster: int, size: int) -> bytes:
        entry = bytearray(32)
        entry[0:11] = alias
        entry[11] = attr
        struct.pack_into("<H", entry, 20, (cluster >> 16) & 0xFFFF)
        struct.pack_into("<H", entry, 26, cluster & 0xFFFF)
        struct.pack_into("<I", entry, 28, size)
        return bytes(entry)

    def append_record(self, dir_cluster: int, name: str, attr: int, cluster: int, size: int) -> None:
        used = {record[4] for record in self.dir_records[dir_cluster]}
        alias, _needs_lfn = choose_short_alias(name, used)
        record = (name, attr, cluster, size, alias)
        self.dir_records[dir_cluster].append(record)

    def records_as_bytes(self, records: list[tuple[str, int, int, int, bytes]]) -> bytes:
        output: list[bytes] = []
        for name, attr, cluster, size, alias in records:
            if name not in (".", ".."):
                output.extend(long_name_entries(name, alias))
            output.append(self.dir_entry(alias, attr, cluster, size))
        return b"".join(output)

    def mkdir(self, path: str) -> int:
        parent_cluster, name = self.resolve_parent(path)
        existing = self.find_entry(parent_cluster, name)
        if existing is not None:
            return existing
        cluster = self.alloc_cluster()
        self.dir_records[cluster] = []
        self.dir_chains[cluster] = [cluster]
        self.append_record(cluster, ".", 0x10, cluster, 0)
        self.append_record(
            cluster,
            "..",
            0x10,
            parent_cluster if parent_cluster != ROOT_CLUSTER else 0,
            0,
        )
        self.append_record(parent_cluster, name, 0x10, cluster, 0)
        return cluster

    def add_file(self, source: Path, dest: str) -> None:
        parent_cluster, name = self.resolve_parent(dest, create=True)
        payload = source.read_bytes()
        cluster = self.chain_for_data(payload) if payload else 0
        self.append_record(parent_cluster, name, 0x20, cluster, len(payload))

    def resolve_parent(self, path: str, create: bool = False) -> tuple[int, str]:
        parts = [p for p in path.replace("\\", "/").split("/") if p]
        if not parts:
            raise ValueError("destination path must include a file or directory name")
        cluster = ROOT_CLUSTER
        for index, part in enumerate(parts[:-1]):
            next_cluster = self.find_entry(cluster, part)
            if next_cluster is None:
                if not create:
                    raise FileNotFoundError(path)
                next_cluster = self.mkdir("/" + "/".join(parts[:index + 1]))
            cluster = next_cluster
        return cluster, parts[-1]

    def find_entry(self, dir_cluster: int, name: str) -> int | None:
        target = name.casefold()
        for entry_name, attr, cluster, _size, _alias in self.dir_records.get(dir_cluster, []):
            if entry_name.casefold() == target and (attr & 0x10):
                return cluster
        return None

    def flush_dirs(self) -> None:
        for cluster, records in self.dir_records.items():
            payload = self.records_as_bytes(records)
            required_clusters = max(1, math.ceil(len(payload) / SECTOR_SIZE))
            chain = self.dir_chains[cluster]
            while len(chain) < required_clusters:
                next_cluster = self.alloc_cluster()
                self.fat[chain[-1]] = next_cluster
                chain.append(next_cluster)
            for index, dir_cluster in enumerate(chain):
                chunk = payload[index * SECTOR_SIZE:(index + 1) * SECTOR_SIZE]
                self.write_cluster(dir_cluster, chunk)

    def flush_fats(self) -> None:
        fat_bytes = bytearray(FAT_SIZE * SECTOR_SIZE)
        for index, value in enumerate(self.fat):
            struct.pack_into("<I", fat_bytes, index * 4, value)
        for fat_index in range(FAT_COUNT):
            start = (RESERVED_SECTORS + fat_index * FAT_SIZE) * SECTOR_SIZE
            self.data[start:start + len(fat_bytes)] = fat_bytes

    def save(self) -> None:
        self.flush_dirs()
        self.flush_fats()
        if self.path.exists() and self.path.stat().st_size == len(self.data):
            with self.path.open("r+b") as image:
                image.seek(0)
                image.write(self.data)
            return

        temp_path = self.path.with_suffix(self.path.suffix + ".tmp")
        with temp_path.open("w+b") as image:
            image.write(self.data)
        temp_path.replace(self.path)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", required=True, type=Path)
    parser.add_argument("--boot", required=True, type=Path)
    parser.add_argument("--copy", action="append", default=[], help="source:dest")
    args = parser.parse_args()

    image = Fat32Image(args.image, args.boot.read_bytes())
    for spec in args.copy:
        source_text, dest = spec.split(":", 1)
        image.add_file(Path(source_text), dest)
    image.save()


if __name__ == "__main__":
    main()
