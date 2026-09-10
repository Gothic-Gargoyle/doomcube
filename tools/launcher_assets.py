#!/usr/bin/env python3

"""
Generate DoomCube launcher assets from the WADs staged for a disc.

This tool intentionally operates on the disposable disc-staging tree.
It never writes generated Doom artwork into the source tree or release
bundle.

Individual artwork failures are non-fatal.  DoomCube must remain able
to build and launch even when optional launcher artwork is unavailable
or malformed.

Dependencies: Python standard library + sibling wadgfx.py.
"""

from __future__ import annotations

import argparse
import struct
import wave
from dataclasses import dataclass
from pathlib import Path

import wadgfx


TITLEPIC_WIDTH = 320
TITLEPIC_HEIGHT = 200

#
# Doom's STCFN small-font patches are at most 9x8 in the IWADs
# currently supported by DoomCube.
#
# Give every glyph a one-pixel transparent gutter inside a fixed cell.
# This keeps the atlas trivial to consume and prevents neighbouring
# glyph pixels from being sampled when the texture is scaled.
#
FONT_ATLAS_COLUMNS = 16
FONT_CELL_WIDTH = 12
FONT_CELL_HEIGHT = 10
FONT_CELL_PADDING = 1

FONT_CHROMA_KEY = (255, 0, 255)
FONT_BACKGROUND_INDEX = 0

FONT_SPACE_ADVANCE = 4
FONT_LINE_HEIGHT = 9

#
# 33..95 are Doom's ordinary small-font character range.
# STCFN121 is also present in every currently supported IWAD, so retain
# it in the generated atlas rather than silently throwing source data
# away.
#
FONT_CODES = tuple(range(33, 96)) + (121,)

#
# Launcher text is deliberately sourced only from an IWAD, never from
# PWAD override data.  Prefer the normal DOOM family when available and
# fall back deterministically for discs containing only another game.
#
FONT_SOURCE_PRIORITY = (
    ("DOOM", "data/wad/doom.wad"),
    ("DOOM II", "data/wad/doom2.wad"),
    ("TNT: EVILUTION", "data/wad/tnt.wad"),
    ("PLUTONIA", "data/wad/plutonia.wad"),
    ("DOOM SHAREWARE", "data/wad/doom1.wad"),
)


@dataclass(frozen=True)
class Campaign:
    key: str
    label: str
    iwad: str
    pwad: str | None = None


CAMPAIGNS = (
    Campaign(
        key="doom1",
        label="DOOM SHAREWARE",
        iwad="data/wad/doom1.wad",
    ),
    Campaign(
        key="doom",
        label="DOOM",
        iwad="data/wad/doom.wad",
    ),
    Campaign(
        key="doom2",
        label="DOOM II",
        iwad="data/wad/doom2.wad",
    ),
    Campaign(
        key="tnt",
        label="TNT: EVILUTION",
        iwad="data/wad/tnt.wad",
    ),
    Campaign(
        key="plutonia",
        label="PLUTONIA",
        iwad="data/wad/plutonia.wad",
    ),
    Campaign(
        key="sigil",
        label="SIGIL",
        iwad="data/wad/doom.wad",
        pwad="data/pwad/doom/SIGIL_V1_23.wad",
    ),
    Campaign(
        key="sigil2",
        label="SIGIL II",
        iwad="data/wad/doom.wad",
        pwad="data/pwad/doom/SIGIL_II_V1_0.WAD",
    ),
)


#
# Launcher audio is derived only into the disposable disc-staging tree.
#
# Music stays in its original WAD lump representation here. The GameCube
# launcher can later apply the same MUS/MIDI handling used by DoomCube's
# already-proven music backend instead of maintaining a second converter
# in the player-facing Python packer.
#
LAUNCHER_CAMPAIGN_MUSIC_LUMPS = {
    "doom1": "D_E1M1",
    "doom": "D_E1M1",
    "doom2": "D_RUNNIN",
    "tnt": "D_RUNNIN",
    "plutonia": "D_RUNNIN",
    "sigil": "D_E5M1",
    "sigil2": "D_E6M1",
}


#
# Prefer the ordinary full DOOM family for shared launcher presentation
# when several IWADs are packed. CUSTOM is a launcher-level entry rather
# than a campaign, so give it one deterministic intermission track.
#
LAUNCHER_CUSTOM_INTERMISSION_SOURCES = (
    ("DOOM", "data/wad/doom.wad", "D_INTER"),
    ("DOOM II", "data/wad/doom2.wad", "D_DM2INT"),
    ("TNT: EVILUTION", "data/wad/tnt.wad", "D_DM2INT"),
    ("PLUTONIA", "data/wad/plutonia.wad", "D_DM2INT"),
    ("DOOM SHAREWARE", "data/wad/doom1.wad", "D_INTER"),
)


LAUNCHER_CYBSIT_SOURCES = (
    ("DOOM", "data/wad/doom.wad"),
    ("DOOM II", "data/wad/doom2.wad"),
    ("TNT: EVILUTION", "data/wad/tnt.wad"),
    ("PLUTONIA", "data/wad/plutonia.wad"),
    ("DOOM SHAREWARE", "data/wad/doom1.wad"),
)


def warn(message: str) -> None:
    print(f"[WARN] {message}")


def info(message: str) -> None:
    print(f"[--] {message}")


