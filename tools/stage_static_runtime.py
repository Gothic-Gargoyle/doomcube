#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import shutil
from pathlib import Path


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require_file(path: Path, label: str) -> None:
    if not path.is_file():
        raise SystemExit(f"ERROR: missing {label}: {path}")


def copy_verified(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    if source.stat().st_size != destination.stat().st_size:
        raise SystemExit(f"ERROR: size mismatch staging {destination}")
    if sha256_file(source) != sha256_file(destination):
        raise SystemExit(f"ERROR: SHA256 mismatch staging {destination}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Stage DoomCube static runtime assets")
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[1]
    ch = repo / "deps/carryhandle"
    root = args.root.resolve()
    launcher = repo / "data/launcher"
    toomai = ch / "assets/controller/gamecube/toomai"

    static_files = (
        (launcher / "doomcube.bmp", root / "launcher/doomcube.bmp", "DoomCube launcher logo"),
        (launcher / "sperge_brigade_studios.bmp", root / "launcher/sperge_brigade_studios.bmp", "studio ident"),
        (ch / "assets/branding/powered_by_carryhandle.bmp", root / "launcher/carryhandle_powered_by.bmp", "CarryHandle ident"),
        (toomai / "ATTRIBUTION.md", root / "assets/controller/gamecube/toomai/ATTRIBUTION.md", "Toomai attribution"),
        (toomai / "MANIFEST.tsv", root / "assets/controller/gamecube/toomai/MANIFEST.tsv", "Toomai manifest"),
    )

    for source, _dest, label in static_files:
        require_file(source, label)

    glyphs = sorted((toomai / "bmp").glob("ButtonIcon-GCN-*.bmp"), key=lambda p: p.name.casefold())
    if len(glyphs) != 43:
        raise SystemExit(f"ERROR: expected 43 Toomai BMPs, found {len(glyphs)}")

    for source, destination, _label in static_files:
        copy_verified(source, destination)

    glyph_dest = root / "assets/controller/gamecube/toomai/bmp"
    glyph_dest.mkdir(parents=True, exist_ok=True)
    for old in glyph_dest.glob("*.bmp"):
        old.unlink()
    for glyph in glyphs:
        copy_verified(glyph, glyph_dest / glyph.name)

    staged = list(glyph_dest.glob("ButtonIcon-GCN-*.bmp"))
    if len(staged) != 43:
        raise SystemExit(f"ERROR: staged glyph count is {len(staged)}, expected 43")

    print("DoomCube static runtime staged")
    print(f"  root       : {root}")
    print("  launcher   : doomcube.bmp")
    print("  studio     : sperge_brigade_studios.bmp")
    print("  CarryHandle: carryhandle_powered_by.bmp")
    print(f"  glyphs     : {len(staged)} BMPs")
    print("  WAD-derived assets: intentionally excluded here")


if __name__ == "__main__":
    main()
