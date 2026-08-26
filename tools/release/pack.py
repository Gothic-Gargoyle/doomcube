#!/usr/bin/env python3

"""
DoomCube player-side disc packer.

This tool does NOT compile DoomCube.

It combines:
    - a precompiled DoomCube DOL
    - a precompiled GameCube apploader
    - DoomCube runtime assets
    - user-supplied IWADs

into a native GameCube disc image using mkdoomcube.py.

It supports two layouts:

1. DoomCube source tree
2. DoomCube release bundle

Release bundle layout:

    DoomCube/
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
"""

from __future__ import annotations

import argparse
import hashlib
import io
import shutil
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request
import tarfile
from dataclasses import dataclass
from pathlib import Path

# ---------------------------------------------------------------------------
# DOOM Shareware v1.9
# ---------------------------------------------------------------------------

SHAREWARE_FILENAME = "doom1.wad"

SHAREWARE_SIZE = 4_196_020

SHAREWARE_SHA256 = (
    "1d7d43be501e67d927e415e0b8f3e29c"
    "3bf33075e859721816f652a526cac771"
)

SHAREWARE_ARCHIVE_NAME = "doom-wad-shareware_1.9.fixed.orig.tar.gz"

SHAREWARE_URLS = (
    (
        "https://ftp.debian.org/debian/pool/non-free/d/"
        "doom-wad-shareware/"
        + SHAREWARE_ARCHIVE_NAME
    ),
    (
        "https://deb.debian.org/debian/pool/non-free/d/"
        "doom-wad-shareware/"
        + SHAREWARE_ARCHIVE_NAME
    ),
)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def validate_shareware_wad(data: bytes) -> None:
    if len(data) != SHAREWARE_SIZE:
        raise RuntimeError(
            "Downloaded DOOM Shareware has the wrong size:\n"
            f"  expected: {SHAREWARE_SIZE:,} bytes\n"
            f"  actual:   {len(data):,} bytes"
        )

    if data[:4] != b"IWAD":
        raise RuntimeError(
            "Downloaded DOOM Shareware does not have an IWAD header."
        )

    digest = sha256_bytes(data)

    if digest.lower() != SHAREWARE_SHA256.lower():
        raise RuntimeError(
            "Downloaded DOOM Shareware failed SHA-256 verification:\n"
            f"  expected: {SHAREWARE_SHA256}\n"
            f"  actual:   {digest}"
        )


def extract_shareware_wad(archive_data: bytes) -> bytes:
    try:
        with tarfile.open(
            fileobj=io.BytesIO(archive_data),
            mode="r:gz",
        ) as archive:
            wad_member = next(
                (
                    member
                    for member in archive.getmembers()
                    if member.isfile()
                    and Path(member.name).name.lower() == SHAREWARE_FILENAME
                ),
                None,
            )

            if wad_member is None:
                raise RuntimeError(
                    f"{SHAREWARE_FILENAME} was not found in "
                    f"{SHAREWARE_ARCHIVE_NAME}."
                )

            extracted = archive.extractfile(wad_member)

            if extracted is None:
                raise RuntimeError(
                    f"Could not extract {SHAREWARE_FILENAME} from "
                    f"{SHAREWARE_ARCHIVE_NAME}."
                )

            wad_data = extracted.read()

    except tarfile.TarError as exc:
        raise RuntimeError(
            "Downloaded DOOM Shareware archive is not a valid tar.gz file."
        ) from exc

    validate_shareware_wad(wad_data)

    return wad_data

