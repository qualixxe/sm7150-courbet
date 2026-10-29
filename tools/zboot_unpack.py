#!/usr/bin/env python3
"""Turn whatever a distro publishes as its arm64 kernel into a plain arm64 Image.

QEMU's -kernel wants an uncompressed arm64 Image. It can decompress a gzip
stream, and QEMU 8.2 and later can also unpack an EFI zboot container by
themselves, but a bundle that depends on either has to be run by a QEMU new
enough to do it, and the failure looks like a silent reset rather than a
missing file. So the conversion happens here, once, at build time.

Alpine, Debian and Fedora now publish zboot: the file starts with "MZ", and
the payload is a gzip stream somewhere inside it. Which somewhere is not
guessed - the zboot header states it:

    0x00 msdos_magic[2]       "MZ"
    0x04 zimg[4]              "zimg"
    0x08 payload_offset (le32)
    0x0c payload_size  (le32)
    0x10 reserved[8]
    0x18 compression_type[32] "gzip"
    0x38 linux_magic[4]       cd 23 82 81
    0x3c pe_header_offset(le32)

which is struct linux_efi_zboot_header in QEMU's hw/core/loader.c, itself
based on Linux's drivers/firmware/efi/libstub/zboot-header.S.

Exit is non-zero unless the output really is an arm64 Image, so a wrong
assumption cannot produce a bundle that fails on the phone.
"""

import argparse
import gzip
import sys

ARM64_MAGIC = b"ARM\x64"
ARM64_MAGIC_OFFSET = 0x38
ZIMG = b"zimg"
ZBOOT_LINUX_MAGIC = bytes.fromhex("cd238281")
GZIP_MAGIC = b"\x1f\x8b"


def u32le(data, off):
    return int.from_bytes(data[off:off + 4], "little")


def is_image(data):
    return len(data) > ARM64_MAGIC_OFFSET + 4 and data[ARM64_MAGIC_OFFSET:ARM64_MAGIC_OFFSET + 4] == ARM64_MAGIC


def zboot_payload(data):
    """Return (offset, size) of the gzip payload, or None if this is not zboot."""
    if len(data) < 0x40:
        return None
    if data[0:2] != b"MZ" or data[4:8] != ZIMG:
        return None
    if data[ARM64_MAGIC_OFFSET:ARM64_MAGIC_OFFSET + 4] != ZBOOT_LINUX_MAGIC:
        return None
    comp = data[0x18:0x1c]
    if comp != b"gzip":
        raise SystemExit("zboot payload is %r; only gzip is handled here" % comp)
    off, size = u32le(data, 0x08), u32le(data, 0x0c)
    if off + size > len(data):
        raise SystemExit("corrupt zboot header: payload %d+%d beyond %d bytes"
                         % (off, size, len(data)))
    return off, size


def to_image(data, name):
    where = zboot_payload(data)
    if where:
        off, size = where
        print("%s: EFI zboot, payload at %d, %d compressed bytes" % (name, off, size))
        return gzip.decompress(data[off:off + size])
    if data[:2] == GZIP_MAGIC:
        print("%s: gzip stream, decompressing" % name)
        return gzip.decompress(data)
    if is_image(data):
        print("%s: already an arm64 Image" % name)
        return data
    raise SystemExit("%s: not an arm64 Image, not gzip and not EFI zboot "
                     "(first bytes %s)" % (name, data[:4].hex()))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("-o", "--output", required=True,
                    help="may be the same as the input")
    a = ap.parse_args()

    with open(a.input, "rb") as fh:
        data = fh.read()

    image = to_image(data, a.input)
    if not is_image(image):
        raise SystemExit("result has no ARM64 magic at 0x38 (%s there), refusing to write"
                         % image[ARM64_MAGIC_OFFSET:ARM64_MAGIC_OFFSET + 4].hex())

    with open(a.output, "wb") as fh:
        fh.write(image)

    text_offset = int.from_bytes(image[8:16], "little")
    image_size = int.from_bytes(image[16:24], "little")
    print("%s: arm64 Image, %d bytes, text_offset %d, image_size %d"
          % (a.output, len(image), text_offset, image_size))
    return 0


if __name__ == "__main__":
    sys.exit(main())
