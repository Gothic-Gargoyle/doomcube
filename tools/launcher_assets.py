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

    return generated, skipped


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
