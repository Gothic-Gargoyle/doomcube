#!/usr/bin/env python3

"""
DoomCube player release bundle builder.

Developer-side tool.

This script takes already-built DoomCube runtime components and creates
the self-contained player release:

    DoomCube/
    |── build.sh
    ├── build.bat
    ├── pack.py
    ├── WADs/
    ├── PWADs/
    ├── DEH/
    └── runtime/
        ├── doomcube.dol
        ├── apploader.bin
        ├── carryhandle.cfg
        ├── opening.bnr
        ├── assets/
        │   └── presentation/
        │       └── banner.png
        ├── tools/
        │   ├── ch_manifest.py
        │   └── native-gcm/
        │       └── ch_gcm.py
        ├── launcher/
        │   └── doomcube.bmp
        └── timidity/

The resulting DoomCube directory is then packed into a ZIP.

No WAD files are copied into the release.
"""

from __future__ import annotations

import argparse
import shutil
import stat
import subprocess
import sys
from pathlib import Path


STAGE_NAME = "DoomCube"
EXPECTED_TIMIDITY_PATCHES = 192


def die(message: str) -> None:
    print()
    print(f"[ERROR] {message}")
    raise SystemExit(1)


def info(message: str) -> None:
    print(f"[--] {message}")


def ok(message: str) -> None:
    print(f"[OK] {message}")


def human_size(size: int) -> str:
    value = float(size)

    for unit in ("B", "KiB", "MiB", "GiB"):
        if value < 1024.0 or unit == "GiB":
            if unit == "B":
                return f"{int(value)} {unit}"

            return f"{value:.1f} {unit}"

        value /= 1024.0

    return f"{size} B"


def require_file(description: str, path: Path) -> None:
    if not path.is_file():
        die(f"Missing {description}: {path}")


def require_directory(description: str, path: Path) -> None:
    if not path.is_dir():
        die(f"Missing {description}: {path}")


def validate_python(description: str, path: Path) -> None:
    require_file(description, path)

    try:
        source = path.read_text(encoding="utf-8")

        compile(
            source,
            str(path),
            "exec",
        )
    except (OSError, SyntaxError) as exc:
        die(
            f"{description} failed Python validation:\n"
            f"{exc}"
        )


def validate_timidity(path: Path) -> int:
    require_directory(
        "TiMidity runtime directory",
        path,
    )

    cfg = path / "timidity.cfg"

    require_file(
        "TiMidity configuration",
        cfg,
    )

    cfg_text = cfg.read_text(
        encoding="utf-8",
        errors="replace",
    )

    if "dvd:/data/timidity" not in cfg_text:
        die(
            "timidity.cfg does not contain the expected "
            "'dvd:/data/timidity' runtime path."
        )

    patch_count = sum(
        1
        for item in path.rglob("*")
        if (
            item.is_file()
            and item.suffix.lower() == ".pat"
        )
    )

    if patch_count != EXPECTED_TIMIDITY_PATCHES:
        die(
            "Unexpected TiMidity instrument set.\n"
            f"Expected: {EXPECTED_TIMIDITY_PATCHES}\n"
            f"Found:    {patch_count}"
        )

    return patch_count


def make_executable(path: Path) -> None:
    mode = path.stat().st_mode

    path.chmod(
        mode
        | stat.S_IXUSR
        | stat.S_IXGRP
        | stat.S_IXOTH
    )


def copy_timidity(
    source: Path,
    destination: Path,
) -> None:
    shutil.copytree(
        source,
        destination,
        dirs_exist_ok=True,
        ignore=shutil.ignore_patterns(
            ".gitkeep",
            "__pycache__",
            "*.pyc",
        ),
    )


def validate_stage(stage: Path) -> None:
    forbidden_wads = [
        item
        for item in stage.rglob("*")
        if (
            item.is_file()
            and item.suffix.lower() == ".wad"
        )
    ]

    if forbidden_wads:
        listing = "\n".join(
            f"  {item.relative_to(stage)}"
            for item in forbidden_wads
        )

        die(
            "Release bundle unexpectedly contains WAD files:\n"
            f"{listing}"
        )

    forbidden_junk = [
        item
        for item in stage.rglob("*")
        if (
            item.is_file()
            and (
                item.name == ".gitkeep"
                or item.suffix.lower() == ".pyc"
            )
        )
    ]

    if forbidden_junk:
        listing = "\n".join(
            f"  {item.relative_to(stage)}"
            for item in forbidden_junk
        )

        die(
            "Release bundle contains unwanted files:\n"
            f"{listing}"
        )


