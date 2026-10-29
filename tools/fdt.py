#!/usr/bin/env python3
"""Read a flattened device tree without libfdt.

COURBET.md tells the reader to run this, and until now there was nothing to
run. It is a parser, not a library: the FDT format is a header, a block of
node/property records and a block of strings, which is small enough to read
directly and means the answer does not depend on what is installed.

    python tools/fdt.py courbet.dtb --node /            # one node, all props
    python tools/fdt.py courbet.dtb --find touch        # any path containing
    python tools/fdt.py courbet.dtb --tree --depth 2    # shape of the tree
"""

import argparse
import struct
import sys

FDT_MAGIC = 0xD00DFEED
FDT_BEGIN_NODE, FDT_END_NODE, FDT_PROP, FDT_NOP, FDT_END = 1, 2, 3, 4, 9


class Fdt:
    def __init__(self, blob):
        magic, totalsize, off_struct, off_strings, _, _, _, _, _ = (
            struct.unpack_from(">9I", blob, 0))
        if magic != FDT_MAGIC:
            raise SystemExit("not a device tree: magic %08x" % magic)
        self.blob = blob
        self.strings = off_strings
        self.root = self._parse(off_struct, totalsize)

    def _string(self, off):
        end = self.blob.index(b"\0", self.strings + off)
        return self.blob[self.strings + off:end].decode("utf-8", "replace")

    def _parse(self, off, totalsize):
        # The structure block starts with BEGIN_NODE for the root itself, so the
        # stack starts empty and the root is whatever that first record opens;
        # seeding one here instead produces a phantom child named "".
        top = []
        stack = []
        while off < totalsize:
            tag, = struct.unpack_from(">I", self.blob, off)
            off += 4
            if tag == FDT_BEGIN_NODE:
                end = self.blob.index(b"\0", off)
                name = self.blob[off:end].decode("utf-8", "replace")
                off = (end + 4) & ~3
                node = (name, {}, [])
                (stack[-1][2] if stack else top).append(node)
                stack.append(node)
            elif tag == FDT_END_NODE:
                stack.pop()
            elif tag == FDT_PROP:
                length, nameoff = struct.unpack_from(">II", self.blob, off)
                off += 8
                stack[-1][1][self._string(nameoff)] = self.blob[off:off + length]
                off = (off + length + 3) & ~3
            elif tag == FDT_NOP:
                continue
            elif tag == FDT_END:
                break
            else:
                raise SystemExit("bad tag %d at %d" % (tag, off - 4))
        return top[0]

    def walk(self, node=None, path=""):
        node = node or self.root
        name = node[0].strip("/")
        here = path.rstrip("/") + "/" + name if path else "/" + name
        if not here:
            here = "/"
        yield here, node
        for child in node[2]:
            yield from self.walk(child, here)


def cells(blob):
    return " ".join(str(x) for x in struct.unpack(">%dI" % (len(blob) // 4), blob[:len(blob) // 4 * 4]))


def show(props, indent="  "):
    for name in sorted(props):
        value = props[name]
        if not value:
            print("%s%s" % (indent, name))
            continue
        text = None
        if len(value) == 1 and value[0] == 0:
            text = "<empty>"
        elif len(value) % 4 == 0 and len(value) <= 64 and not _printable(value):
            text = "<%s>" % cells(value)
        if text is None:
            if _printable(value):
                text = '"%s"' % value.rstrip(b"\0").decode("utf-8", "replace")
            else:
                text = "<%s>" % value.hex()
        print("%s%s = %s" % (indent, name, text))


def _printable(value):
    return all(32 <= b < 127 or b == 0 for b in value) and b"\0" not in value[:-1]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("dtb")
    ap.add_argument("--node", help="print one node by exact path")
    ap.add_argument("--find", help="print every node whose path contains this")
    ap.add_argument("--tree", action="store_true", help="print paths only")
    ap.add_argument("--depth", type=int, default=99)
    args = ap.parse_args()

    with open(args.dtb, "rb") as handle:
        tree = Fdt(handle.read())

    nodes = list(tree.walk())
    if args.node:
        for path, node in nodes:
            if path == args.node:
                print(path)
                show(node[1])
                return
        raise SystemExit("no node %s" % args.node)

    if args.find:
        hits = 0
        for path, node in nodes:
            if args.find in path:
                hits += 1
                print(path)
                show(node[1])
        if not hits:
            raise SystemExit("nothing matches %r" % args.find)
        return

    for path, _ in nodes:
        if path.count("/") - 1 <= args.depth:
            print(path)


if __name__ == "__main__":
    sys.exit(main())