def generate_campaign(
    root: Path,
    output_dir: Path,
    campaign: Campaign,
) -> bool:
    iwad = root / campaign.iwad

    output = output_dir / f"{campaign.key}.bmp"

    # Avoid ever leaving a stale generated file behind if this helper
    # is rerun against a persistent staging tree.
    if output.exists():
        output.unlink()

    if not iwad.is_file():
        info(
            f"Launcher art {campaign.label}: skipped; "
            f"missing {campaign.iwad}"
        )
        return False

    wad_paths = [iwad]

    if campaign.pwad is not None:
        pwad = root / campaign.pwad

        if not pwad.is_file():
            info(
                f"Launcher art {campaign.label}: skipped; "
                f"missing {campaign.pwad}"
            )
            return False

        wad_paths.append(pwad)

    try:
        wads = [
            wadgfx.WadFile(path)
            for path in wad_paths
        ]

        graphic_wad, graphic_lump = (
            wadgfx.resolve_lump(
                wads,
                "TITLEPIC",
            )
        )

        palette_wad, palette = (
            wadgfx.resolve_palette(
                wads,
                0,
            )
        )

        patch = wadgfx.decode_patch(
            graphic_wad.lump_data(
                graphic_lump
            )
        )

        if (
            patch.width != TITLEPIC_WIDTH
            or patch.height != TITLEPIC_HEIGHT
        ):
            raise wadgfx.WadError(
                "TITLEPIC has unsupported dimensions "
                f"{patch.width}x{patch.height}; "
                f"expected "
                f"{TITLEPIC_WIDTH}x{TITLEPIC_HEIGHT}"
            )

        wadgfx.write_bmp(
            output,
            patch,
            palette,
        )

    except (
        OSError,
        ValueError,
        wadgfx.WadError,
    ) as exc:
        warn(
            f"Launcher art {campaign.label} unavailable: "
            f"{exc}; runtime fallback will be used"
        )
        return False

    print(
        f"[OK] Launcher art {campaign.label}"
    )
    print(
        f"     TITLEPIC: {graphic_wad.path}"
    )
    print(
        f"     PLAYPAL : {palette_wad.path}"
    )
    print(
        f"     Output  : "
        f"{output.relative_to(root).as_posix()}"
    )

    return True


def select_font_source(
    root: Path,
) -> tuple[str, Path] | None:
    for label, relative in FONT_SOURCE_PRIORITY:
        path = root / relative

        if path.is_file():
            return label, path

    return None


