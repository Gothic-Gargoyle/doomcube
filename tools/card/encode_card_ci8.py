#!/usr/bin/env python3

"""
Build DoomCube's GameCube Memory Card presentation data.

The GameCube CARD manager expects:

    CI8 banner:
        96 x 32
        3072-byte tiled image
        512-byte RGB5A3 palette

    CI8 icon:
        32 x 32
        1024-byte tiled image
        shared 512-byte RGB5A3 palette

    comment block:
        2 x 32 bytes

gxtexconv v1.0.6 currently segfaults in its CI8 path on the
development host, so this encoder deliberately implements only
the tiny deterministic subset required for CARD presentation.

CI8 GX tiles are 8 x 4 pixels.
"""

from pathlib import Path
from PIL import Image
import argparse
import hashlib
import struct


BANNER_W = 96
BANNER_H = 32

ICON_W = 32
ICON_H = 32

BANNER_IMAGE_SIZE = 3072
ICON_IMAGE_SIZE = 1024
PALETTE_SIZE = 512

COMMENT_LINE_SIZE = 32
COMMENT_SIZE = 64

PRESENTATION_SIZE = (
    BANNER_IMAGE_SIZE +
    PALETTE_SIZE +
    ICON_IMAGE_SIZE +
    PALETTE_SIZE +
    COMMENT_SIZE
)


def round_bits(value, bits):
    maximum = (1 << bits) - 1
    return (value * maximum + 127) // 255


