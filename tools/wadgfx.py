#!/usr/bin/env python3

"""
Dependency-free Doom WAD graphics reader.

This module is intended for DoomCube's player-facing packer as well as
developer tooling.  It deliberately depends only on the Python standard
library.

Supported primitives:

    - bounds-checked IWAD/PWAD directory parsing
    - Doom load-order lump resolution
    - PLAYPAL palette extraction
    - Doom patch-column graphics decoding
    - 24-bit BMP output

Later WADs override earlier WADs when resolving a lump, matching the
normal Doom resource-loading model used by DoomCube's launcher assets.
"""

from __future__ import annotations

import argparse
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence


MAX_PATCH_DIMENSION = 4096
MAX_PATCH_PIXELS = 16 * 1024 * 1024

PALETTE_COLORS = 256
PALETTE_BYTES = PALETTE_COLORS * 3


class WadError(RuntimeError):
    """Raised when a WAD or Doom graphic is malformed."""


@dataclass(frozen=True)
class Lump:
    index: int
    name: str
    offset: int
    size: int


@dataclass
class Patch:
    width: int
    height: int
    left_offset: int
    top_offset: int
    pixels: bytearray
    opaque: bytearray

    def opaque_pixels(self) -> int:
        return sum(self.opaque)


class WadFile:
    def __init__(self, path: Path | str):
        self.path = Path(path)

        try:
            self.data = self.path.read_bytes()
        except OSError as exc:
            raise WadError(
                f"cannot read WAD {self.path}: {exc}"
            ) from exc

        self.magic: str
        self.lumps: list[Lump]

        self.magic, self.lumps = self._read_directory()

    def _read_directory(self) -> tuple[str, list[Lump]]:
        data = self.data

        if len(data) < 12:
            raise WadError(
                f"{self.path}: file is smaller than the WAD header"
            )

        magic_raw, lump_count, directory_offset = (
            struct.unpack_from("<4sII", data, 0)
        )

        if magic_raw not in (b"IWAD", b"PWAD"):
            raise WadError(
                f"{self.path}: invalid WAD magic {magic_raw!r}"
            )

        directory_size = lump_count * 16
        directory_end = directory_offset + directory_size

        if directory_offset > len(data):
            raise WadError(
                f"{self.path}: directory offset is outside file"
            )

        if directory_end > len(data):
            raise WadError(
                f"{self.path}: WAD directory extends outside file"
            )

        lumps: list[Lump] = []

        for index in range(lump_count):
            entry_offset = directory_offset + index * 16

            file_offset, size, raw_name = struct.unpack_from(
                "<II8s",
                data,
                entry_offset,
            )

            name = (
                raw_name
                .split(b"\0", 1)[0]
                .decode("ascii", errors="replace")
                .upper()
            )

            if file_offset > len(data):
                raise WadError(
                    f"{self.path}: lump {index} {name!r} "
                    "starts outside file"
                )

            if size > len(data) - file_offset:
                raise WadError(
                    f"{self.path}: lump {index} {name!r} "
                    "extends outside file"
                )

            lumps.append(
                Lump(
                    index=index,
                    name=name,
                    offset=file_offset,
                    size=size,
                )
            )

        return magic_raw.decode("ascii"), lumps

    def find_last(self, name: str) -> Lump | None:
        wanted = name.upper()

        for lump in reversed(self.lumps):
            if lump.name == wanted:
                return lump

        return None

    def lump_data(self, lump: Lump) -> bytes:
        return self.data[
            lump.offset:
            lump.offset + lump.size
        ]


def resolve_lump(
    wads: Sequence[WadFile],
    name: str,
) -> tuple[WadFile, Lump]:
    """
    Resolve a lump using Doom-style load order.

    Later WADs override matching lumps in earlier WADs.
    """

    for wad in reversed(wads):
        lump = wad.find_last(name)

        if lump is not None:
            return wad, lump

    raise WadError(
        f"lump {name.upper()!r} was not found in supplied WADs"
    )