def generate_font(root: Path) -> bool:
    output_dir = root / "launcher" / "font"
    atlas_path = output_dir / "doomfont.bmp"
    metrics_path = output_dir / "doomfont.txt"

    output_dir.mkdir(
        parents=True,
        exist_ok=True,
    )

    #
    # Never retain stale generated assets if this helper is rerun
    # against a persistent developer staging directory.
    #
    for path in (atlas_path, metrics_path):
        if path.exists():
            path.unlink()

    selected = select_font_source(root)

    if selected is None:
        info(
            "Launcher Doom font: skipped; "
            "no supported base IWAD is present"
        )
        return False

    source_label, source_path = selected

    try:
        wad = wadgfx.WadFile(source_path)

        palette_wad, palette = wadgfx.resolve_palette(
            [wad],
            0,
        )

        glyphs = []

        for code in FONT_CODES:
            name = f"STCFN{code:03d}"
            lump = wad.find_last(name)

            if lump is None:
                raise wadgfx.WadError(
                    f"required font lump {name} is missing"
                )

            patch = wadgfx.decode_patch(
                wad.lump_data(lump)
            )

            if (
                patch.width >
                    FONT_CELL_WIDTH
                    - 2 * FONT_CELL_PADDING
                or patch.height >
                    FONT_CELL_HEIGHT
                    - 2 * FONT_CELL_PADDING
            ):
                raise wadgfx.WadError(
                    f"{name} is too large for font cell: "
                    f"{patch.width}x{patch.height}"
                )

            #
            # Palette index 0 is reserved as the atlas background.
            # The supported IWAD STCFN graphics do not use it.
            #
            for pixel, opaque in zip(
                patch.pixels,
                patch.opaque,
            ):
                if (
                    opaque
                    and pixel == FONT_BACKGROUND_INDEX
                ):
                    raise wadgfx.WadError(
                        f"{name} uses reserved font "
                        f"background palette index "
                        f"{FONT_BACKGROUND_INDEX}"
                    )

            glyphs.append(
                (code, patch)
            )

        rows = (
            len(glyphs)
            + FONT_ATLAS_COLUMNS
            - 1
        ) // FONT_ATLAS_COLUMNS

        atlas_width = (
            FONT_ATLAS_COLUMNS
            * FONT_CELL_WIDTH
        )

        atlas_height = (
            rows
            * FONT_CELL_HEIGHT
        )

        atlas_pixels = bytearray(
            atlas_width * atlas_height
        )

        atlas_opaque = bytearray(
            atlas_width * atlas_height
        )

        metrics = [
            "DOOMCUBE_FONT_V1",
            f"atlas {atlas_width} {atlas_height}",
            (
                "key "
                f"{FONT_CHROMA_KEY[0]} "
                f"{FONT_CHROMA_KEY[1]} "
                f"{FONT_CHROMA_KEY[2]}"
            ),
            f"line_height {FONT_LINE_HEIGHT}",
            f"space {FONT_SPACE_ADVANCE}",
            (
                "source "
                + source_path.relative_to(root).as_posix()
            ),
        ]

        for ordinal, (code, patch) in enumerate(glyphs):
            cell_x = (
                ordinal % FONT_ATLAS_COLUMNS
            ) * FONT_CELL_WIDTH

            cell_y = (
                ordinal // FONT_ATLAS_COLUMNS
            ) * FONT_CELL_HEIGHT

            glyph_x = (
                cell_x
                + FONT_CELL_PADDING
            )

            glyph_y = (
                cell_y
                + FONT_CELL_PADDING
            )

            for y in range(patch.height):
                for x in range(patch.width):
                    source = (
                        y * patch.width
                        + x
                    )

                    if not patch.opaque[source]:
                        continue

                    destination = (
                        (glyph_y + y)
                        * atlas_width
                        + glyph_x
                        + x
                    )

                    atlas_pixels[destination] = (
                        patch.pixels[source]
                    )

                    atlas_opaque[destination] = 1

            #
            # Doom patch origin semantics are intentionally retained.
            # A runtime renderer should place the bitmap at:
            #
            #   draw_x = pen_x - left_offset
            #   draw_y = pen_y - top_offset
            #
            # This is particularly important for punctuation such as
            # '.', ',' and '_' whose negative top offsets move them
            # downward relative to ordinary letters.
            #
            advance = patch.width + 1

            metrics.append(
                "glyph "
                f"{code} "
                f"{glyph_x} "
                f"{glyph_y} "
                f"{patch.width} "
                f"{patch.height} "
                f"{patch.left_offset} "
                f"{patch.top_offset} "
                f"{advance}"
            )

        atlas = wadgfx.Patch(
            width=atlas_width,
            height=atlas_height,
            left_offset=0,
            top_offset=0,
            pixels=atlas_pixels,
            opaque=atlas_opaque,
        )

        #
        # Reuse wadgfx's already-proven 24-bit BMP writer.
        #
        # Index zero is unused by the opaque STCFN pixels, so replace
        # that palette entry with an unmistakable RGB chroma key.
        #
        #
        # Preserve the original Doom PLAYPAL colours of every opaque
        # STCFN glyph pixel.  Only the reserved background index is
        # replaced with the magenta transparency key.
        #
        # This keeps the native red/orange shading and internal detail
        # from the WAD instead of flattening each glyph to a white mask.
        #
        atlas_palette = list(palette)

        atlas_palette[FONT_BACKGROUND_INDEX] = (
            FONT_CHROMA_KEY
        )

        wadgfx.write_bmp(
            atlas_path,
            atlas,
            atlas_palette,
            FONT_BACKGROUND_INDEX,
        )

        metrics_path.write_text(
            "\n".join(metrics) + "\n",
            encoding="ascii",
        )

    except (
        OSError,
        ValueError,
        wadgfx.WadError,
    ) as exc:
        for path in (atlas_path, metrics_path):
            if path.exists():
                path.unlink()

        warn(
            "Launcher Doom font unavailable: "
            f"{exc}; runtime built-in font fallback "
            "will be used"
        )

        return False

    print("[OK] Launcher Doom font")
    print(f"     Source  : {source_label}")
    print(f"     IWAD    : {source_path}")
    print(f"     PLAYPAL : {palette_wad.path}")
    print(
        "     Atlas   : "
        f"{atlas_path.relative_to(root).as_posix()} "
        f"({atlas_width}x{atlas_height})"
    )
    print(
        "     Metrics : "
        f"{metrics_path.relative_to(root).as_posix()} "
        f"({len(glyphs)} glyphs)"
    )

    return True


def launcher_music_format(data: bytes) -> str | None:
    if len(data) >= 4 and data[:4] == b"MUS\x1a":
        return "MUS"

    if len(data) >= 4 and data[:4] == b"MThd":
        return "MIDI"

    return None


def resolve_raw_lump(
    wad_paths: list[Path],
    lump_name: str,
):
    loaded = [
        wadgfx.WadFile(path)
        for path in wad_paths
    ]

    for wad in reversed(loaded):
        lump = wad.find_last(lump_name)

        if lump is not None:
            return wad, lump, wad.lump_data(lump)

    raise wadgfx.WadError(
        f"lump {lump_name!r} not found in "
        + ", ".join(str(path) for path in wad_paths)
    )


def generate_campaign_music(
    root: Path,
    output_dir: Path,
    campaign: Campaign,
) -> bool:
    output = output_dir / f"{campaign.key}.lmp"

    if output.exists():
        output.unlink()

    lump_name = LAUNCHER_CAMPAIGN_MUSIC_LUMPS[campaign.key]

    iwad = root / campaign.iwad

    if not iwad.is_file():
        info(
            f"Launcher music {campaign.label}: skipped; "
            f"missing {campaign.iwad}"
        )
        return False

    wad_paths = [iwad]

    if campaign.pwad is not None:
        pwad = root / campaign.pwad

        if not pwad.is_file():
            info(
                f"Launcher music {campaign.label}: skipped; "
                f"missing {campaign.pwad}"
            )
            return False

        wad_paths.append(pwad)

    try:
        source_wad, _lump, data = resolve_raw_lump(
            wad_paths,
            lump_name,
        )

        music_format = launcher_music_format(data)

        if music_format is None:
            raise wadgfx.WadError(
                f"{lump_name} is neither MUS nor MIDI"
            )

        output.write_bytes(data)

    except (
        OSError,
        ValueError,
        wadgfx.WadError,
    ) as exc:
        if output.exists():
            output.unlink()

        warn(
            f"Launcher music {campaign.label} unavailable: {exc}"
        )
        return False

    print()
    print(f"[OK] Launcher music {campaign.label}")
    print(f"     Lump    : {lump_name}")
    print(f"     Source  : {source_wad.path}")
    print(f"     Format  : {music_format}")
    print(f"     Bytes   : {len(data)}")
    print(
        "     Output  : "
        f"{output.relative_to(root).as_posix()}"
    )

    return True


