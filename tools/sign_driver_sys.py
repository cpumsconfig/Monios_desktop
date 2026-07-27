from __future__ import annotations

import argparse
import hashlib
import shutil
import struct
import subprocess
from pathlib import Path


DEFAULT_QM_DIR = Path("D:/qm")
DEFAULT_SUBJECT = "MoniOS Driver Signing"
CODE_SIGNING_EKU = "1.3.6.1.5.5.7.3.3"
SIGNER_MARKER = b"MONIOS-SIGNER-V1"


def run(args: list[str], cwd: Path | None = None) -> None:
    subprocess.run(args, cwd=str(cwd) if cwd else None, check=True)


def cert_thumbprint(subject: str) -> str:
    ps = (
        "$subject = 'CN={0}'; "
        "Get-ChildItem Cert:\\CurrentUser\\My | "
        "Where-Object {{ $_.Subject -eq $subject -and $_.HasPrivateKey }} | "
        "Select-Object -First 1 -ExpandProperty Thumbprint"
    ).format(subject.replace("'", "''"))
    result = subprocess.run(
        ["powershell", "-NoProfile", "-Command", ps],
        check=False,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def ensure_certificate(qm_dir: Path, subject: str) -> None:
    if cert_thumbprint(subject):
        return
    makecert = qm_dir / "makecert.exe"
    if not makecert.exists():
        raise SystemExit(f"missing makecert: {makecert}")
    run([
        str(makecert),
        "-r",
        "-n",
        f"CN={subject}",
        "-ss",
        "My",
        "-sr",
        "CurrentUser",
        "-sky",
        "signature",
        "-eku",
        CODE_SIGNING_EKU,
    ])
    if not cert_thumbprint(subject):
        raise SystemExit(f"certificate was not created: CN={subject}")


def signer_id(subject: str) -> bytes:
    thumbprint = cert_thumbprint(subject).replace(" ", "").replace("\r", "").replace("\n", "")
    try:
        certificate_digest = bytes.fromhex(thumbprint)
    except ValueError as exc:
        raise SystemExit(f"invalid certificate thumbprint for CN={subject}") from exc
    if not certificate_digest:
        raise SystemExit(f"certificate thumbprint missing for CN={subject}")
    return hashlib.sha256(certificate_digest).digest()


def append_signer_marker(path: Path, identity: bytes) -> None:
    marker = SIGNER_MARKER + identity
    data = path.read_bytes()
    if marker not in data:
        path.write_bytes(data + marker)


def pe_security_directory_size(path: Path) -> int:
    data = path.read_bytes()
    if len(data) < 0x40 or data[:2] != b"MZ":
        return 0
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if pe_offset + 24 + 112 + 8 > len(data) or data[pe_offset:pe_offset + 4] != b"PE\0\0":
        return 0
    optional_offset = pe_offset + 24
    magic = struct.unpack_from("<H", data, optional_offset)[0]
    if magic != 0x20B:
        return 0
    number_of_rva_and_sizes = struct.unpack_from("<I", data, optional_offset + 108)[0]
    if number_of_rva_and_sizes <= 4:
        return 0
    cert_offset, cert_size = struct.unpack_from("<II", data, optional_offset + 112 + 4 * 8)
    if cert_offset == 0 or cert_size < 8 or cert_offset + cert_size > len(data):
        return 0
    win_cert_len, revision, cert_type = struct.unpack_from("<IHH", data, cert_offset)
    if win_cert_len < 8 or win_cert_len > cert_size:
        return 0
    if revision not in (0x0100, 0x0200) or cert_type != 0x0002:
        return 0
    return cert_size


def main() -> None:
    parser = argparse.ArgumentParser(description="Sign a MoniOS PE image with D:/qm signtool")
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--qm-dir", type=Path, default=DEFAULT_QM_DIR)
    parser.add_argument("--subject", default=DEFAULT_SUBJECT)
    args = parser.parse_args()

    signtool = args.qm_dir / "signtool.exe"
    if not signtool.exists():
        raise SystemExit(f"missing signtool: {signtool}")
    if not args.input.exists():
        raise SystemExit(f"missing input driver: {args.input}")

    ensure_certificate(args.qm_dir, args.subject)
    identity = signer_id(args.subject)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if args.input.resolve() != args.output.resolve():
        shutil.copyfile(args.input, args.output)
    append_signer_marker(args.output, identity)

    run([
        str(signtool),
        "sign",
        "/fd",
        "SHA256",
        "/s",
        "My",
        "/n",
        args.subject,
        str(args.output),
    ])
    if pe_security_directory_size(args.output) == 0:
        raise SystemExit(f"PE signature table missing after signing: {args.output}")
    signed_data = args.output.read_bytes()
    if SIGNER_MARKER + identity not in signed_data:
        raise SystemExit(f"MoniOS signer marker missing after signing: {args.output}")


if __name__ == "__main__":
    main()
