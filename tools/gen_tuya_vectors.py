"""Generuje wektory testowe protokołu Tuya 3.3 referencyjną biblioteką tinytuya.

Użycie: tools/.venv/bin/python tools/gen_tuya_vectors.py > test/test_tuya_proto/vectors.h
Klucz i ID są fikcyjne - plik wynikowy może trafić do gita.
"""
import hashlib
import json
import zlib

import tinytuya
from tinytuya import TuyaMessage, pack_message

KEY = b"0123456789abcdef"
DEV_ID = "bf0000000000000000test"
T = "1700000000"
PREFIX = 0x000055AA
cipher = tinytuya.AESCipher(KEY)


def c_array(name: str, data: bytes) -> str:
    body = ", ".join(f"0x{b:02X}" for b in data)
    return f"static const uint8_t {name}[] = {{ {body} }};\n#define {name.upper()}_LEN {len(data)}u\n"


def c_string(name: str, s: str) -> str:
    return f"static const char {name}[] = {json.dumps(s)};\n"


def frame(seq: int, cmd: int, payload: bytes, retcode=None) -> bytes:
    if retcode is not None:
        payload = retcode.to_bytes(4, "big") + payload
    return pack_message(TuyaMessage(seq, cmd, None, payload, 0, True, PREFIX, None))


out = ["/* Wygenerowane przez tools/gen_tuya_vectors.py (tinytuya %s) - nie edytować ręcznie. */" % tinytuya.__version__,
       "#pragma once", "#include <stdint.h>", ""]
out.append(c_string("VEC_KEY", KEY.decode()))
out.append(c_string("VEC_DEV_ID", DEV_ID))

# CRC32
out.append(c_string("VEC_CRC_INPUT", "123456789"))
out.append(f"#define VEC_CRC_EXPECTED 0x{zlib.crc32(b'123456789'):08X}u\n")

# AES-128-ECB + PKCS#7: 16 bajtów -> pełny blok paddingu
aes_plain = '{"dps":{"1":true}'  # 17 bajtów -> 32 bajty szyfrogramu
out.append(c_string("VEC_AES_PLAIN", aes_plain))
out.append(c_array("vec_aes_cipher", cipher.encrypt(aes_plain.encode(), use_base64=False)))
aes_plain16 = "0123456789ABCDEF"
out.append(c_string("VEC_AES_PLAIN16", aes_plain16))
out.append(c_array("vec_aes_cipher16", cipher.encrypt(aes_plain16.encode(), use_base64=False)))

# CONTROL (cmd 7): nagłówek "3.3"+12x00 + szyfrogram
control_json = json.dumps({"devId": DEV_ID, "uid": DEV_ID, "t": T, "dps": {"20": True}}, separators=(",", ":"))
out.append(c_string("VEC_CONTROL_JSON", control_json))
control_payload = tinytuya.PROTOCOL_33_HEADER + cipher.encrypt(control_json.encode(), use_base64=False)
out.append(c_array("vec_control_frame", frame(1, 7, control_payload)))

# DP_QUERY (cmd 10): bez nagłówka wersji
query_json = json.dumps({"gwId": DEV_ID, "devId": DEV_ID, "uid": DEV_ID, "t": T}, separators=(",", ":"))
out.append(c_string("VEC_QUERY_JSON", query_json))
out.append(c_array("vec_query_frame", frame(2, 10, cipher.encrypt(query_json.encode(), use_base64=False))))

# HEART_BEAT (cmd 9): pusty payload
out.append(c_array("vec_heartbeat_frame", frame(3, 9, b"")))

# Odpowiedź urządzenia na DP_QUERY: retcode 0 + szyfrogram (bez nagłówka)
resp_json = json.dumps({"devId": DEV_ID, "dps": {"20": False, "22": 1000}}, separators=(",", ":"))
out.append(c_string("VEC_RESP_JSON", resp_json))
out.append(c_array("vec_resp_frame", frame(2, 10, cipher.encrypt(resp_json.encode(), use_base64=False), retcode=0)))

# Push STATUS (cmd 8) z urządzenia: retcode 0 + nagłówek "3.3"+12 + szyfrogram
push_json = json.dumps({"devId": DEV_ID, "dps": {"1": "stop", "2": 40}, "t": 1700000001}, separators=(",", ":"))
out.append(c_string("VEC_PUSH_JSON", push_json))
out.append(c_array("vec_push_frame",
                   frame(0, 8, tinytuya.PROTOCOL_33_HEADER + cipher.encrypt(push_json.encode(), use_base64=False), retcode=0)))

# Broadcast discovery (UDP 6667): klucz md5("yGAdlopoPVldABfn"), retcode 0
udp_key = hashlib.md5(b"yGAdlopoPVldABfn").digest()
disc_json = json.dumps({"ip": "192.168.0.242", "gwId": DEV_ID, "active": 2, "encrypt": True,
                        "productKey": "keyj979nf3q3theh", "version": "3.3"}, separators=(",", ":"))
out.append(c_string("VEC_DISC_JSON", disc_json))
out.append(c_array("vec_disc_frame",
                   frame(0, 19, tinytuya.AESCipher(udp_key).encrypt(disc_json.encode(), use_base64=False), retcode=0)))

print("\n".join(out))