def generate_custom_intermission_music(
    root: Path,
    output_dir: Path,
) -> bool:
    output = output_dir / "custom.lmp"

    if output.exists():
        output.unlink()

    for label, relative, lump_name in (
        LAUNCHER_CUSTOM_INTERMISSION_SOURCES
    ):
        source = root / relative

        if not source.is_file():
            continue

        try:
            wad = wadgfx.WadFile(source)
            lump = wad.find_last(lump_name)

            if lump is None:
                continue

            data = wad.lump_data(lump)
            music_format = launcher_music_format(data)

            if music_format is None:
                raise wadgfx.WadError(
                    f"{lump_name} is neither MUS nor MIDI"
                )

            output.write_bytes(data)

        except (
            OSError,
            ValueError,
            wadgfx.WadError,
        ) as exc:
            warn(
                f"Launcher CUSTOM intermission unavailable "
                f"from {relative}: {exc}"
            )
            continue

        print()
        print("[OK] Launcher music CUSTOM")
        print(f"     Source  : {label}")
        print(f"     IWAD    : {source}")
        print(f"     Lump    : {lump_name}")
        print(f"     Format  : {music_format}")
        print(f"     Bytes   : {len(data)}")
        print(
            "     Output  : "
            f"{output.relative_to(root).as_posix()}"
        )

        return True

    info(
        "Launcher music CUSTOM: skipped; "
        "no supported IWAD provides an intermission track"
    )

    return False


def decode_dmx_sound(data: bytes) -> tuple[int, bytes]:
    if len(data) < 8:
        raise wadgfx.WadError(
            "DMX sound lump is shorter than its 8-byte header"
        )

    if data[0:2] != b"\x03\x00":
        raise wadgfx.WadError(
            "DMX sound lump does not have type 3 header"
        )

    sample_rate, declared_length = struct.unpack_from(
        "<HI",
        data,
        2,
    )

    if sample_rate <= 0:
        raise wadgfx.WadError(
            "DMX sound lump has invalid sample rate"
        )

    if (
        declared_length > len(data) - 8
        or declared_length <= 48
    ):
        raise wadgfx.WadError(
            "DMX sound lump has invalid declared sample length"
        )

    #
    # Match Chocolate Doom's DMX compatibility path exactly:
    #
    #     data   += 16
    #     length -= 32
    #
    # This intentionally preserves the same trimming behavior used by
    # DoomCube's normal SFX loader.
    #
    pcm_start = 16
    pcm_length = declared_length - 32
    pcm_end = pcm_start + pcm_length

    if pcm_end > len(data):
        raise wadgfx.WadError(
            "trimmed DMX PCM range extends outside lump"
        )

    return sample_rate, data[pcm_start:pcm_end]


def generate_cybsit_wav(
    root: Path,
    output_dir: Path,
) -> bool:
    output = output_dir / "cybsit.wav"

    if output.exists():
        output.unlink()

    for label, relative in LAUNCHER_CYBSIT_SOURCES:
        source = root / relative

        if not source.is_file():
            continue

        try:
            wad = wadgfx.WadFile(source)
            lump = wad.find_last("DSCYBSIT")

            if lump is None:
                continue

            sample_rate, pcm = decode_dmx_sound(
                wad.lump_data(lump)
            )

            with wave.open(str(output), "wb") as wav:
                wav.setnchannels(1)
                wav.setsampwidth(1)
                wav.setframerate(sample_rate)
                wav.writeframes(pcm)

        except (
            OSError,
            ValueError,
            wave.Error,
            wadgfx.WadError,
        ) as exc:
            if output.exists():
                output.unlink()

            warn(
                f"Launcher DSCYBSIT unavailable "
                f"from {relative}: {exc}"
            )
            continue

        print()
        print("[OK] Launcher Cyberdemon ident sound")
        print(f"     Source  : {label}")
        print(f"     IWAD    : {source}")
        print("     Lump    : DSCYBSIT")
        print(f"     Rate    : {sample_rate} Hz")
        print(f"     PCM     : {len(pcm)} bytes")
        print(
            "     Output  : "
            f"{output.relative_to(root).as_posix()}"
        )

        return True

    info(
        "Launcher Cyberdemon ident sound: skipped; "
        "no supported IWAD provides DSCYBSIT"
    )

    return False


def generate_launcher_audio(root: Path) -> tuple[int, int, bool]:
    audio_dir = root / "launcher" / "audio"
    music_dir = root / "launcher" / "music"

    audio_dir.mkdir(
        parents=True,
        exist_ok=True,
    )

    music_dir.mkdir(
        parents=True,
        exist_ok=True,
    )

    generated = 0

    for campaign in CAMPAIGNS:
        if generate_campaign_music(
            root,
            music_dir,
            campaign,
        ):
            generated += 1

    custom_generated = generate_custom_intermission_music(
        root,
        music_dir,
    )

    if custom_generated:
        generated += 1

    roar_generated = generate_cybsit_wav(
        root,
        audio_dir,
    )

    expected = len(CAMPAIGNS) + 1

    print()
    print(
        "Launcher music: "
        f"{generated} generated, "
        f"{expected - generated} skipped"
    )

    print(
        "Launcher ident audio: "
        + (
            "generated"
            if roar_generated
            else "not generated"
        )
    )

    return generated, expected - generated, roar_generated