def read_palette(
    data: bytes,
    palette_index: int = 0,
) -> list[tuple[int, int, int]]:
    if palette_index < 0:
        raise WadError("palette index cannot be negative")

    start = palette_index * PALETTE_BYTES
    end = start + PALETTE_BYTES

    if end > len(data):
        available = len(data) // PALETTE_BYTES

        raise WadError(
            "PLAYPAL does not contain requested palette "
            f"{palette_index}; available complete palettes: "
            f"{available}"
        )

    palette = []

    for offset in range(start, end, 3):
        palette.append(
            (
                data[offset],
                data[offset + 1],
                data[offset + 2],
            )
        )

    return palette


def resolve_palette(
    wads: Sequence[WadFile],
    palette_index: int = 0,
) -> tuple[WadFile, list[tuple[int, int, int]]]:
    wad, lump = resolve_lump(wads, "PLAYPAL")

    palette = read_palette(
        wad.lump_data(lump),
        palette_index,
    )

    return wad, palette


def decode_patch(data: bytes) -> Patch:
    """
    Decode a Doom patch graphic.

    Pixels are returned row-major.  `opaque` contains 1 for pixels
    explicitly present in patch posts and 0 for transparent holes.

    Tall-patch relative topdelta behaviour is accepted as well, even
    though DoomCube's current TITLEPIC/STCFN assets do not require it.
    """

    if len(data) < 8:
        raise WadError(
            "patch is smaller than its 8-byte header"
        )

    width, height, left_offset, top_offset = (
        struct.unpack_from("<HHhh", data, 0)
    )

    if width == 0 or height == 0:
        raise WadError(
            f"invalid patch dimensions {width}x{height}"
        )

    if (
        width > MAX_PATCH_DIMENSION
        or height > MAX_PATCH_DIMENSION
    ):
        raise WadError(
            f"patch dimensions {width}x{height} exceed safety limit"
        )

    pixel_count = width * height

    if pixel_count > MAX_PATCH_PIXELS:
        raise WadError(
            f"patch has too many pixels: {pixel_count}"
        )

    column_table_end = 8 + width * 4

    if column_table_end > len(data):
        raise WadError(
            "patch column-offset table extends outside lump"
        )

    pixels = bytearray(pixel_count)
    opaque = bytearray(pixel_count)

    column_offsets = struct.unpack_from(
        f"<{width}I",
        data,
        8,
    )

    for x, column_offset in enumerate(column_offsets):
        if column_offset >= len(data):
            raise WadError(
                f"patch column {x} starts outside lump "
                f"at offset {column_offset}"
            )

        pos = column_offset
        previous_top = -1
        saw_terminator = False

        while pos < len(data):
            top_delta = data[pos]
            pos += 1

            if top_delta == 0xFF:
                saw_terminator = True
                break

            if pos + 2 > len(data):
                raise WadError(
                    f"patch column {x}: truncated post header"
                )

            length = data[pos]
            pos += 1

            # Doom patch format contains one unused byte here.
            pos += 1

            if pos + length + 1 > len(data):
                raise WadError(
                    f"patch column {x}: post data extends "
                    "outside lump"
                )

            if (
                previous_top >= 0
                and top_delta <= previous_top
            ):
                absolute_top = previous_top + top_delta
            else:
                absolute_top = top_delta

            previous_top = absolute_top

            if absolute_top + length > height:
                raise WadError(
                    f"patch column {x}: post rows "
                    f"{absolute_top}.."
                    f"{absolute_top + length - 1} exceed "
                    f"height {height}"
                )

            for row in range(length):
                y = absolute_top + row
                destination = y * width + x

                pixels[destination] = data[pos + row]
                opaque[destination] = 1

            pos += length

            # Doom patch format contains another unused byte.
            pos += 1

        if not saw_terminator:
            raise WadError(
                f"patch column {x}: missing 0xFF terminator"
            )

    return Patch(
        width=width,
        height=height,
        left_offset=left_offset,
        top_offset=top_offset,
        pixels=pixels,
        opaque=opaque,
    )


