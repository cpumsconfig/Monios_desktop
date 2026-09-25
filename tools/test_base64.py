"""Host regression tests: python tools/test_base64.py (requires GCC)."""
import base64
import ctypes
from pathlib import Path
import random
import subprocess
import tempfile
import unittest


class Base64Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = Path(__file__).resolve().parents[1]
        cls.temp = tempfile.TemporaryDirectory()
        library = Path(cls.temp.name) / "base64.dll"
        subprocess.run([
            "gcc", "-shared", "-fPIC", "-ffreestanding", "-fno-builtin",
            "-Wall", "-Wextra", "-Werror", "-I", str(root / "include"),
            str(root / "lib/base64.c"), "-o", str(library),
        ], check=True)
        cls.lib = ctypes.CDLL(str(library))
        cls.lib.base64_encode.argtypes = [ctypes.c_char_p, ctypes.c_uint32,
                                         ctypes.c_void_p, ctypes.c_uint32]
        cls.lib.base64_decode.argtypes = [ctypes.c_char_p, ctypes.c_void_p,
                                         ctypes.c_uint32]
        cls.lib.base64_encode.restype = ctypes.c_int32
        cls.lib.base64_decode.restype = ctypes.c_int32

    @classmethod
    def tearDownClass(cls):
        import _ctypes
        if hasattr(_ctypes, "FreeLibrary"):
            _ctypes.FreeLibrary(cls.lib._handle)
        else:
            _ctypes.dlclose(cls.lib._handle)
        cls.temp.cleanup()

    def test_round_trips_and_capacity(self):
        rng = random.Random(42)
        for size in range(257):
            raw = bytes(rng.randrange(256) for _ in range(size))
            expected = base64.b64encode(raw)
            encoded = ctypes.create_string_buffer(len(expected) + 2)
            encoded[-1] = b"!"
            self.assertEqual(self.lib.base64_encode(raw, size, encoded,
                             len(expected) + 1), len(expected))
            self.assertEqual(encoded.value, expected)
            self.assertEqual(encoded[-1], b"!")
            self.assertEqual(self.lib.base64_encode(raw, size, encoded,
                             len(expected)), -1)
            decoded = ctypes.create_string_buffer(size + 1)
            decoded[-1] = b"!"
            self.assertEqual(self.lib.base64_decode(expected, decoded, size), size)
            self.assertEqual(decoded.raw[:size], raw)
            self.assertEqual(decoded[-1], b"!")
            if size:
                self.assertEqual(self.lib.base64_decode(expected, decoded, size - 1), -1)

    def test_malformed_input(self):
        for value in (b"Zg=A", b"Zg=!", b"Zm!=", b"Zg==AAAA", b"Zm8=AAAA",
                      b"=AAA", b"A=AA", b"====", b"Z", b"Zg", b"Zg=", b"Zg==="):
            with self.subTest(value=value):
                out = ctypes.create_string_buffer(32)
                self.assertEqual(self.lib.base64_decode(value, out, 32), -1)

    def test_whitespace(self):
        out = ctypes.create_string_buffer(8)
        self.assertEqual(self.lib.base64_decode(b" Z\tg=\r=\n ", out, 8), 1)
        self.assertEqual(out.raw[:1], b"f")

    def test_overflow_and_null(self):
        out = ctypes.create_string_buffer(b"sentinel")
        for size in (0xFFFFFFFF, 0xFFFFFFFE, 0xC0000000, 0x60000000):
            self.assertEqual(self.lib.base64_encode(b"x", size, out, 0xFFFFFFFF), -1)
            self.assertEqual(out.value, b"sentinel")
        self.assertEqual(self.lib.base64_encode(None, 1, out, 9), -1)
        self.assertEqual(self.lib.base64_encode(b"", 0, None, 9), -1)
        self.assertEqual(self.lib.base64_encode(None, 0, out, 9), 0)
        self.assertEqual(self.lib.base64_decode(None, out, 9), -1)
        self.assertEqual(self.lib.base64_decode(b"Zg==", None, 9), -1)
        self.assertEqual(self.lib.base64_decode(b"Zg==", None, 0), -1)
        self.assertEqual(self.lib.base64_decode(b"", None, 0), 0)


if __name__ == "__main__":
    unittest.main()