def generate_launcher_assets(root: Path) -> tuple[int, int]:
    output_dir = (
        root
        / "launcher"
        / "titlepic"
    )

    output_dir.mkdir(
        parents=True,
        exist_ok=True,
    )

    generated = 0

    for campaign in CAMPAIGNS:
        if generate_campaign(
            root,
            output_dir,
            campaign,
        ):
            generated += 1

    skipped = len(CAMPAIGNS) - generated

    print()
    print(
        "Launcher TITLEPIC art: "
        f"{generated} generated, "
        f"{skipped} skipped"
    )

    print()

    font_generated = generate_font(root)

    print()
    print(
        "Launcher Doom font: "
        + (
            "generated"
            if font_generated
            else "not generated"
        )
    )

    generate_splash_collage(root)
    generate_doom_menu_logo(root)
    generate_doom_menu_skull(root)
    generate_doom_memcard_art(root)
    generate_splash_cube(root)
    generate_launcher_audio(root)

    return generated, skipped



SPLASH_DARKEN_PERCENT = 28
DOOM_MENU_LOGO_KEY = (255, 0, 255)

DOOM_MENU_LOGO_SOURCES = (
    ("DOOM", "data/wad/doom.wad"),
    ("DOOM SHAREWARE", "data/wad/doom1.wad"),
    ("DOOM II", "data/wad/doom2.wad"),
    ("TNT: EVILUTION", "data/wad/tnt.wad"),
    ("PLUTONIA", "data/wad/plutonia.wad"),
)


def patch_rows(
    patch: wadgfx.Patch,
) -> list[list[int | None]]:
    # Normalize wadgfx.Patch.pixels to row-major [y][x] form
    # without assuming the decoder's internal storage layout.

    pixels = patch.pixels
    width = patch.width
    height = patch.height
    outer = len(pixels)

    first = pixels[0] if outer else None

    nested = False

    if first is not None:
        try:
            len(first)
            nested = not isinstance(
                first,
                (str, bytes, bytearray),
            )
        except TypeError:
            nested = False

    if not nested and outer == width * height:
        return [
            list(
                pixels[
                    y * width:
                    (y + 1) * width
                ]
            )
            for y in range(height)
        ]

    if nested and outer == height:
        if all(
            len(row) == width
            for row in pixels
        ):
            return [
                list(row)
                for row in pixels
            ]

    if nested and outer == width:
        if all(
            len(column) == height
            for column in pixels
        ):
            return [
                [
                    pixels[x][y]
                    for x in range(width)
                ]
                for y in range(height)
            ]

    raise wadgfx.WadError(
        "unsupported decoded patch pixel layout: "
        f"outer={outer}, "
        f"width={width}, "
        f"height={height}"
    )


def read_bmp24(
    path: Path,
) -> tuple[int, int, list[list[tuple[int, int, int]]]]:
    data = path.read_bytes()

    if data[:2] != b"BM":
        raise ValueError(
            f"not a BMP: {path}"
        )

    pixel_offset = struct.unpack_from(
        "<I",
        data,
        10,
    )[0]

    width = struct.unpack_from(
        "<i",
        data,
        18,
    )[0]

    signed_height = struct.unpack_from(
        "<i",
        data,
        22,
    )[0]

    bpp = struct.unpack_from(
        "<H",
        data,
        28,
    )[0]

    compression = struct.unpack_from(
        "<I",
        data,
        30,
    )[0]

    if (
        width <= 0
        or signed_height == 0
        or bpp != 24
        or compression != 0
    ):
        raise ValueError(
            f"unsupported BMP format: {path}"
        )

    height = abs(signed_height)
    bottom_up = signed_height > 0
    stride = ((width * 3) + 3) & ~3
    rows: list[list[tuple[int, int, int]]] = []

    for output_y in range(height):
        source_y = (
            height - 1 - output_y
            if bottom_up
            else output_y
        )

        row_start = (
            pixel_offset
            + source_y * stride
        )

        row: list[tuple[int, int, int]] = []

        for x in range(width):
            pos = row_start + x * 3
            blue, green, red = data[
                pos:pos + 3
            ]

            row.append(
                (red, green, blue)
            )

        rows.append(row)

    return width, height, rows


def write_bmp24(
    path: Path,
    pixels: list[list[tuple[int, int, int]]],
) -> None:
    if not pixels or not pixels[0]:
        raise ValueError(
            "cannot write an empty BMP"
        )

    height = len(pixels)
    width = len(pixels[0])

    if any(
        len(row) != width
        for row in pixels
    ):
        raise ValueError(
            "BMP rows have inconsistent widths"
        )

    row_bytes = width * 3
    stride = (row_bytes + 3) & ~3
    image_size = stride * height
    pixel_offset = 54
    file_size = pixel_offset + image_size

    header = (
        b"BM"
        + struct.pack(
            "<IHHI",
            file_size,
            0,
            0,
            pixel_offset,
        )
        + struct.pack(
            "<IIIHHIIIIII",
            40,
            width,
            height,
            1,
            24,
            0,
            image_size,
            2835,
            2835,
            0,
            0,
        )
    )

    body = bytearray()

    for y in range(height - 1, -1, -1):
        row = bytearray()

        for red, green, blue in pixels[y]:
            row.extend(
                (blue, green, red)
            )

        row.extend(
            b"\x00" * (stride - row_bytes)
        )

        body.extend(row)

    path.parent.mkdir(
        parents=True,
        exist_ok=True,
    )

    path.write_bytes(
        header + body
    )


