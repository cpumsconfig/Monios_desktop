import ctypes
import _ctypes
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def put16(packet, value):
    packet.extend([(value >> 8) & 0xFF, value & 0xFF])


def encode_name(name):
    out = bytearray()
    for label in name.split("."):
        out.append(len(label))
        out.extend(label.encode("ascii"))
    out.append(0)
    return out


def build_response(query_name, answer_name, answer_ip, query_type=1,
                   query_class=1, answer_pointer=True):
    packet = bytearray()
    put16(packet, 0x1234)
    put16(packet, 0x8180)
    put16(packet, 1)
    put16(packet, 1)
    put16(packet, 0)
    put16(packet, 0)
    packet.extend(encode_name(query_name))
    put16(packet, query_type)
    put16(packet, query_class)
    if answer_pointer:
        packet.extend([0xC0, 0x0C])
    else:
        packet.extend(encode_name(answer_name))
    put16(packet, 1)
    put16(packet, 1)
    packet.extend([0, 0, 0, 60])
    put16(packet, 4)
    packet.extend(answer_ip)
    return bytes(packet)


class DnsLibrary:
    def __init__(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="monios_dns_test_"))
        source = self.tmp / "dns_test.c"
        self.lib_path = self.tmp / ("dns_test.dll" if os.name == "nt" else "dns_test.so")
        source.write_text(
            r'''
#include "lib/string.c"
#include "kernel/net/ip.c"
#include "kernel/net/dns.c"

static uint8_t test_dns_server[4] = { 8, 8, 8, 8 };

bool ipv4_parse(const char *text, uint8_t out[4])
{
    return ip_parse_ipv4(text, out);
}

uint64_t cpu_read_tsc(void)
{
    return 0x12345678ULL;
}

uint64_t timer_ticks(void) { return 1ULL; }
uint32_t timer_hz(void) { return 1000U; }

bool net_get_dns_ip(uint8_t out[4])
{
    memcpy(out, test_dns_server, 4);
    return true;
}

uint32_t net_dns_server_count(void) { return 1U; }

bool net_get_dns_server(uint32_t index, uint8_t out[4])
{
    if (index != 0U || out == NULL) return false;
    memcpy(out, test_dns_server, 4);
    return true;
}

bool net_udp_send_to(const uint8_t dst_ip[4], uint16_t src_port,
                     uint16_t dst_port, const uint8_t *payload,
                     uint16_t payload_len)
{
    (void) dst_ip;
    (void) src_port;
    (void) dst_port;
    (void) payload;
    (void) payload_len;
    return true;
}

void net_update(void)
{
}

void io_wait(void)
{
}

void log_write(const char *str)
{
    (void) str;
}

void dns_test_begin(const char *name)
{
    dns_init();
    strcpy(g_dns_query_name, name);
    g_dns_query_id = 0x1234;
    g_dns_waiting = true;
    g_dns_answer_valid = false;
    memcpy(g_dns_server_ip, test_dns_server, 4);
}

int dns_test_handle(const uint8_t *packet, uint16_t length, uint8_t out[4])
{
    dns_handle_udp(test_dns_server, 53, packet, length);
    if (!g_dns_answer_valid) {
        return 0;
    }
    memcpy(out, g_dns_answer_ip, 4);
    return 1;
}
''',
            encoding="ascii",
        )
        subprocess.run(
            [
                "gcc", "-shared", "-fPIC", "-ffreestanding", "-fno-builtin",
                "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "include"),
                "-I", str(ROOT / "kernel" / "net"), "-I", str(ROOT),
                str(source), "-o", str(self.lib_path),
            ],
            check=True,
            cwd=ROOT,
        )
        self.lib = ctypes.CDLL(str(self.lib_path))
        self.lib.dns_test_begin.argtypes = [ctypes.c_char_p]
        self.lib.dns_test_begin.restype = None
        self.lib.dns_test_handle.argtypes = [
            ctypes.c_char_p, ctypes.c_uint16, ctypes.POINTER(ctypes.c_uint8),
        ]
        self.lib.dns_test_handle.restype = ctypes.c_int

    def close(self):
        handle = self.lib._handle
        del self.lib
        if os.name == "nt":
            _ctypes.FreeLibrary(handle)
        else:
            _ctypes.dlclose(handle)
        shutil.rmtree(self.tmp)


class DnsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dns = DnsLibrary()

    @classmethod
    def tearDownClass(cls):
        cls.dns.close()

    def resolve_packet(self, packet, query=b"example.com"):
        out = (ctypes.c_uint8 * 4)()
        self.dns.lib.dns_test_begin(query)
        accepted = self.dns.lib.dns_test_handle(packet, len(packet), out)
        return accepted, bytes(out)

    def test_accepts_matching_answer(self):
        packet = build_response("example.com", "example.com", b"\x01\x02\x03\x04")
        self.assertEqual(self.resolve_packet(packet), (1, b"\x01\x02\x03\x04"))

    def test_rejects_mismatched_question_name(self):
        packet = build_response("attacker.test", "attacker.test", b"\x05\x06\x07\x08")
        self.assertEqual(self.resolve_packet(packet), (0, b"\x00\x00\x00\x00"))

    def test_rejects_mismatched_answer_name(self):
        packet = build_response("example.com", "attacker.test", b"\x05\x06\x07\x08",
                                answer_pointer=False)
        self.assertEqual(self.resolve_packet(packet), (0, b"\x00\x00\x00\x00"))

    def test_name_matching_is_case_insensitive(self):
        packet = build_response("EXAMPLE.COM", "EXAMPLE.COM", b"\x09\x08\x07\x06")
        self.assertEqual(self.resolve_packet(packet), (1, b"\x09\x08\x07\x06"))

    def test_rejects_bad_compression_pointer(self):
        packet = bytearray(build_response("example.com", "example.com",
                                          b"\x01\x01\x01\x01"))
        answer_start = 12 + len(encode_name("example.com")) + 4
        packet[answer_start] = 0xC0
        packet[answer_start + 1] = 0xFF
        self.assertEqual(self.resolve_packet(bytes(packet)), (0, b"\x00\x00\x00\x00"))

    def test_rejects_truncated_response(self):
        packet = bytearray(build_response("example.com", "example.com", b"\x01\x02\x03\x04"))
        packet[2] |= 2
        self.assertEqual(self.resolve_packet(bytes(packet))[0], 0)

    def test_rejects_overlong_name_alias(self):
        name = "a" * 63 + ".evil"
        packet = build_response(name, name, b"\x01\x02\x03\x04")
        self.assertEqual(self.resolve_packet(packet, query=b"a" * 63)[0], 0)

    def test_rejects_embedded_nul_alias(self):
        name = "example.com\x00.evil"
        packet = build_response(name, name, b"\x01\x02\x03\x04")
        self.assertEqual(self.resolve_packet(packet)[0], 0)


if __name__ == "__main__":
    unittest.main()
