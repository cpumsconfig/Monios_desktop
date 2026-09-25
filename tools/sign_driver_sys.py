from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import struct
import subprocess
from datetime import datetime, timedelta, timezone
from pathlib import Path


DEFAULT_QM_DIR = Path("D:/qm")
DEFAULT_SUBJECT = "MoniOS Driver Signing"
CODE_SIGNING_EKU = "1.3.6.1.5.5.7.3.3"
REPO_ROOT = Path(__file__).resolve().parent.parent
DEVELOPMENT_KEY = REPO_ROOT / "out" / "monios-dev-signing.key.pem"
DEVELOPMENT_CERT = REPO_ROOT / "out" / "monios-dev-signing.crt"
TRUST_ROOT_CERT = REPO_ROOT / "assets" / "cent" / (
    "0000000000000000000000000000000000000000.crt"
)


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


def export_windows_certificate(subject: str) -> None:
    TRUST_ROOT_CERT.parent.mkdir(parents=True, exist_ok=True)

    # 导出结果可以直接复用：ensure_certificate() 只在证书缺失时创建，
    # 证书一旦存在其内容就不会变，所以导出文件存在即等价于导出完成。
    # 这一步同时消除了并行构建（make -j）下多个 .sys 争抢同一导出目标的竞态。
    if TRUST_ROOT_CERT.exists() and TRUST_ROOT_CERT.stat().st_size > 0:
        return

    escaped_subject = subject.replace("'", "''")

    def export_to(target: Path) -> None:
        escaped_path = str(target).replace("'", "''")
        ps = (
            "$cert = Get-ChildItem Cert:\\CurrentUser\\My | "
            "Where-Object {{ $_.Subject -eq 'CN={0}' -and $_.HasPrivateKey }} | "
            "Select-Object -First 1; "
            "if ($null -eq $cert) {{ throw 'signing certificate missing' }}; "
            "Export-Certificate -Cert $cert -FilePath '{1}' -Type CERT -Force | "
            "Out-Null"
        ).format(escaped_subject, escaped_path)
        subprocess.run(
            ["powershell", "-NoProfile", "-Command", ps],
            check=True,
        )

    # 先导出到进程私有的临时文件，再原子替换到目标路径。这样多个签名任务
    # 并发执行时各自写各自的临时文件，读者也永远看不到写了一半的证书。
    tmp_path = TRUST_ROOT_CERT.with_name(
        "{0}.{1}.tmp".format(TRUST_ROOT_CERT.name, os.getpid())
    )
    try:
        export_to(tmp_path)
        os.replace(tmp_path, TRUST_ROOT_CERT)
    except (subprocess.CalledProcessError, OSError) as exc:
        try:
            tmp_path.unlink()
        except OSError:
            pass
        # 并发情况下可能是别的进程已经导出成功，此时直接接受它的结果。
        if TRUST_ROOT_CERT.exists() and TRUST_ROOT_CERT.stat().st_size > 0:
            return
        raise SystemExit(
            "failed to export signer certificate: {0} ({1})".format(
                TRUST_ROOT_CERT, exc
            )
        ) from exc

    if not TRUST_ROOT_CERT.exists():
        raise SystemExit("failed to export signer certificate: {0}".format(TRUST_ROOT_CERT))


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def pe_security_directory_offset(data: bytes) -> int:
    if len(data) < 0x40 or data[:2] != b"MZ":
        raise SystemExit("input is not a PE image")
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    optional_offset = pe_offset + 24
    if (
        pe_offset + 24 > len(data)
        or data[pe_offset:pe_offset + 4] != b"PE\0\0"
        or optional_offset + 120 > len(data)
        or struct.unpack_from("<H", data, optional_offset)[0] != 0x20B
    ):
        raise SystemExit("input is not a PE32+ image")
    number_of_rva_and_sizes = struct.unpack_from("<I", data, optional_offset + 108)[0]
    if number_of_rva_and_sizes <= 4:
        raise SystemExit("PE image has no security directory")
    return optional_offset + 112 + 4 * 8


def authenticode_digest(data: bytes,
                       optional_offset: int,
                       security_dir_offset: int,
                       cert_offset: int,
                       cert_size: int) -> bytes:
    checksum_offset = optional_offset + 64
    cert_end = cert_offset + cert_size
    digest = hashlib.sha256()
    digest.update(data[:checksum_offset])
    digest.update(data[checksum_offset + 4:security_dir_offset])
    digest.update(data[security_dir_offset + 8:cert_offset])
    if cert_end < len(data):
        digest.update(data[cert_end:])
    return digest.digest()