def expand_bits(value, bits):
    maximum = (1 << bits) - 1
    return (value * 255 + maximum // 2) // maximum


def encode_rgb5a3(r, g, b, a=255):
    if a >= 224:
        return (
            0x8000 |
            (round_bits(r, 5) << 10) |
            (round_bits(g, 5) << 5) |
            round_bits(b, 5)
        )

    return (
        (round_bits(a, 3) << 12) |
        (round_bits(r, 4) << 8) |
        (round_bits(g, 4) << 4) |
        round_bits(b, 4)
    )


def decode_rgb5a3(value):
    if value & 0x8000:
        return (
            expand_bits((value >> 10) & 0x1f, 5),
            expand_bits((value >> 5) & 0x1f, 5),
            expand_bits(value & 0x1f, 5),
            255,
        )

    return (
        expand_bits((value >> 8) & 0x0f, 4),
        expand_bits((value >> 4) & 0x0f, 4),
        expand_bits(value & 0x0f, 4),
        expand_bits((value >> 12) & 0x07, 3),
    )


def quantize(image):
    return image.convert("RGB").quantize(
        colors=256,
        method=Image.Quantize.MEDIANCUT,
        dither=Image.Dither.NONE,
    )


def get_palette(q):
    raw = list(q.getpalette() or [])

    raw.extend(
        [0] * max(0, 768 - len(raw))
    )

    result = []

    for index in range(256):
        result.append(
            encode_rgb5a3(
                raw[index * 3 + 0],
                raw[index * 3 + 1],
                raw[index * 3 + 2],
                255,
            )
        )

    return result


def encode_palette(palette):
    data = b"".join(
        struct.pack(">H", value)
        for value in palette
    )

    if len(data) != PALETTE_SIZE:
        raise RuntimeError(
            f"palette encoded to {len(data)} bytes"
        )

    return data


def encode_ci8(q):
    width, height = q.size

    if width % 8:
        raise RuntimeError(
            f"CI8 width {width} is not divisible by 8"
        )

    if height % 4:
        raise RuntimeError(
            f"CI8 height {height} is not divisible by 4"
        )

    pixels = q.load()
    out = bytearray()

    for tile_y in range(0, height, 4):
        for tile_x in range(0, width, 8):
            for y in range(4):
                for x in range(8):
                    out.append(
                        pixels[
                            tile_x + x,
                            tile_y + y
                        ]
                    )

    if len(out) != width * height:
        raise RuntimeError(
            "CI8 encoded length is wrong"
        )

    return bytes(out)


def decode_ci8(data, width, height):
    result = [0] * (width * height)

    pos = 0

    for tile_y in range(0, height, 4):
        for tile_x in range(0, width, 8):
            for y in range(4):
                for x in range(8):
                    dst = (
                        (tile_y + y) * width +
                        tile_x + x
                    )

                    result[dst] = data[pos]
                    pos += 1

    return result


def make_asset(image, expected_size):
    q = quantize(image)

    texture = encode_ci8(q)

    if len(texture) != expected_size:
        raise RuntimeError(
            f"texture size {len(texture)} "
            f"!= expected {expected_size}"
        )

    original_indices = list(q.get_flattened_data())
    decoded_indices = decode_ci8(
        texture,
        q.width,
        q.height,
    )

    if original_indices != decoded_indices:
        raise RuntimeError(
            "CI8 tile round-trip mismatch"
        )

    palette_values = get_palette(q)
    palette = encode_palette(palette_values)

    return texture, palette, q, palette_values


def comment_line(text):
    encoded = text.encode("ascii")

    if len(encoded) >= COMMENT_LINE_SIZE:
        encoded = encoded[
            :COMMENT_LINE_SIZE - 1
        ]

    return (
        encoded +
        bytes(
            COMMENT_LINE_SIZE -
            len(encoded)
        )
    )


def emit_header(path):
    path.write_text(
        """\
#ifndef DOOMCUBE_GC_CARD_PRESENTATION_DATA_H
#define DOOMCUBE_GC_CARD_PRESENTATION_DATA_H

#define GC_CARD_PRESENTATION_DATA_SIZE 5184u

extern const unsigned char
    gc_card_presentation_data[
        GC_CARD_PRESENTATION_DATA_SIZE
    ];

#endif
""",
        encoding="ascii",
    )


def emit_source(path, header_name, data):
    lines = []

    for offset in range(0, len(data), 16):
        chunk = data[offset:offset + 16]

        lines.append(
            "    " +
            ", ".join(
                f"0x{value:02x}"
                for value in chunk
            ) +
            ","
        )

    path.write_text(
        f"""\
/*
 * GENERATED FILE.
 *
 * Generated by tools/card/encode_card_ci8.py.
 * Do not hand-edit.
 */

#include "{header_name}"

const unsigned char
    gc_card_presentation_data[
        GC_CARD_PRESENTATION_DATA_SIZE
    ] =
{{
{chr(10).join(lines)}
}};
""",
        encoding="ascii",
    )


def preview_rgb(q, palette_values):
    image = Image.new(
        "RGB",
        q.size,
    )

    src = q.load()
    dst = image.load()

    for y in range(q.height):
        for x in range(q.width):
            r, g, b, a = decode_rgb5a3(
                palette_values[
                    src[x, y]
                ]
            )

            dst[x, y] = (r, g, b)

    return image


def main():
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--input",
        required=True,
    )

    parser.add_argument(
        "--output-c",
        required=True,
    )

    parser.add_argument(
        "--output-h",
        required=True,
    )

    parser.add_argument(
        "--preview-dir",
    )

    args = parser.parse_args()

    source = Path(args.input)
    output_c = Path(args.output_c)
    output_h = Path(args.output_h)

    output_c.parent.mkdir(
        parents=True,
        exist_ok=True,
    )

    output_h.parent.mkdir(
        parents=True,
        exist_ok=True,
    )

    with Image.open(source) as raw:
        original = raw.convert("RGB")

        icon_source = original.resize(
            (ICON_W, ICON_H),
            Image.Resampling.LANCZOS,
        )

        # Keep the existing square DoomCube artwork undistorted:
        # centre a 32x32 copy in the 96x32 CARD banner.
        banner_source = Image.new(
            "RGB",
            (BANNER_W, BANNER_H),
            (0, 0, 0),
        )

        banner_logo = original.resize(
            (32, 32),
            Image.Resampling.LANCZOS,
        )

        banner_source.paste(
            banner_logo,
            (32, 0),
        )

    (
        banner_texture,
        banner_palette,
        banner_q,
        banner_palette_values,
    ) = make_asset(
        banner_source,
        BANNER_IMAGE_SIZE,
    )

    (
        icon_texture,
        icon_palette,
        icon_q,
        icon_palette_values,
    ) = make_asset(
        icon_source,
        ICON_IMAGE_SIZE,
    )

    comments = (
        comment_line("DoomCube") +
        comment_line("DOOM save data")
    )

    data = (
        banner_texture +
        banner_palette +
        icon_texture +
        icon_palette +
        comments
    )

    if len(data) != PRESENTATION_SIZE:
        raise RuntimeError(
            f"presentation size {len(data)} "
            f"!= {PRESENTATION_SIZE}"
        )

    emit_header(output_h)

    emit_source(
        output_c,
        output_h.name,
        data,
    )

    if args.preview_dir:
        preview_dir = Path(
            args.preview_dir
        )

        preview_dir.mkdir(
            parents=True,
            exist_ok=True,
        )

        banner_source.save(
            preview_dir /
            "card-banner-source.png"
        )

        icon_source.save(
            preview_dir /
            "card-icon-source.png"
        )

        preview_rgb(
            banner_q,
            banner_palette_values,
        ).save(
            preview_dir /
            "card-banner-encoded.png"
        )

        preview_rgb(
            icon_q,
            icon_palette_values,
        ).save(
            preview_dir /
            "card-icon-encoded.png"
        )

        (
            preview_dir /
            "doomcube-card-presentation.bin"
        ).write_bytes(data)

    print(
        "DoomCube CARD presentation generated"
    )
    print(
        f"  banner CI8       : "
        f"{len(banner_texture)} bytes"
    )
    print(
        f"  banner palette   : "
        f"{len(banner_palette)} bytes"
    )
    print(
        f"  icon CI8         : "
        f"{len(icon_texture)} bytes"
    )
    print(
        f"  icon palette     : "
        f"{len(icon_palette)} bytes"
    )
    print(
        f"  comments         : "
        f"{len(comments)} bytes"
    )
    print(
        f"  presentation     : "
        f"{len(data)} bytes"
    )
    print(
        f"  sha256           : "
        f"{hashlib.sha256(data).hexdigest()}"
    )
    print(
        f"  C source         : {output_c}"
    )
    print(
        f"  header           : {output_h}"
    )


if __name__ == "__main__":
    main()
