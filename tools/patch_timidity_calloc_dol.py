#!/usr/bin/env python3
"""
DoomCube SDL2_mixer/TiMidity Sample[] zero-init DOL hotfix.

Runtime-proven semantic change:

    SDL_malloc(sample_count * 108)
        ->
    SDL_calloc(sample_count, 108)

Why this is needed:
SDL_mixer 2.8.2's TiMidity loader allocates the complete Sample[] array with
SDL_malloc(). If loading fails after only part of the array has been initialized,
free_instrument() still walks every declared Sample and frees Sample.data.
Never-initialized entries can therefore contain garbage pointers.

This helper patches the linked DOL immediately before native GCM construction.
It:
  * derives the companion ELF from the DOL name
  * reads SDL_malloc / SDL_calloc addresses from that ELF
  * finds exactly one matching TiMidity allocation sequence in DOL text
  * proves the stock call targets SDL_malloc before changing anything
  * rewrites exactly two PowerPC instructions
  * verifies the result
  * is idempotent
"""

from __future__ import annotations

import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import NoReturn

# Proven instruction shape around the SDL_mixer TiMidity Sample[] allocation.
PREFIX = bytes.fromhex(
    "886100ce"  # lbz   r3,206(r1)  ; sample count
    "907d0000"  # stw   r3,0(r29)    ; Instrument.samples
)

MULLI_R3_R3_108 = bytes.fromhex("1c63006c")
LI_R4_108 = bytes.fromhex("3880006c")

SUFFIX = bytes.fromhex(
    "2c030000"  # cmpwi r3,0
    "907d0004"  # stw   r3,4(r29)    ; Instrument.sample
)

BRANCH_OPCODE_MASK = 0xFC000000
BRANCH_OPCODE = 0x48000000
BRANCH_AA = 0x00000002
BRANCH_LK = 0x00000001
BRANCH_DISP_MASK = 0x03FFFFFC


def fail(message: str) -> NoReturn:
    print(f"ERROR: {message}", file=sys.stderr)
    raise SystemExit(1)


def find_nm() -> str:
    for candidate in (
        shutil.which("powerpc-eabi-nm"),
        "/opt/devkitpro/devkitPPC/bin/powerpc-eabi-nm",
    ):
        if candidate and Path(candidate).is_file():
            return str(candidate)
    fail("powerpc-eabi-nm not found")


def load_symbols(elf: Path) -> dict[str, int]:
    proc = subprocess.run(
        [find_nm(), "-n", str(elf)],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
    )

    wanted = {"SDL_malloc", "SDL_calloc"}
    result: dict[str, int] = {}

    for line in proc.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[2] in wanted:
            result[parts[2]] = int(parts[0], 16)

    missing = wanted - result.keys()
    if missing:
        fail("missing ELF symbol(s): " + ", ".join(sorted(missing)))

    return result


def text_sections(data: bytes):
    if len(data) < 0x100:
        fail("DOL too small")

    offsets = struct.unpack_from(">7I", data, 0x00)
    addrs = struct.unpack_from(">7I", data, 0x48)
    sizes = struct.unpack_from(">7I", data, 0x90)

    result = []
    for index, (offset, addr, size) in enumerate(zip(offsets, addrs, sizes)):
        if size == 0:
            continue
        if offset + size > len(data):
            fail(f"DOL text section {index} extends beyond file")
        result.append((index, offset, addr, size))
    return result


def sign_extend_branch(value: int) -> int:
    value &= BRANCH_DISP_MASK
    if value & 0x02000000:
        value -= 0x04000000
    return value


def decode_bl_target(instruction: int, pc: int) -> int:
    if (instruction & BRANCH_OPCODE_MASK) != BRANCH_OPCODE:
        fail(f"0x{pc:08x}: not a PowerPC b/bl instruction")
    if instruction & BRANCH_AA:
        fail(f"0x{pc:08x}: absolute branch not expected")
    if not (instruction & BRANCH_LK):
        fail(f"0x{pc:08x}: branch does not set LK")
    return (pc + sign_extend_branch(instruction)) & 0xFFFFFFFF


def encode_bl(pc: int, target: int) -> int:
    delta = target - pc
    if delta & 3:
        fail("SDL_calloc target is not 4-byte aligned")
    if delta < -0x02000000 or delta > 0x01FFFFFC:
        fail("SDL_calloc is outside relative branch range")
    return BRANCH_OPCODE | (delta & BRANCH_DISP_MASK) | BRANCH_LK