def stage_bundle(
    *,
    stage: Path,
    dol: Path,
    apploader: Path,
    image_builder: Path,
    manifest_tool: Path,
    bnr_tool: Path,
    manifest: Path,
    presentation_banner: Path,
    packer: Path,
    launcher_assets: Path,
    wadgfx: Path,
    launcher: Path,
    timidity: Path,
    shell_launcher: Path,
    batch_launcher: Path,
) -> None:
    if stage.exists():
        shutil.rmtree(stage)

    runtime = stage / "runtime"

    static_stager = (
        packer.parent.parent
        / "stage_static_runtime.py"
    ).resolve()

    directories = (
        stage / "WADs",
        stage / "PWADs",
        stage / "PWADs/doom",
        stage / "PWADs/doom2",
        stage / "PWADs/tnt",
        stage / "PWADs/plutonia",
        stage / "DEH",
        runtime / "assets/presentation",
        runtime / "tools/native-gcm",
        runtime / "launcher",
        runtime / "timidity",
    )

    for directory in directories:
        directory.mkdir(
            parents=True,
            exist_ok=True,
        )

    shutil.copy2(
        packer,
        stage / "pack.py",
    )

    shutil.copy2(
        shell_launcher,
        stage / "build.sh",
    )

    shutil.copy2(
        batch_launcher,
        stage / "build.bat",
    )

    make_executable(
        stage / "build.sh",
    )

    shutil.copy2(
        dol,
        runtime / "doomcube.dol",
    )

    shutil.copy2(
        apploader,
        runtime / "apploader.bin",
    )

    shutil.copy2(
        image_builder,
        runtime / "tools/native-gcm/ch_gcm.py",
    )

    shutil.copy2(
        manifest_tool,
        runtime / "tools/ch_manifest.py",
    )

    shutil.copy2(
        launcher_assets,
        runtime / "tools/launcher_assets.py",
    )

    shutil.copy2(
        wadgfx,
        runtime / "tools/wadgfx.py",
    )

    shutil.copy2(
        manifest,
        runtime / "carryhandle.cfg",
    )

    shutil.copy2(
        presentation_banner,
        runtime / "assets/presentation/banner.png",
    )

    shutil.copy2(
        launcher,
        runtime / "launcher/doomcube.bmp",
    )

    require_file(
        "DoomCube static runtime stager",
        static_stager,
    )

    static_root = runtime / "static"

    result = subprocess.run(
        [
            sys.executable,
            "-B",
            str(static_stager),
            "--root",
            str(static_root),
        ],
        check=False,
    )

    if result.returncode != 0:
        die(
            "DoomCube static runtime staging failed "
            f"with exit status {result.returncode}."
        )

    info("Static DoomCube runtime staged for player bundle")

    copy_timidity(
        timidity,
        runtime / "timidity",
    )

    make_executable(
        stage / "pack.py",
    )

    make_executable(
        runtime / "tools/native-gcm/ch_gcm.py",
    )

    make_executable(
        runtime / "tools/launcher_assets.py",
    )

    make_executable(
        runtime / "tools/wadgfx.py",
    )

    bnr_output = runtime / "opening.bnr"

    result = subprocess.run(
        [
            sys.executable,
            "-B",
            str(bnr_tool),
            "--manifest",
            str(runtime / "carryhandle.cfg"),
            "--output",
            str(bnr_output),
        ],
        check=False,
    )

    if result.returncode != 0:
        die(
            "CarryHandle BNR generation failed "
            f"with exit status {result.returncode}."
        )

    require_file(
        "generated opening.bnr",
        bnr_output,
    )

    bnr_data = bnr_output.read_bytes()

    if len(bnr_data) != 6496 or bnr_data[:4] != b"BNR1":
        die(
            "Generated opening.bnr is not the expected "
            "6496-byte BNR1 payload."
        )

    validate_stage(stage)