def generate_splash_collage(root: Path) -> bool:
    output = root / "launcher" / "splash.bmp"

    if output.exists():
        output.unlink()

    available: list[
        tuple[str, list[list[tuple[int, int, int]]]]
    ] = []

    full_doom_present = (
        root
        / "launcher"
        / "titlepic"
        / "doom.bmp"
    ).is_file()

    for campaign in CAMPAIGNS:
        if (
            campaign.key == "doom1"
            and full_doom_present
        ):
            info(
                "Launcher splash: suppressing DOOM SHAREWARE "
                "because full DOOM is present"
            )
            continue

        titlepic = (
            root
            / "launcher"
            / "titlepic"
            / f"{campaign.key}.bmp"
        )

        if not titlepic.is_file():
            continue

        try:
            width, height, pixels = read_bmp24(
                titlepic
            )
        except (
            OSError,
            ValueError,
        ) as exc:
            warn(
                "Launcher splash: ignoring "
                f"{titlepic}: {exc}"
            )
            continue

        if (
            width != TITLEPIC_WIDTH
            or height != TITLEPIC_HEIGHT
        ):
            warn(
                "Launcher splash: ignoring "
                f"{titlepic}; unexpected "
                f"{width}x{height}"
            )
            continue

        available.append(
            (campaign.label, pixels)
        )

    if not available:
        info(
            "Launcher splash collage: skipped; "
            "no generated TITLEPICs"
        )
        return False

    result = [
        [(0, 0, 0)] * TITLEPIC_WIDTH
        for _ in range(TITLEPIC_HEIGHT)
    ]

    count = len(available)

    for index, (label, pixels) in enumerate(
        available
    ):
        destination_y0 = (
            index
            * TITLEPIC_HEIGHT
            // count
        )

        destination_y1 = (
            (index + 1)
            * TITLEPIC_HEIGHT
            // count
        )

        band_height = (
            destination_y1
            - destination_y0
        )

        source_y0 = (
            TITLEPIC_HEIGHT
            - band_height
        ) // 2

        for offset in range(band_height):
            source_row = pixels[
                source_y0 + offset
            ]

            destination_row = result[
                destination_y0 + offset
            ]

            for x in range(TITLEPIC_WIDTH):
                red, green, blue = source_row[x]

                destination_row[x] = (
                    red
                    * SPLASH_DARKEN_PERCENT
                    // 100,
                    green
                    * SPLASH_DARKEN_PERCENT
                    // 100,
                    blue
                    * SPLASH_DARKEN_PERCENT
                    // 100,
                )

        print(
            "[OK] Splash band "
            f"{index + 1}/{count}: "
            f"{label} "
            f"({band_height}px)"
        )

    write_bmp24(
        output,
        result,
    )

    print()
    print("[OK] Launcher splash collage")
    print(
        f"     Sources : {count}"
    )
    print(
        f"     Layout  : horizontal bands"
    )
    print(
        f"     Size    : "
        f"{TITLEPIC_WIDTH}x{TITLEPIC_HEIGHT}"
    )
    print(
        f"     Bright  : "
        f"{SPLASH_DARKEN_PERCENT}%"
    )
    print(
        "     Output  : "
        f"{output.relative_to(root).as_posix()}"
    )

    return True


def key_border_connected_dark_pixels(
    pixels: list[list[tuple[int, int, int]]],
    *,
    threshold: int,
    key: tuple[int, int, int],
) -> tuple[list[list[tuple[int, int, int]]], int]:
    # Chroma-key only dark pixels connected to the bitmap border.
    # This removes the rectangular black canvas around splash logos
    # while preserving enclosed dark shading/detail in the artwork.

    height = len(pixels)
    width = len(pixels[0])

    transparent = [
        [False] * width
        for _ in range(height)
    ]

    queue: list[tuple[int, int]] = []

    def is_background(
        color: tuple[int, int, int],
    ) -> bool:
        red, green, blue = color

        return max(
            red,
            green,
            blue,
        ) <= threshold

    def seed(x: int, y: int) -> None:
        if transparent[y][x]:
            return

        if not is_background(
            pixels[y][x]
        ):
            return

        transparent[y][x] = True
        queue.append((x, y))

    for x in range(width):
        seed(x, 0)
        seed(x, height - 1)

    for y in range(height):
        seed(0, y)
        seed(width - 1, y)

    read_index = 0

    while read_index < len(queue):
        x, y = queue[read_index]
        read_index += 1

        if x > 0:
            seed(x - 1, y)

        if x + 1 < width:
            seed(x + 1, y)

        if y > 0:
            seed(x, y - 1)

        if y + 1 < height:
            seed(x, y + 1)

    output = [
        list(row)
        for row in pixels
    ]

    count = 0

    for y in range(height):
        for x in range(width):
            if transparent[y][x]:
                output[y][x] = key
                count += 1

    return output, count


