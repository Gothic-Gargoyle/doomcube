#!/usr/bin/env python3

from __future__ import annotations

import argparse
import struct
from pathlib import Path


BOOT_BIN_SIZE = 0x440
BI2_SIZE = 0x2000

APPLOADER_OFFSET = 0x2440
APPLOADER_HEADER_SIZE = 0x20

GC_MAGIC = 0xC2339F3D

ALIGN = 0x20

PWAD_MANIFEST_NAME = "doomcube.lst"


#
# GameCube BI2 region field.
#
# This is disc/application identity metadata, not a request to force
# a particular runtime video mode.
#
GC_REGIONS = {
    "NTSC-J": 0,
    "NTSC-U": 1,
    "PAL": 2,
}

def align(value: int, alignment: int = ALIGN) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def be32(buf: bytearray, off: int, value: int) -> None:
    struct.pack_into(">I", buf, off, value)


class Node:
    def __init__(self, name: str, path: Path, is_dir: bool):
        self.name = name
        self.path = path
        self.is_dir = is_dir

        self.parent_index = 0
        self.next_index = 0

        self.name_offset = 0

        self.file_offset = 0
        self.file_size = 0

        self.index = -1

        self.children: list[Node] = []


def write_pwad_manifest(root: Path) -> None:
    """
    Generate the PWAD list consumed by the DoomCube launcher.

    The launcher does not need directory-enumeration support from the
    GameCube DVD filesystem: it can read this ordinary file instead.

    Manifest format:
        one PWAD filename per UTF-8 line

    Only top-level .wad files in data/pwad are listed.
    """
    pwad_dir = root / "data" / "pwad"

    if not pwad_dir.is_dir():
        return

    pwads = sorted(
        (
            entry.name
            for entry in pwad_dir.iterdir()
            if entry.is_file()
            and entry.suffix.lower() == ".wad"
            and "\n" not in entry.name
            and "\r" not in entry.name
        ),
        key=str.casefold,
    )

    manifest = pwad_dir / PWAD_MANIFEST_NAME

    manifest.write_text(
        "".join(f"{name}\n" for name in pwads),
        encoding="utf-8",
    )

    print(
        f"PWAD manifest : {len(pwads)} file(s)"
    )


def build_tree(root: Path) -> Node:
    node = Node("", root, True)

    def walk(parent: Node) -> None:
        entries = sorted(
            parent.path.iterdir(),
            key=lambda p: (not p.is_dir(), p.name.lower())
        )

        for p in entries:
            child = Node(
                p.name,
                p,
                p.is_dir()
            )

            parent.children.append(child)

            if child.is_dir:
                walk(child)

    walk(node)
    return node


def flatten_tree(root: Node) -> list[Node]:
    """
    GameCube FST directories use:
      field 2 = parent entry index
      field 3 = index immediately after all descendants.
    """
    entries: list[Node] = []

    def emit(node: Node, parent_index: int) -> None:
        node.index = len(entries)
        node.parent_index = parent_index
        entries.append(node)

        if node.is_dir:
            for child in node.children:
                emit(child, node.index)

            node.next_index = len(entries)

    emit(root, 0)

    root.parent_index = 0
    root.next_index = len(entries)

    return entries


def build_string_table(entries: list[Node]) -> bytes:
    table = bytearray(b"\0")

    for node in entries[1:]:
        node.name_offset = len(table)

        encoded = node.name.encode("utf-8")

        if len(encoded) == 0:
            raise ValueError("empty FST filename")

        if len(table) >= 0x01000000:
            raise ValueError("FST string table exceeds 24-bit name offset")

        table += encoded
        table += b"\0"

    return bytes(table)


def make_boot_bin(
    title: str,
    dol_offset: int,
    fst_offset: int,
    fst_size: int,
    fst_max_size: int,
    user_position: int,
    user_length: int,
) -> bytes:

    b = bytearray(BOOT_BIN_SIZE)

    # Game ID:
    #   4-byte game code + 2-byte maker code.
    b[0x000:0x006] = b"GDOE01"

    # Disc number / version.
    b[0x006] = 0
    b[0x007] = 0

    # Audio streaming disabled.
    b[0x008] = 0
    b[0x009] = 0

    be32(b, 0x01C, GC_MAGIC)

    title_bytes = title.encode(
        "ascii",
        errors="replace"
    )[:0x3DF]

    b[0x020:0x020 + len(title_bytes)] = title_bytes
    b[0x020 + len(title_bytes)] = 0

    # Normal GameCube disc layout fields.
    be32(b, 0x420, dol_offset)
    be32(b, 0x424, fst_offset)
    be32(b, 0x428, fst_size)
    be32(b, 0x42C, fst_max_size)

    be32(b, 0x434, user_position)
    be32(b, 0x438, user_length)

    return bytes(b)


def make_bi2(region: int) -> bytes:
    """
    Build the GameCube BI2 block.

    DoomCube's homebrew apploader does not require most Nintendo SDK
    BI2 metadata, so the unused fields remain zero.

    BI2 + 0x18 is the GameCube region field:

        0 = NTSC-J / Japan
        1 = NTSC-U / USA
        2 = PAL / Europe

    The region identifies the disc to the platform/emulator.  It does
    not force DoomCube's runtime video mode.
    """

    if region not in GC_REGIONS.values():
        raise ValueError(
            f"invalid GameCube region value: {region}"
        )

    b = bytearray(BI2_SIZE)

    be32(
        b,
        0x18,
        region
    )

    return bytes(b)


