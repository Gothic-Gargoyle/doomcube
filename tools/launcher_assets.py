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