# DOOMCUBE_PLAYER_README_V1
#
# The player-facing README is application-owned prose. Keep it outside
# CarryHandle's runtime/application manifest and stage it only when building
# the downloadable player bundle.
MEMORY_CARD_WARNING_PREFIX = (
    b"!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"
    b"WARNING: MEMORY CARD SUPPORT IS EXPERIMENTAL\n"
    b"!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"
    b"\n"
    b"DO NOT USE A MEMORY CARD CONTAINING SAVES OR OTHER DATA YOU CARE ABOUT.\n"
    b"\n"
    b"MEMORY CARD SAVE/CONFIG WRITING HAS NOT YET BEEN VALIDATED ON REAL\n"
    b"HARDWARE. UNTIL IT HAS BEEN PROVEN SAFE, USE A DISPOSABLE/TEST CARD\n"
    b"ONLY.\n"
    b"\n"
    b"!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"
    b"\n"
)


def stage_player_readme(
    readme: Path,
    stage: Path,
) -> Path:
    # Stage and verify DoomCube's player-facing README.txt.

    if not readme.is_file():
        raise RuntimeError(
            f"Player README is missing: {readme}"
        )

    source = readme.read_bytes()

    if not source.startswith(MEMORY_CARD_WARNING_PREFIX):
        raise RuntimeError(
            "Player README does not begin with the required "
            "all-caps experimental Memory Card warning."
        )

    destination = stage / "README.txt"
    shutil.copy2(readme, destination)

    if destination.read_bytes() != source:
        raise RuntimeError(
            "Staged README.txt does not match the source README."
        )

    ok("player README.txt staged")
    return destination

def create_zip(
    *,
    dist: Path,
    stage: Path,
    archive_name: str,
) -> Path:
    archive_name = Path(archive_name).name

    if not archive_name.lower().endswith(".zip"):
        archive_name += ".zip"

    archive = dist / archive_name

    if archive.exists():
        archive.unlink()

    archive_base = archive.with_suffix("")

    result = Path(
        shutil.make_archive(
            str(archive_base),
            "zip",
            root_dir=dist,
            base_dir=stage.name,
        )
    )

    if not result.is_file():
        die(
            "Release archive was not created."
        )

    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Build a self-contained DoomCube player release."
        )
    )

    parser.add_argument(
        "--dol",
        type=Path,
        required=True,
        help="compiled DoomCube DOL",
    )

    parser.add_argument(
        "--apploader",
        type=Path,
        required=True,
        help="pinned CarryHandle apploader.bin",
    )

    parser.add_argument(
        "--builder",
        type=Path,
        required=True,
        help="pinned CarryHandle ch_gcm.py",
    )

    parser.add_argument(
        "--manifest-tool",
        type=Path,
        required=True,
        help="pinned CarryHandle ch_manifest.py",
    )

    parser.add_argument(
        "--bnr-tool",
        type=Path,
        required=True,
        help="pinned CarryHandle ch_bnr.py",
    )

    parser.add_argument(
        "--manifest",
        type=Path,
        required=True,
        help="DoomCube carryhandle.cfg",
    )

    parser.add_argument(
        "--presentation-banner",
        type=Path,
        required=True,
        help="DoomCube 96x32 presentation banner",
    )

    parser.add_argument(
        "--packer",
        type=Path,
        required=True,
        help="player-side pack.py",
    )

    parser.add_argument(
        "--launcher",
        type=Path,
        required=True,
        help="DoomCube launcher artwork",
    )

    parser.add_argument(
        "--timidity",
        type=Path,
        required=True,
        help="TiMidity runtime directory",
    )

    parser.add_argument(
        "--dist",
        type=Path,
        required=True,
        help="release output directory",
    )

    parser.add_argument(
        "--archive-name",
        required=True,
        help="release ZIP filename",
    )

    return parser.parse_args()