def der_length(length: int) -> bytes:
    if length < 0x80:
        return bytes([length])
    encoded = length.to_bytes((length.bit_length() + 7) // 8, "big")
    return bytes([0x80 | len(encoded)]) + encoded


def der_tlv(tag: int, value: bytes) -> bytes:
    return bytes([tag]) + der_length(len(value)) + value


def der_sequence(*values: bytes) -> bytes:
    return der_tlv(0x30, b"".join(values))


def der_set(*values: bytes) -> bytes:
    return der_tlv(0x31, b"".join(values))


def der_oid(encoded_value: bytes) -> bytes:
    return der_tlv(0x06, encoded_value)


def der_integer(value: int) -> bytes:
    raw = value.to_bytes(max(1, (value.bit_length() + 7) // 8), "big")
    if raw[0] & 0x80:
        raw = b"\0" + raw
    return der_tlv(0x02, raw)


def der_algorithm(oid: bytes) -> bytes:
    return der_sequence(der_oid(oid), der_tlv(0x05, b""))


def development_certificate(subject: str):
    try:
        from cryptography import x509
        from cryptography.hazmat.primitives import hashes, serialization
        from cryptography.hazmat.primitives.asymmetric import rsa
        from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID
    except ImportError as exc:
        raise SystemExit(
            "Linux signing requires the Python 'cryptography' package"
        ) from exc

    DEVELOPMENT_KEY.parent.mkdir(parents=True, exist_ok=True)
    if DEVELOPMENT_KEY.exists() and DEVELOPMENT_CERT.exists():
        try:
            key = serialization.load_pem_private_key(
                DEVELOPMENT_KEY.read_bytes(),
                password=None,
            )
            certificate = x509.load_der_x509_certificate(DEVELOPMENT_CERT.read_bytes())
            TRUST_ROOT_CERT.parent.mkdir(parents=True, exist_ok=True)
            TRUST_ROOT_CERT.write_bytes(
                certificate.public_bytes(serialization.Encoding.DER)
            )
            return key, certificate
        except Exception:
            DEVELOPMENT_KEY.unlink(missing_ok=True)
            DEVELOPMENT_CERT.unlink(missing_ok=True)

    key = rsa.generate_private_key(public_exponent=65537, key_size=1024)
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, subject)])
    now = datetime.now(timezone.utc)
    certificate = (
        x509.CertificateBuilder()
        .subject_name(name)
        .issuer_name(name)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - timedelta(days=1))
        .not_valid_after(now + timedelta(days=3650))
        .add_extension(x509.BasicConstraints(ca=True, path_length=0), critical=True)
        .add_extension(
            x509.KeyUsage(
                digital_signature=True,
                content_commitment=False,
                key_encipherment=False,
                data_encipherment=False,
                key_agreement=False,
                key_cert_sign=True,
                crl_sign=False,
                encipher_only=False,
                decipher_only=False,
            ),
            critical=True,
        )
        .add_extension(
            x509.ExtendedKeyUsage([ExtendedKeyUsageOID.CODE_SIGNING]),
            critical=False,
        )
        .sign(key, hashes.SHA256())
    )
    DEVELOPMENT_KEY.write_bytes(
        key.private_bytes(
            serialization.Encoding.PEM,
            serialization.PrivateFormat.PKCS8,
            serialization.NoEncryption(),
        )
    )
    cert_der = certificate.public_bytes(serialization.Encoding.DER)
    DEVELOPMENT_CERT.write_bytes(cert_der)
    TRUST_ROOT_CERT.parent.mkdir(parents=True, exist_ok=True)
    TRUST_ROOT_CERT.write_bytes(cert_der)
    return key, certificate


