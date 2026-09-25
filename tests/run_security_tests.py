"""Build and execute host security regressions using the installed GCC."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SUITES = {
    "perm_security": ["tests/test_perm_security.c", "lib/string.c", "lib/path.c"],
    "zip_security": ["tests/test_zip_security.c", "lib/string.c"],
    "dos_security": ["tests/test_dos_security.c", "lib/string.c"],
    "cache_security": ["tests/test_cache_security.c", "lib/string.c"],
    "ext_security": ["tests/test_ext_security.c", "tests/stubs_ext.c"],
}


def main():
    with tempfile.TemporaryDirectory(prefix="monios_security_") as directory:
        for name, sources in SUITES.items():
            binary = Path(directory) / (name + (".exe" if os.name == "nt" else ""))
            subprocess.run(["gcc", "-std=gnu17", "-fno-builtin", "-I", "include",
                            "-I", "kernel/net", *sources, "-o", str(binary)],
                           cwd=ROOT, check=True)
            subprocess.run([str(binary)], cwd=ROOT, check=True, timeout=30)
    subprocess.run([sys.executable, "tools/test_dns.py"], cwd=ROOT, check=True)


if __name__ == "__main__":
    main()