def make_apploader_image(binary: bytes) -> bytes:
    """
    Native GameCube apploader area:

        0x00  build date string[16]
        0x10  entry point
        0x14  code size
        0x18  trailer size
        0x1c  reserved
        0x20  apploader machine code

    Our cubeboot-derived apploader is linked at 0x81200000.
    """
    header = bytearray(APPLOADER_HEADER_SIZE)

    date = b"2026/08/25"
    header[0:len(date)] = date

    be32(header, 0x10, 0x81200000)
    be32(header, 0x14, len(binary))
    be32(header, 0x18, 0)
    be32(header, 0x1C, 0)

    return bytes(header) + binary


def write_fst(
    entries: list[Node],
    strings: bytes,
) -> bytes:

    out = bytearray()

    for node in entries:
        if node.is_dir:
            type_name = (
                0x01000000 |
                (node.name_offset & 0x00FFFFFF)
            )

            out += struct.pack(
                ">III",
                type_name,
                node.parent_index,
                node.next_index
            )
        else:
            type_name = (
                node.name_offset &
                0x00FFFFFF
            )

            out += struct.pack(
                ">III",
                type_name,
                node.file_offset,
                node.file_size
            )

    out += strings

    return bytes(out)


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Build a native GameCube DoomCube GCM"
    )

    ap.add_argument(
        "--dol",
        required=True,
        type=Path
    )

    ap.add_argument(
        "--apploader",
        required=True,
        type=Path
    )

    ap.add_argument(
        "--root",
        required=True,
        type=Path
    )

    ap.add_argument(
        "--output",
        required=True,
        type=Path
    )

    ap.add_argument(
        "--title",
        default="DOOMCUBE"
    )

    ap.add_argument(
        "--region",
        choices=tuple(GC_REGIONS),
        default="PAL",
        help=(
            "GameCube disc region written to BI2 "
            "(default: PAL)"
        ),
    )

    args = ap.parse_args()

    dol = args.dol.read_bytes()
    app = args.apploader.read_bytes()

    if not args.root.is_dir():
        raise SystemExit(
            f"ERROR: root directory missing: {args.root}"
        )

    write_pwad_manifest(args.root)

    root = build_tree(args.root)
    entries = flatten_tree(root)
    strings = build_string_table(entries)

    apploader_image = make_apploader_image(app)

    #
    # Native system area
    #

    app_end = (
        APPLOADER_OFFSET +
        len(apploader_image)
    )

    dol_offset = align(app_end, 0x100)

    fst_entry_bytes = len(entries) * 12
    fst_size = fst_entry_bytes + len(strings)

    fst_offset = align(
        dol_offset + len(dol),
        ALIGN
    )

    #
    # Data files follow the FST.
    #

    data_offset = align(
        fst_offset + fst_size,
        ALIGN
    )

    current = data_offset

    for node in entries:
        if node.is_dir:
            continue

        node.file_offset = current
        node.file_size = node.path.stat().st_size

        current = align(
            current + node.file_size,
            ALIGN
        )

    fst = write_fst(
        entries,
        strings
    )

    # Sanity: offsets didn't change FST size.
    if len(fst) != fst_size:
        raise SystemExit(
            "internal error: FST size changed"
        )

    user_position = data_offset
    user_length = current - data_offset

    boot = make_boot_bin(
        title=args.title,
        dol_offset=dol_offset,
        fst_offset=fst_offset,
        fst_size=len(fst),
        fst_max_size=len(fst),
        user_position=user_position,
        user_length=user_length,
    )

    region_value = GC_REGIONS[
        args.region
    ]

    bi2 = make_bi2(
        region_value
    )

    image_size = current

    image = bytearray(image_size)

    #
    # System area.
    #

    image[0:BOOT_BIN_SIZE] = boot

    image[
        BOOT_BIN_SIZE:
        BOOT_BIN_SIZE + BI2_SIZE
    ] = bi2

    image[
        APPLOADER_OFFSET:
        APPLOADER_OFFSET + len(apploader_image)
    ] = apploader_image

    #
    # Main executable.
    #

    image[
        dol_offset:
        dol_offset + len(dol)
    ] = dol

    #
    # Native GameCube FST.
    #

    image[
        fst_offset:
        fst_offset + len(fst)
    ] = fst

    #
    # User files.
    #

    for node in entries:
        if node.is_dir:
            continue

        data = node.path.read_bytes()

        if len(data) != node.file_size:
            raise SystemExit(
                f"ERROR: file changed while building: "
                f"{node.path}"
            )

        start = node.file_offset
        end = start + len(data)

        image[start:end] = data

    args.output.parent.mkdir(
        parents=True,
        exist_ok=True
    )

    args.output.write_bytes(image)

    print()
    print("DoomCube native GCM built")
    print("=========================")
    print(f"output       : {args.output}")
    print(
        f"region       : {args.region} "
        f"(BI2={region_value})"
    )
    print(f"size         : {len(image):,} bytes")
    print(f"apploader    : 0x{APPLOADER_OFFSET:08x}")
    print(f"DOL offset   : 0x{dol_offset:08x}")
    print(f"DOL size     : {len(dol):,}")
    print(f"FST offset   : 0x{fst_offset:08x}")
    print(f"FST size     : {len(fst):,}")
    print(f"FST entries  : {len(entries)}")
    print(f"file data    : 0x{data_offset:08x}")
    print()

    for node in entries:
        if node.is_dir:
            continue

        rel = node.path.relative_to(args.root)

        print(
            f"  {node.file_offset:08x} "
            f"{node.file_size:10d} "
            f"{rel}"
        )


if __name__ == "__main__":
    main()