def download_shareware(wad_dir: Path) -> Path:
    destination = wad_dir / SHAREWARE_FILENAME

    if destination.exists():
        existing = destination.read_bytes()

        try:
            validate_shareware_wad(existing)
        except RuntimeError:
            raise RuntimeError(
                f"{destination} already exists, but it is not the expected "
                "DOOM Shareware v1.9 WAD.\n"
                "DoomCube will not overwrite it."
            )

        print()
        print("[OK] DOOM Shareware v1.9 is already present")
        print(f"     {destination}")
        print(f"     {len(existing):,} bytes")
        print(f"     SHA-256: {SHAREWARE_SHA256}")

        return destination

    print()
    print("Downloading DOOM Shareware v1.9...")
    print()

    archive_data = None
    downloaded_from = None
    errors = []

    for url in SHAREWARE_URLS:
        print(f"  Trying: {url}")

        request = urllib.request.Request(
            url,
            headers={
                "User-Agent": "DoomCube pack.py"
            },
        )

        try:
            with urllib.request.urlopen(
                request,
                timeout=30,
            ) as response:
                archive_data = response.read()

            downloaded_from = url
            break

        except (
            urllib.error.URLError,
            urllib.error.HTTPError,
            TimeoutError,
            OSError,
        ) as exc:
            errors.append(f"{url}: {exc}")

    if archive_data is None:
        detail = "\n".join(
            f"  {error}"
            for error in errors
        )

        raise RuntimeError(
            "Could not download DOOM Shareware from any configured mirror."
            + (
                "\n" + detail
                if detail
                else ""
            )
        )

    print()
    print(f"Downloaded {len(archive_data):,} bytes")
    print(f"Extracting {SHAREWARE_FILENAME}...")

    wad_data = extract_shareware_wad(archive_data)

    print()
    print("[OK] DOOM Shareware verified")
    print(f"     Size:     {len(wad_data):,} bytes")
    print(f"     SHA-256:  {sha256_bytes(wad_data)}")
    print(f"     Source:   {downloaded_from}")

    wad_dir.mkdir(
        parents=True,
        exist_ok=True,
    )

    temporary = destination.with_suffix(".wad.tmp")

    try:
        temporary.write_bytes(wad_data)
        temporary.replace(destination)
    finally:
        if temporary.exists():
            temporary.unlink()

    print()
    print(f"[OK] Installed {destination}")

    return destination


def offer_shareware_download(wad_dir: Path) -> bool:
    destination = wad_dir / SHAREWARE_FILENAME

    if destination.exists():
        return False

    if not sys.stdin.isatty():
        return False

    print()
    print("No supported IWADs were found.")
    print()
    print(
        "DOOM Shareware v1.9 is available separately."
    )
    print(
        "DoomCube can download and verify it automatically."
    )
    print()

    try:
        answer = input(
            "Download DOOM Shareware now? [Y/n]: "
        ).strip().lower()
    except EOFError:
        return False

    if answer not in ("", "y", "yes"):
        print()
        print("Shareware download skipped.")
        return False

    download_shareware(wad_dir)

    return True

SUPPORTED_IWADS = {
    "doom1.wad": "DOOM Shareware",
    "doom.wad": "The Ultimate DOOM",
    "doom2.wad": "DOOM II",
    "tnt.wad": "Final DOOM: TNT - Evilution",
    "plutonia.wad": "Final DOOM: The Plutonia Experiment",
}


@dataclass
class Runtime:
    mode: str
    root: Path
    dol: Path
    apploader: Path
    builder: Path
    launcher: Path
    timidity: Path
    default_wads: Path
    default_pwads: Path
    default_deh: Path


def die(message: str) -> None:
    print()
    print(f"[ERROR] {message}")
    raise SystemExit(1)


def ok(message: str) -> None:
    print(f"[OK] {message}")


def info(message: str) -> None:
    print(f"[--] {message}")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()

    with path.open("rb") as f:
        while True:
            chunk = f.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)

    return digest.hexdigest()


def human_size(size: int) -> str:
    value = float(size)

    for unit in ("B", "KiB", "MiB", "GiB"):
        if value < 1024.0 or unit == "GiB":
            if unit == "B":
                return f"{int(value)} {unit}"
            return f"{value:.1f} {unit}"
        value /= 1024.0

    return f"{size} B"


def newest_matching(directory: Path, patterns: tuple[str, ...]) -> Path | None:
    matches: list[Path] = []

    for pattern in patterns:
        matches.extend(
            p for p in directory.glob(pattern)
            if p.is_file()
        )

    if not matches:
        return None

    return max(matches, key=lambda p: p.stat().st_mtime)


def find_repo_root(start: Path) -> Path | None:
    candidates = [start, *start.parents]

    for candidate in candidates:
        if (
            (candidate / "Makefile").is_file()
            and (candidate / "tools/native-gcm/mkdoomcube.py").is_file()
        ):
            return candidate

    return None