def write_bmp(
    path: Path | str,
    patch: Patch,
    palette: Sequence[tuple[int, int, int]],
    background_index: int = 0,
) -> None:
    """
    Write a Doom patch as an ordinary uncompressed 24-bit BMP.

    Transparent holes are filled from background_index.  TITLEPIC is
    normally fully opaque; later font-atlas generation can make its own
    explicit transparency treatment without changing this decoder.
    """

    output = Path(path)

    if len(palette) != PALETTE_COLORS:
        raise WadError(
            f"expected {PALETTE_COLORS} palette colours; "
            f"got {len(palette)}"
        )

    if not 0 <= background_index < PALETTE_COLORS:
        raise WadError(
            f"invalid background palette index {background_index}"
        )

    row_bytes = patch.width * 3
    row_stride = (row_bytes + 3) & ~3
    image_size = row_stride * patch.height
    file_size = 54 + image_size

    bmp = bytearray(file_size)

    struct.pack_into(
        "<2sIHHI",
        bmp,
        0,
        b"BM",
        file_size,
        0,
        0,
        54,
    )

    struct.pack_into(
        "<IIIHHIIIIII",
        bmp,
        14,
        40,
        patch.width,
        patch.height,
        1,
        24,
        0,
        image_size,
        2835,
        2835,
        0,
        0,
    )

    background = palette[background_index]

    for bmp_row in range(patch.height):
        source_y = patch.height - 1 - bmp_row
        row_start = 54 + bmp_row * row_stride

        for x in range(patch.width):
            source = source_y * patch.width + x

            if patch.opaque[source]:
                colour = palette[patch.pixels[source]]
            else:
                colour = background

            red, green, blue = colour
            destination = row_start + x * 3

            bmp[destination] = blue
            bmp[destination + 1] = green
            bmp[destination + 2] = red

    output.parent.mkdir(
        parents=True,
        exist_ok=True,
    )

    output.write_bytes(bmp)


def load_wads(paths: Iterable[Path]) -> list[WadFile]:
    wads = [WadFile(path) for path in paths]

    if not wads:
        raise WadError("at least one WAD is required")

    return wads


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Extract a Doom patch lump through Doom-style WAD "
            "load order and write it as a 24-bit BMP."
        )
    )

    parser.add_argument(
        "--wad",
        action="append",
        type=Path,
        required=True,
        help=(
            "WAD in load order; may be supplied multiple times. "
            "Later WADs override earlier ones."
        ),
    )

    parser.add_argument(
        "--lump",
        required=True,
        help="patch lump to extract, for example TITLEPIC",
    )

    parser.add_argument(
        "--output",
        type=Path,
        required=True,
        help="output BMP path",
    )

    parser.add_argument(
        "--palette-index",
        type=int,
        default=0,
        help="PLAYPAL palette number, default 0",
    )

    parser.add_argument(
        "--background-index",
        type=int,
        default=0,
        help=(
            "palette index used for transparent patch holes, "
            "default 0"
        ),
    )

    args = parser.parse_args()

    try:
        wads = load_wads(args.wad)

        lump_wad, lump = resolve_lump(
            wads,
            args.lump,
        )

        palette_wad, palette = resolve_palette(
            wads,
            args.palette_index,
        )

        patch = decode_patch(
            lump_wad.lump_data(lump)
        )

        write_bmp(
            args.output,
            patch,
            palette,
            args.background_index,
        )

    except WadError as exc:
        print(
            f"ERROR: {exc}",
            file=__import__("sys").stderr,
        )

        return 1

    print(
        f"Lump     : {args.lump.upper()} "
        f"from {lump_wad.path}"
    )

    print(
        f"Palette  : PLAYPAL[{args.palette_index}] "
        f"from {palette_wad.path}"
    )

    print(
        f"Patch    : {patch.width}x{patch.height} "
        f"offset=({patch.left_offset},{patch.top_offset}) "
        f"opaque={patch.opaque_pixels()}/"
        f"{patch.width * patch.height}"
    )

    print(
        f"Output   : {args.output}"
    )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