def generate_doom_menu_logo(root: Path) -> bool:
    output = root / "launcher" / "m_doom.bmp"

    if output.exists():
        output.unlink()

    for label, relative in DOOM_MENU_LOGO_SOURCES:
        iwad = root / relative

        if not iwad.is_file():
            continue

        try:
            wad = wadgfx.WadFile(iwad)
            lump = wad.find_last("M_DOOM")

            if lump is None:
                continue

            _palette_wad, palette = (
                wadgfx.resolve_palette(
                    [wad],
                    0,
                )
            )

            patch = wadgfx.decode_patch(
                wad.lump_data(lump)
            )

            source_pixels = [
                [
                    palette[index]
                    for index in row
                ]
                for row in patch_rows(patch)
            ]

            pixels, transparent_count = (
                key_border_connected_dark_pixels(
                    source_pixels,
                    threshold=64,
                    key=DOOM_MENU_LOGO_KEY,
                )
            )

            write_bmp24(
                output,
                pixels,
            )

        except (
            OSError,
            ValueError,
            wadgfx.WadError,
        ) as exc:
            warn(
                "Launcher M_DOOM unavailable "
                f"from {relative}: {exc}"
            )
            continue

        print()
        print("[OK] Launcher M_DOOM")
        print(f"     Source  : {label}")
        print(f"     IWAD    : {iwad}")
        print(
            f"     Size    : "
            f"{patch.width}x{patch.height}"
        )
        print(
            f"     Keyed   : "
            f"{transparent_count} border-connected dark pixels"
        )
        print(
            "     Threshold: RGB max <= 64"
        )
        print(
            "     Output  : "
            f"{output.relative_to(root).as_posix()}"
        )

        return True

    info(
        "Launcher M_DOOM: skipped; "
        "no supported IWAD provides it"
    )

    return False


def generate_doom_memcard_art(root: Path) -> bool:
    # Generate DoomCube Memory Card backgrounds from the full DOOM IWAD.
    iwad = root / "data/wad/doom.wad"

    output_dir = (
        root
        / "launcher"
        / "memcard"
    )

    wall_output = output_dir / "mwall4_1.bmp"
    error_output = output_dir / "pfub2.bmp"

    for output in (
        wall_output,
        error_output,
    ):
        if output.exists():
            output.unlink()

    if not iwad.is_file():
        info(
            "Memory-card Doom skin: skipped; "
            "missing data/wad/doom.wad"
        )
        return False

    try:
        wad = wadgfx.WadFile(iwad)

        _palette_wad, palette = (
            wadgfx.resolve_palette(
                [wad],
                0,
            )
        )

        wall_lump = wad.find_last("MWALL4_1")
        error_lump = wad.find_last("PFUB2")

        if wall_lump is None:
            raise wadgfx.WadError(
                "DOOM.WAD does not contain MWALL4_1"
            )

        if error_lump is None:
            raise wadgfx.WadError(
                "DOOM.WAD does not contain PFUB2"
            )

        wall_patch = wadgfx.decode_patch(
            wad.lump_data(wall_lump)
        )

        error_patch = wadgfx.decode_patch(
            wad.lump_data(error_lump)
        )

        if (
            wall_patch.width != 128
            or wall_patch.height != 128
        ):
            raise wadgfx.WadError(
                "MWALL4_1 has unexpected dimensions "
                f"{wall_patch.width}x{wall_patch.height}; "
                "expected 128x128"
            )

        if (
            error_patch.width != 320
            or error_patch.height != 200
        ):
            raise wadgfx.WadError(
                "PFUB2 has unexpected dimensions "
                f"{error_patch.width}x{error_patch.height}; "
                "expected 320x200"
            )

        wall_indices = patch_rows(wall_patch)
        error_indices = patch_rows(error_patch)

        if any(
            index is None
            for row in wall_indices
            for index in row
        ):
            raise wadgfx.WadError(
                "MWALL4_1 unexpectedly contains transparent pixels"
            )

        if any(
            index is None
            for row in error_indices
            for index in row
        ):
            raise wadgfx.WadError(
                "PFUB2 unexpectedly contains transparent pixels"
            )

        wall_pixels = [
            [palette[index] for index in row]
            for row in wall_indices
        ]

        error_pixels = [
            [palette[index] for index in row]
            for row in error_indices
        ]

        # One centered skull instead of a repeated wall grid.
        #
        # MWALL4_1 is square (128x128), while the GameCube frame is
        # 4:3. Center-crop the source to 128x96, then nearest-neighbour
        # scale that crop to 320x240. The runtime can therefore map it
        # directly to 640x480 with no distortion and no second crop.
        wall_crop_height = (
            wall_patch.width * 240 // 320
        )

        wall_crop_y = (
            wall_patch.height - wall_crop_height
        ) // 2

        covered_wall = [
            [
                wall_pixels[
                    wall_crop_y
                    + min(
                        wall_crop_height - 1,
                        y * wall_crop_height // 240,
                    )
                ][
                    min(
                        wall_patch.width - 1,
                        x * wall_patch.width // 320,
                    )
                ]
                for x in range(320)
            ]
            for y in range(240)
        ]

        output_dir.mkdir(
            parents=True,
            exist_ok=True,
        )

        write_bmp24(
            wall_output,
            covered_wall,
        )

        write_bmp24(
            error_output,
            error_pixels,
        )

    except (
        OSError,
        ValueError,
        wadgfx.WadError,
    ) as exc:
        warn(
            "Memory-card Doom skin unavailable: "
            f"{exc}; generic presentation will be used"
        )
        return False

    print()
    print("[OK] DoomCube memory-card skin art")
    print(f"     IWAD       : {iwad}")
    print(
        "     MWALL4_1   : "
        f"{wall_patch.width}x{wall_patch.height} "
        "center cover -> 320x240"
    )
    print(
        "     PFUB2      : "
        f"{error_patch.width}x{error_patch.height}"
    )
    print(
        "     Normal BMP : "
        f"{wall_output.relative_to(root).as_posix()}"
    )
    print(
        "     Error BMP  : "
        f"{error_output.relative_to(root).as_posix()}"
    )

    return True