def discover_runtime(script_path: Path) -> Runtime:
    script_dir = script_path.parent

    # ------------------------------------------------------------
    # Release bundle mode
    # ------------------------------------------------------------

    for bundle_root in (script_dir, Path.cwd()):
        runtime_dir = bundle_root / "runtime"

        if not runtime_dir.is_dir():
            continue

        dol = runtime_dir / "doomcube.dol"

        if not dol.is_file():
            dol = newest_matching(
                runtime_dir,
                ("doomcube*.dol", "*.dol"),
            )

        if dol is None:
            die(f"No DoomCube DOL found in {runtime_dir}")

        apploader = runtime_dir / "apploader.bin"
        builder = runtime_dir / "mkdoomcube.py"
        launcher = runtime_dir / "launcher/doomcube.bmp"
        timidity = runtime_dir / "timidity"

        return Runtime(
            mode="release",
            root=bundle_root,
            dol=dol,
            apploader=apploader,
            builder=builder,
            launcher=launcher,
            timidity=timidity,
            default_wads=bundle_root / "WADs",
            default_pwads=bundle_root / "PWADs",
            default_deh=bundle_root / "DEH",
        )

    # ------------------------------------------------------------
    # Source tree mode
    # ------------------------------------------------------------

    repo_root = find_repo_root(script_dir)

    if repo_root is None:
        repo_root = find_repo_root(Path.cwd())

    if repo_root is None:
        die(
            "Could not find either a DoomCube release bundle "
            "or DoomCube source tree."
        )

    dol = newest_matching(
        repo_root,
        ("doomcube-v*.dol", "doomcube*.dol"),
    )

    if dol is None:
        die(
            "No compiled DoomCube DOL found.\n"
            "Developer/source-tree mode requires DoomCube to have "
            "already been compiled with 'make'."
        )

    return Runtime(
        mode="source",
        root=repo_root,
        dol=dol,
        apploader=repo_root / "tools/native-gcm/apploader.bin",
        builder=repo_root / "tools/native-gcm/mkdoomcube.py",
        launcher=repo_root / "data/launcher/doomcube.bmp",
        timidity=repo_root / "data/timidity",
        default_wads=repo_root / "data/wad",
        default_pwads=repo_root / "data/pwad",
        default_deh=repo_root / "data/deh",
    )


def validate_runtime(runtime: Runtime) -> None:
    required_files = (
        ("DoomCube DOL", runtime.dol),
        ("GameCube apploader", runtime.apploader),
        ("native GCM builder", runtime.builder),
        ("launcher artwork", runtime.launcher),
        ("TiMidity configuration", runtime.timidity / "timidity.cfg"),
    )

    for description, path in required_files:
        if not path.is_file():
            die(f"Missing {description}: {path}")

    if not runtime.timidity.is_dir():
        die(f"Missing TiMidity directory: {runtime.timidity}")

    cfg = (runtime.timidity / "timidity.cfg").read_text(
        encoding="utf-8",
        errors="replace",
    )

    if "dvd:/data/timidity" not in cfg:
        die(
            "timidity.cfg does not contain the expected "
            "'dvd:/data/timidity' path."
        )

    patch_count = sum(
        1 for p in runtime.timidity.rglob("*")
        if p.is_file() and p.suffix.lower() == ".pat"
    )

    if patch_count == 0:
        die("No TiMidity .pat instrument files were found.")

    if patch_count != 192:
        info(
            f"TiMidity contains {patch_count} .pat files "
            "(the tested DoomCube set contains 192)."
        )
    else:
        ok("TiMidity instrument set: 192 patches")


def add_wad(
    found: dict[str, Path],
    path: Path,
) -> None:
    if not path.is_file():
        die(f"IWAD does not exist: {path}")

    canonical = path.name.lower()

    if canonical not in SUPPORTED_IWADS:
        die(
            f"Unsupported IWAD filename: {path.name}\n"
            "Supported filenames: "
            + ", ".join(SUPPORTED_IWADS)
        )

    if canonical in found:
        die(
            f"Duplicate IWAD for {canonical}:\n"
            f"  {found[canonical]}\n"
            f"  {path}"
        )

    try:
        with path.open("rb") as f:
            header = f.read(4)
    except OSError as exc:
        die(f"Could not read {path}: {exc}")

    if header != b"IWAD":
        die(
            f"{path} does not appear to be an IWAD "
            f"(header was {header!r})."
        )

    found[canonical] = path


def discover_wads(
    supplied_paths: list[Path],
    default_directory: Path,
) -> dict[str, Path]:
    found: dict[str, Path] = {}

    if supplied_paths:
        for supplied in supplied_paths:
            supplied = supplied.expanduser().resolve()

            if supplied.is_dir():
                for child in sorted(supplied.iterdir()):
                    if (
                        child.is_file()
                        and child.name.lower() in SUPPORTED_IWADS
                    ):
                        add_wad(found, child)
            else:
                add_wad(found, supplied)

        return found

    if default_directory.is_dir():
        for child in sorted(default_directory.iterdir()):
            if (
                child.is_file()
                and child.name.lower() in SUPPORTED_IWADS
            ):
                add_wad(found, child)

    return found


