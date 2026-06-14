#!/usr/bin/env python3
import argparse
import struct
from pathlib import Path


HEADER_FORMAT = "<QIIIIQQQQIIQIIQIIQIIQIIQQQQ"
SIGNATURE_FORMAT = "<QIIIIQQQQ"
CERTIFICATE_FORMAT = "<QII32s"

DRV_MAGIC = 0x5652443436534F
DRV_SIGNATURE_MAGIC = 0x314749533436534F
DRV_CERTIFICATE_MAGIC = 0x315452433436534F
DRV_SIGNATURE_VERSION = 1
DRV_SIGNATURE_HASH_CHECKSUM64 = 1
DRV_SIGNATURE_ALG_LOCAL_TEST = 1
DRV_SIGNATURE_ALG_ROOT_KEY = 2
DRV_SIGNATURE_ALG_TPM_LOCAL = 3
DRV_CERTIFICATE_TYPE_LOCAL_TEST = 1
DRV_CERTIFICATE_TYPE_ROOT_KEY = 2
DRV_CERTIFICATE_TYPE_TPM_LOCAL = 3

ALGORITHMS = {
    "local-test": (DRV_SIGNATURE_ALG_LOCAL_TEST, DRV_CERTIFICATE_TYPE_LOCAL_TEST, "OS64 local test key"),
    "root-key": (DRV_SIGNATURE_ALG_ROOT_KEY, DRV_CERTIFICATE_TYPE_ROOT_KEY, "OS64 root key placeholder"),
    "tpm-local": (DRV_SIGNATURE_ALG_TPM_LOCAL, DRV_CERTIFICATE_TYPE_TPM_LOCAL, "OS64 TPM local placeholder"),
}


def zstr(text: str, size: int) -> bytes:
    data = text.encode("ascii")
    if len(data) >= size:
        raise SystemExit(f"{text}: string is too long for {size} bytes")
    return data + bytes(size - len(data))


def checksum64(data: bytes) -> int:
    value = 0xCBF29CE484222325
    for byte in data:
        value ^= byte
        value = (value * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def patch_header(image: bytearray, file_size: int, signature_offset: int, signature_size: int, certificate_offset: int, certificate_size: int) -> None:
    header = list(struct.unpack_from(HEADER_FORMAT, image, 0))
    header[5] = file_size
    header[23] = signature_offset
    header[24] = signature_size
    header[25] = certificate_offset
    header[26] = certificate_size
    struct.pack_into(HEADER_FORMAT, image, 0, *header)


def sign_image(unsigned: bytes, algorithm_name: str) -> bytes:
    if len(unsigned) < struct.calcsize(HEADER_FORMAT):
        raise SystemExit("input is too small")
    header = struct.unpack_from(HEADER_FORMAT, unsigned, 0)
    if header[0] != DRV_MAGIC:
        raise SystemExit("input is not a .drv image")
    if header[23] != 0 or header[24] != 0 or header[25] != 0 or header[26] != 0:
        raise SystemExit("input is already signed")
    if header[5] != len(unsigned):
        raise SystemExit("input file size does not match header")

    if algorithm_name not in ALGORITHMS:
        raise SystemExit(f"unknown signature algorithm: {algorithm_name}")
    algorithm, certificate_type, key_id = ALGORITHMS[algorithm_name]

    certificate = struct.pack(
        CERTIFICATE_FORMAT,
        DRV_CERTIFICATE_MAGIC,
        DRV_SIGNATURE_VERSION,
        certificate_type,
        zstr(key_id, 32),
    )
    signature_size = struct.calcsize(SIGNATURE_FORMAT)
    signature_offset = len(unsigned)
    certificate_offset = signature_offset + signature_size
    file_size = certificate_offset + len(certificate)

    signed_prefix = bytearray(unsigned)
    patch_header(signed_prefix, file_size, signature_offset, signature_size, certificate_offset, len(certificate))
    signed_hash = checksum64(bytes(signed_prefix))
    certificate_hash = checksum64(certificate)
    signature_value = signed_hash ^ certificate_hash ^ file_size ^ algorithm
    signature = struct.pack(
        SIGNATURE_FORMAT,
        DRV_SIGNATURE_MAGIC,
        DRV_SIGNATURE_VERSION,
        algorithm,
        DRV_SIGNATURE_HASH_CHECKSUM64,
        0,
        signature_offset,
        signed_hash,
        signature_value,
        0,
    )
    return bytes(signed_prefix) + signature + certificate


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--algorithm", default="local-test", choices=sorted(ALGORITHMS.keys()))
    args = parser.parse_args()

    signed = sign_image(Path(args.input).read_bytes(), args.algorithm)
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(signed)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