def find_candidates(data: bytes):
    result = []

    for section_index, section_offset, section_addr, section_size in text_sections(data):
        blob = data[section_offset:section_offset + section_size]
        start = 0

        while True:
            pos = blob.find(PREFIX, start)
            if pos < 0:
                break

            if pos + 24 <= len(blob):
                alloc = blob[pos + 8:pos + 12]
                call = struct.unpack(">I", blob[pos + 12:pos + 16])[0]
                suffix = blob[pos + 16:pos + 24]

                if (
                    alloc in (MULLI_R3_R3_108, LI_R4_108)
                    and suffix == SUFFIX
                    and (call & BRANCH_OPCODE_MASK) == BRANCH_OPCODE
                    and not (call & BRANCH_AA)
                    and (call & BRANCH_LK)
                ):
                    result.append(
                        {
                            "section": section_index,
                            "file_pos": section_offset + pos,
                            "runtime_pos": section_addr + pos,
                            "call_pc": section_addr + pos + 12,
                            "alloc": alloc,
                            "call": call,
                        }
                    )

            start = pos + 1

    return result


def atomic_replace(path: Path, data: bytes) -> None:
    mode = path.stat().st_mode

    with tempfile.NamedTemporaryFile(
        dir=path.parent,
        prefix=path.name + ".",
        delete=False,
    ) as temp:
        temp_path = Path(temp.name)
        temp.write(data)
        temp.flush()
        os.fsync(temp.fileno())

    os.chmod(temp_path, mode)
    os.replace(temp_path, path)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dol", required=True, type=Path)
    args = parser.parse_args()

    dol = args.dol.resolve()
    if not dol.is_file():
        fail(f"DOL missing: {dol}")

    elf = dol.with_suffix(".elf")
    if not elf.is_file():
        fail(f"companion ELF missing: {elf}")

    symbols = load_symbols(elf)
    malloc_addr = symbols["SDL_malloc"]
    calloc_addr = symbols["SDL_calloc"]

    print("DoomCube TiMidity Sample[] zero-init hotfix")
    print(f"  DOL        : {dol}")
    print(f"  ELF        : {elf}")
    print(f"  SDL_malloc : 0x{malloc_addr:08x}")
    print(f"  SDL_calloc : 0x{calloc_addr:08x}")

    data = bytearray(dol.read_bytes())
    candidates = find_candidates(data)

    if len(candidates) != 1:
        fail(f"expected exactly one TiMidity allocation pattern, found {len(candidates)}")

    candidate = candidates[0]
    base = candidate["file_pos"]
    old_target = decode_bl_target(candidate["call"], candidate["call_pc"])

    print(f"  site       : 0x{candidate['runtime_pos']:08x}")
    print(
        "  allocation : "
        + (
            "mulli r3,r3,108"
            if candidate["alloc"] == MULLI_R3_R3_108
            else "li r4,108"
        )
    )
    print(f"  call target: 0x{old_target:08x}")

    if candidate["alloc"] == LI_R4_108:
        if old_target != calloc_addr:
            fail("already-patched allocation does not call SDL_calloc")
        print("  status     : already patched; PASS")
        return 0

    if old_target != malloc_addr:
        fail(
            "stock allocation pattern does not call SDL_malloc "
            f"(0x{old_target:08x} != 0x{malloc_addr:08x})"
        )

    new_call = encode_bl(candidate["call_pc"], calloc_addr)

    data[base + 8:base + 12] = LI_R4_108
    data[base + 12:base + 16] = struct.pack(">I", new_call)

    if decode_bl_target(new_call, candidate["call_pc"]) != calloc_addr:
        fail("internal SDL_calloc branch verification failed")

    atomic_replace(dol, data)

    final_data = bytearray(dol.read_bytes())
    final_candidates = find_candidates(final_data)

    if len(final_candidates) != 1:
        fail("post-write TiMidity allocation pattern verification failed")

    final = final_candidates[0]
    final_target = decode_bl_target(final["call"], final["call_pc"])

    if final["alloc"] != LI_R4_108 or final_target != calloc_addr:
        fail("post-write DOL does not contain expected calloc fix")

    print("  patched    : li r4,108 + bl SDL_calloc")
    print("  status     : PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