def print_wad_report(found: dict[str, Path]) -> None:
    print()
    print("IWADs")
    print("=====")

    for filename, title in SUPPORTED_IWADS.items():
        path = found.get(filename)

        if path is None:
            print(f"[--] {title}")
            continue

        size = path.stat().st_size
        digest = sha256_file(path)

        print(
            f"[OK] {title}\n"
            f"     {path}\n"
            f"     {human_size(size)}, SHA-256 {digest}"
        )


def copy_optional_tree(source: Path, destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=True)

    if not source.is_dir():
        return

    shutil.copytree(
        source,
        destination,
        dirs_exist_ok=True,
    )


def stage_disc(
    runtime: Runtime,
    found_wads: dict[str, Path],
    staging: Path,
) -> None:
    wad_dir = staging / "data/wad"
    pwad_dir = staging / "data/pwad"
    deh_dir = staging / "data/deh"
    timidity_dir = staging / "data/timidity"
    launcher_dir = staging / "launcher"

    wad_dir.mkdir(parents=True, exist_ok=True)
    pwad_dir.mkdir(parents=True, exist_ok=True)
    deh_dir.mkdir(parents=True, exist_ok=True)
    launcher_dir.mkdir(parents=True, exist_ok=True)

    for canonical, source in found_wads.items():
        shutil.copy2(
            source,
            wad_dir / canonical,
        )

    copy_optional_tree(
        runtime.default_pwads,
        pwad_dir,
    )

    copy_optional_tree(
        runtime.default_deh,
        deh_dir,
    )

    shutil.copytree(
        runtime.timidity,
        timidity_dir,
        dirs_exist_ok=True,
        ignore=shutil.ignore_patterns(".gitkeep"),
    )

    shutil.copy2(
        runtime.launcher,
        launcher_dir / "doomcube.bmp",
    )


def run_builder(
    runtime: Runtime,
    staging: Path,
    output: Path,
) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)

    command = [
        sys.executable,
        str(runtime.builder),
        "--dol",
        str(runtime.dol),
        "--apploader",
        str(runtime.apploader),
        "--root",
        str(staging),
        "--output",
        str(output),
        "--title",
        "DOOMCUBE",
    ]

    print()
    print("Building native GameCube image")
    print("==============================")

    result = subprocess.run(command)

    if result.returncode != 0:
        die(
            "Native GameCube image builder failed "
            f"with exit status {result.returncode}."
        )

    if not output.is_file():
        die(
            "The native builder returned successfully, "
            "but the expected output image was not created."
        )


def default_output(runtime: Runtime) -> Path:
    if runtime.mode == "release":
        return runtime.root / "DoomCube.iso"

    return runtime.root / (
        runtime.dol.stem + "-packed.iso"
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Build a DoomCube native GameCube disc image "
            "using precompiled runtime files and user-supplied IWADs."
        )
    )

    parser.add_argument(
        "wads",
        nargs="*",
        type=Path,
        help=(
            "Optional IWAD files or directories. "
            "If omitted, the default WAD directory is scanned."
        ),
    )

    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        help="Output ISO path.",
    )

    return parser.parse_args()


def main() -> int:
    args = parse_args()

    print()
    print("DoomCube Disc Builder")
    print("=====================")

    runtime = discover_runtime(Path(__file__).resolve())

    info(f"Mode: {runtime.mode}")
    info(f"DOL: {runtime.dol}")

    validate_runtime(runtime)

    found_wads = discover_wads(
        args.wads,
        runtime.default_wads,
    )

    if not found_wads and not args.wads:
        try:
            if offer_shareware_download(runtime.default_wads):
                found_wads = discover_wads(
                    [],
                    runtime.default_wads,
                )
        except RuntimeError as exc:
            die(str(exc))

    print_wad_report(found_wads)

    if not found_wads:
        print()
        print("No supported IWADs were found.")
        print()
        print("Supported filenames:")
        for filename in SUPPORTED_IWADS:
            print(f"  {filename}")

        if runtime.mode == "release":
            print()
            print("Copy one or more IWADs into:")
            print(f"  {runtime.default_wads}")

        return 2

    output = (
        args.output.expanduser().resolve()
        if args.output
        else default_output(runtime)
    )

    print()
    info(f"Output: {output}")

    with tempfile.TemporaryDirectory(
        prefix="doomcube-pack-"
    ) as temporary:
        staging = Path(temporary)

        stage_disc(
            runtime,
            found_wads,
            staging,
        )

        run_builder(
            runtime,
            staging,
            output,
        )

    print()
    print("========================================")
    print("[OK] DoomCube image created successfully")
    print("========================================")
    print()
    print(output)
    print()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
