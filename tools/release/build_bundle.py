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
        ├── mkdoomcube.py
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
    packer: Path,
    launcher: Path,
    timidity: Path,
    shell_launcher: Path,
    batch_launcher: Path,
) -> None:
    if stage.exists():
        shutil.rmtree(stage)

    runtime = stage / "runtime"

    directories = (
        stage / "WADs",
        stage / "PWADs",
        stage / "DEH",
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
        runtime / "mkdoomcube.py",
    )

    shutil.copy2(
        launcher,
        runtime / "launcher/doomcube.bmp",
    )

    copy_timidity(
        timidity,
        runtime / "timidity",
    )

    make_executable(
        stage / "pack.py",
    )

    make_executable(
        runtime / "mkdoomcube.py",
    )

    validate_stage(stage)


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
        help="compiled GameCube apploader.bin",
    )

    parser.add_argument(
        "--builder",
        type=Path,
        required=True,
        help="mkdoomcube.py",
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
    packer = args.packer.resolve()
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
    info(f"Builder:   {image_builder}")
    info(f"Packer:    {packer}")
    info(f"Launcher:  {launcher}")
    info(f"TiMidity:  {timidity}")
    info(f"Output:    {dist / args.archive_name}")

    require_file(
        "DoomCube DOL",
        dol,
    )

    require_file(
        "GameCube apploader",
        apploader,
    )

    validate_python(
        "native GameCube image builder",
        image_builder,
    )

    validate_python(
        "player disc builder",
        packer,
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
        packer=packer,
        shell_launcher=shell_launcher,
        batch_launcher=batch_launcher,
        launcher=launcher,
        timidity=timidity,
    )

    ok("Player bundle staged")
    ok("No IWADs packaged")
    ok("No .gitkeep or Python cache files packaged")

    print()
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