def main() -> None:
    args = parse_args()

    dol = args.dol.resolve()
    apploader = args.apploader.resolve()
    image_builder = args.builder.resolve()
    manifest_tool = args.manifest_tool.resolve()
    bnr_tool = args.bnr_tool.resolve()
    manifest = args.manifest.resolve()
    presentation_banner = args.presentation_banner.resolve()
    packer = args.packer.resolve()

    launcher_assets = (
        packer.parent.parent
        / "launcher_assets.py"
    ).resolve()

    wadgfx = (
        packer.parent.parent
        / "wadgfx.py"
    ).resolve()

    shell_launcher = (
        packer.parent / "build.sh"
    ).resolve()

    batch_launcher = (
        packer.parent / "build.bat"
    ).resolve()

    launcher = args.launcher.resolve()
    timidity = args.timidity.resolve()
    dist = args.dist.resolve()

    print()
    print("DoomCube Release Builder")
    print("========================")

    info(f"DOL:       {dol}")
    info(f"Apploader: {apploader}")
    info(f"GCM:       {image_builder}")
    info(f"Manifest:  {manifest}")
    info(f"BNR tool:  {bnr_tool}")
    info(f"Packer:    {packer}")
    info(f"Launcher:  {launcher}")
    info(f"TiMidity:  {timidity}")
    info(f"Output:    {dist / args.archive_name}")

    require_file(
        "DoomCube DOL",
        dol,
    )

    require_file(
        "CarryHandle apploader",
        apploader,
    )

    validate_python(
        "CarryHandle native GameCube image builder",
        image_builder,
    )

    validate_python(
        "CarryHandle manifest tool",
        manifest_tool,
    )

    validate_python(
        "CarryHandle BNR tool",
        bnr_tool,
    )

    require_file(
        "DoomCube CarryHandle manifest",
        manifest,
    )

    require_file(
        "DoomCube presentation banner",
        presentation_banner,
    )

    validate_python(
        "player disc builder",
        packer,
    )

    validate_python(
        "launcher asset generator",
        launcher_assets,
    )

    validate_python(
        "Doom WAD graphics decoder",
        wadgfx,
    )

    require_file(
        "Unix player launcher",
        shell_launcher,
    )

    require_file(
        "Windows player launcher",
        batch_launcher,
    )

    require_file(
        "launcher artwork",
        launcher,
    )

    patch_count = validate_timidity(
        timidity,
    )

    ok(
        f"TiMidity instrument set: "
        f"{patch_count} patches"
    )

    dist.mkdir(
        parents=True,
        exist_ok=True,
    )

    stage = dist / STAGE_NAME

    print()
    print("Staging player bundle")
    print("=====================")

    stage_bundle(
        stage=stage,
        dol=dol,
        apploader=apploader,
        image_builder=image_builder,
        manifest_tool=manifest_tool,
        bnr_tool=bnr_tool,
        manifest=manifest,
        presentation_banner=presentation_banner,
        packer=packer,
        launcher_assets=launcher_assets,
        wadgfx=wadgfx,
        shell_launcher=shell_launcher,
        batch_launcher=batch_launcher,
        launcher=launcher,
        timidity=timidity,
    )

    ok("Player bundle staged")
    ok("CarryHandle manifest/GCM tooling packaged")
    ok("opening.bnr generated at release-build time")
    ok("No IWADs packaged")
    ok("No .gitkeep or Python cache files packaged")

    print()
    # The player-facing README is application-owned release prose.
    # build_bundle.py lives under tools/release/, while the player README
    # lives in the repository-level release/ directory.
    player_readme = (
        Path(__file__).resolve().parents[2]
        / "release"
        / "README.txt"
    )
    stage_player_readme(
        player_readme,
        stage,
    )

    print("Creating ZIP")
    print("============")

    archive = create_zip(
        dist=dist,
        stage=stage,
        archive_name=args.archive_name,
    )

    print()
    print("========================================")
    print("[OK] DoomCube release created successfully")
    print("========================================")
    print()
    print(f"Stage:   {stage}")
    print(f"Archive: {archive}")
    print(
        f"Size:    {human_size(archive.stat().st_size)}"
    )


if __name__ == "__main__":
    main()