def generate_doom_menu_skull(root: Path) -> bool:
    # Generate the stock Doom menu cursor directly from a supplied IWAD.
    output = root / "launcher" / "m_skull1.bmp"

    if output.exists():
        output.unlink()

    for label, relative in DOOM_MENU_LOGO_SOURCES:
        iwad = root / relative

        if not iwad.is_file():
            continue

        try:
            wad = wadgfx.WadFile(iwad)
            lump = wad.find_last("M_SKULL1")

            if lump is None:
                continue

            _palette_wad, palette = (
                wadgfx.resolve_palette(
                    [wad],
                    0,
                )
            )

            patch = wadgfx.decode_patch(
                wad.lump_data(lump)
            )

            rows = patch_rows(patch)

            if (
                len(rows) != patch.height
                or any(
                    len(row) != patch.width
                    for row in rows
                )
            ):
                raise wadgfx.WadError(
                    "M_SKULL1 decoded row geometry "
                    "does not match patch dimensions"
                )

            width = patch.width
            height = patch.height

            # Doom patch posts can leave genuine transparent holes.
            # Preserve every such hole. If this decoder/input instead
            # presents a painted border background, key only the
            # border-connected component, as we already do for M_DOOM.
            has_patch_transparency = any(
                index is None
                for row in rows
                for index in row
            )

            transparent = [
                [
                    rows[y][x] is None
                    for x in range(width)
                ]
                for y in range(height)
            ]

            if not has_patch_transparency:
                background_index = rows[0][0]
                queue: list[tuple[int, int]] = []

                def seed(x: int, y: int) -> None:
                    if transparent[y][x]:
                        return

                    if rows[y][x] != background_index:
                        return

                    transparent[y][x] = True
                    queue.append((x, y))

                for x in range(width):
                    seed(x, 0)
                    seed(x, height - 1)

                for y in range(height):
                    seed(0, y)
                    seed(width - 1, y)

                read_index = 0

                while read_index < len(queue):
                    x, y = queue[read_index]
                    read_index += 1

                    if x > 0:
                        seed(x - 1, y)

                    if x + 1 < width:
                        seed(x + 1, y)

                    if y > 0:
                        seed(x, y - 1)

                    if y + 1 < height:
                        seed(x, y + 1)

            pixels: list[
                list[tuple[int, int, int]]
            ] = []

            transparent_count = 0

            for y in range(height):
                row: list[
                    tuple[int, int, int]
                ] = []

                for x in range(width):
                    if transparent[y][x]:
                        row.append(
                            DOOM_MENU_LOGO_KEY
                        )
                        transparent_count += 1
                    else:
                        index = rows[y][x]

                        if index is None:
                            raise wadgfx.WadError(
                                "unexpected unkeyed transparent "
                                "M_SKULL1 pixel"
                            )

                        row.append(
                            palette[index]
                        )

                pixels.append(row)

            write_bmp24(
                output,
                pixels,
            )

        except (
            OSError,
            ValueError,
            wadgfx.WadError,
        ) as exc:
            warn(
                "Launcher M_SKULL1 unavailable "
                f"from {relative}: {exc}"
            )
            continue

        print()
        print("[OK] Launcher Doom menu skull")
        print(f"     Source  : {label}")
        print(f"     IWAD    : {iwad}")
        print(
            f"     Size    : "
            f"{patch.width}x{patch.height}"
        )
        print(
            f"     Keyed   : "
            f"{transparent_count} transparent pixels"
        )
        print(
            "     Lump    : M_SKULL1"
        )
        print(
            "     Key     : 255 0 255"
        )
        print(
            "     Output  : "
            f"{output.relative_to(root).as_posix()}"
        )

        return True

    info(
        "Launcher M_SKULL1: skipped; "
        "no supported IWAD provides it"
    )

    return False


def generate_splash_cube(root: Path) -> bool:
    source = root / "launcher" / "doomcube.bmp"
    output = root / "launcher" / "doomcube_splash.bmp"

    if output.exists():
        output.unlink()

    if not source.is_file():
        info(
            "Launcher splash cube: skipped; "
            "launcher/doomcube.bmp is missing"
        )
        return False

    try:
        width, height, pixels = read_bmp24(
            source
        )

        keyed, transparent_count = (
            key_border_connected_dark_pixels(
                pixels,
                threshold=64,
                key=DOOM_MENU_LOGO_KEY,
            )
        )

        write_bmp24(
            output,
            keyed,
        )

    except (
        OSError,
        ValueError,
    ) as exc:
        warn(
            f"Launcher splash cube unavailable: {exc}"
        )
        return False

    print()
    print("[OK] Launcher splash DoomCube cube")
    print(
        f"     Size    : {width}x{height}"
    )
    print(
        f"     Keyed   : "
        f"{transparent_count} border-connected dark pixels"
    )
    print(
        "     Threshold: RGB max <= 64"
    )
    print(
        "     Output  : "
        f"{output.relative_to(root).as_posix()}"
    )

    return True


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Generate DoomCube launcher TITLEPIC BMPs "
            "inside a disposable disc-staging tree."
        )
    )

    parser.add_argument(
        "--root",
        type=Path,
        required=True,
        help="DoomCube disc-staging root",
    )

    return parser.parse_args()


def main() -> int:
    args = parse_args()

    root = args.root.expanduser().resolve()

    if not root.is_dir():
        print(
            f"ERROR: disc-staging root does not exist: "
            f"{root}"
        )
        return 2

    generate_launcher_assets(root)

    # Artwork availability is deliberately non-fatal.
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