def build_authenticode_cms(certificate, key, file_digest: bytes) -> bytes:
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.asymmetric import padding
    from cryptography.hazmat.primitives.serialization import Encoding

    oid_spc_indirect = bytes.fromhex("2b060104018237020104")
    oid_spc_pe_image_data = bytes.fromhex("2b06010401823702010f")
    oid_spc_statement_type = bytes.fromhex("2b060104018237020115")
    oid_signed_data = bytes.fromhex("2a864886f70d010702")
    oid_sha256 = bytes.fromhex("608648016503040201")
    oid_rsa_encryption = bytes.fromhex("2a864886f70d010101")
    oid_content_type = bytes.fromhex("2a864886f70d010903")
    oid_message_digest = bytes.fromhex("2a864886f70d010904")
    oid_spc_opus_info = bytes.fromhex("2b06010401823702010c")
    oid_spc_statement = bytes.fromhex("2b06010401823702010b")

    # This is the standard minimal SpcPeImageData value used by signtool.
    spc_data = bytes.fromhex(
        "3017060a2b06010401823702010f"
        "3009030100a004a2028000"
    )
    digest_info = der_sequence(
        der_algorithm(oid_sha256),
        der_tlv(0x04, file_digest),
    )
    indirect = der_sequence(spc_data, digest_info)
    encap = der_sequence(
        der_oid(oid_spc_indirect),
        der_tlv(0xA0, indirect),
    )

    attributes = [
        der_sequence(
            der_oid(oid_spc_opus_info),
            der_set(der_sequence()),
        ),
        der_sequence(
            der_oid(oid_content_type),
            der_set(der_oid(oid_spc_indirect)),
        ),
        der_sequence(
            der_oid(oid_spc_statement),
            der_set(der_sequence(der_oid(oid_spc_statement_type))),
        ),
        der_sequence(
            der_oid(oid_message_digest),
            der_set(der_tlv(0x04, hashlib.sha256(indirect[2:]).digest())),
        ),
    ]
    signed_attrs = der_set(*attributes)
    signature = key.sign(
        signed_attrs,
        padding.PKCS1v15(),
        hashes.SHA256(),
    )
    signed_attrs_implicit = bytes([0xA0]) + signed_attrs[1:]
    certificate_der = certificate.public_bytes(Encoding.DER)
    issuer_and_serial = der_sequence(
        certificate.issuer.public_bytes(),
        der_integer(certificate.serial_number),
    )
    signer_info = der_sequence(
        der_integer(1),
        issuer_and_serial,
        der_algorithm(oid_sha256),
        signed_attrs_implicit,
        der_algorithm(oid_rsa_encryption),
        der_tlv(0x04, signature),
    )
    signed_data = der_sequence(
        der_integer(1),
        der_set(der_algorithm(oid_sha256)),
        encap,
        der_tlv(0xA0, certificate_der),
        der_set(signer_info),
    )
    return der_sequence(
        der_oid(oid_signed_data),
        der_tlv(0xA0, signed_data),
    )


def write_linux_authenticode_signature(input_path: Path,
                                       output_path: Path,
                                       subject: str) -> None:
    key, certificate = development_certificate(subject)
    image = bytearray(input_path.read_bytes())
    security_dir_offset = pe_security_directory_offset(bytes(image))
    optional_offset = security_dir_offset - 112 - 4 * 8
    existing_offset, existing_size = struct.unpack_from("<II", image, security_dir_offset)
    if existing_offset != 0 or existing_size != 0:
        raise SystemExit(f"input already has a PE security directory: {input_path}")

    cert_offset = align_up(len(image), 8)
    image.extend(b"\0" * (cert_offset - len(image)))
    placeholder_cms = build_authenticode_cms(
        certificate,
        key,
        b"\0" * 32,
    )
    cert_size = align_up(8 + len(placeholder_cms), 8)
    struct.pack_into("<II", image, security_dir_offset, cert_offset, cert_size)
    provisional = bytes(image) + b"\0" * cert_size
    digest = authenticode_digest(
        provisional,
        optional_offset,
        security_dir_offset,
        cert_offset,
        cert_size,
    )
    cms = build_authenticode_cms(certificate, key, digest)
    cert_length = 8 + len(cms)
    if align_up(cert_length, 8) != cert_size:
        cert_size = align_up(cert_length, 8)
        struct.pack_into("<II", image, security_dir_offset, cert_offset, cert_size)
        provisional = bytes(image) + b"\0" * cert_size
        digest = authenticode_digest(
            provisional,
            optional_offset,
            security_dir_offset,
            cert_offset,
            cert_size,
        )
        cms = build_authenticode_cms(certificate, key, digest)
        cert_length = 8 + len(cms)
    certificate_blob = struct.pack("<IHH", cert_length, 0x0200, 0x0002)
    certificate_blob += cms
    certificate_blob += b"\0" * (cert_size - len(certificate_blob))
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(bytes(image) + certificate_blob)

    if pe_security_directory_size(output_path) == 0:
        raise SystemExit(f"PE signature table missing after Linux signing: {output_path}")


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
    parser = argparse.ArgumentParser(description="Sign a MoniOS PE image for kernel verification")
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--qm-dir", type=Path, default=DEFAULT_QM_DIR)
    parser.add_argument("--subject", default=DEFAULT_SUBJECT)
    args = parser.parse_args()

    if not args.input.exists():
        raise SystemExit(f"missing input driver: {args.input}")

    signing_mode = os.environ.get("MONIOS_SIGNING_MODE", "auto").strip().lower()
    if signing_mode in ("synthetic", "development", "linux") or (
        signing_mode == "auto" and os.name != "nt"
    ):
        write_linux_authenticode_signature(args.input, args.output, args.subject)
        return

    signtool = args.qm_dir / "signtool.exe"
    if not signtool.exists():
        raise SystemExit(f"missing signtool: {signtool}")

    ensure_certificate(args.qm_dir, args.subject)
    export_windows_certificate(args.subject)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if args.input.resolve() != args.output.resolve():
        shutil.copyfile(args.input, args.output)

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


if __name__ == "__main__":
    main()
