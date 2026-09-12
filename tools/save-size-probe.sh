#!/usr/bin/env bash

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

DOLPHIN_APP_ID="org.DolphinEmu.dolphin-emu"

RTC_BASE=1704067200
RTC_SLOT=300

TIMEOUT_SECONDS="${DOOMCUBE_SAVE_PROBE_TIMEOUT:-120}"

CASES=(
    "save-doom1-e1m1|6"
    "save-doom-e1m1|7"
    "save-doom2-map01|8"
    "save-tnt-map01|9"
    "save-plutonia-map01|10"
    "save-sigil-e5m1|11"
    "save-sigil2-e6m1|12"
    "save-sigil-e5m2|13"
    "save-sigil-e5m3|14"
    "save-sigil-e5m4|15"
    "save-sigil-e5m5|16"
    "save-sigil-e5m6|17"
    "save-sigil-e5m7|18"
    "save-sigil-e5m8|19"
    "save-sigil-e5m9|20"
    "save-sigil2-e6m2|21"
    "save-sigil2-e6m3|22"
    "save-sigil2-e6m4|23"
    "save-sigil2-e6m5|24"
    "save-sigil2-e6m6|25"
    "save-sigil2-e6m7|26"
    "save-sigil2-e6m8|27"
    "save-sigil2-e6m9|28"
)

CRASH_RE='Invalid read from 0x|probably would have crashed|>>> I_ERROR:|Segmentation fault|core dumped|Unhandled exception'

FILTERS=("$@")


want_case()
{
    local name="$1"
    local filter

    if ((${#FILTERS[@]} == 0)); then
        return 0
    fi

    for filter in "${FILTERS[@]}"; do
        if [[ "$name" == "$filter" ]]; then
            return 0
        fi
    done

    return 1
}


stop_dolphin()
{
    flatpak kill "$DOLPHIN_APP_ID" \
        >/dev/null 2>&1 || true
}


prepare_isolated_user()
{
    local normal_card=""
    local normal_config=""
    local normal_gc=""
    local base

    USERDIR="$LOG_DIR/dolphin-user"

    for base in \
        "$HOME/.var/app/org.DolphinEmu.dolphin-emu/config/dolphin-emu" \
        "$HOME/.local/share/dolphin-emu/Config"
    do
        if [[ -d "$base" && -f "$base/Dolphin.ini" ]]; then
            normal_config="$base"
            break
        fi
    done

    if [[ -z "$normal_config" ]]; then
        echo "ERROR: could not locate normal Dolphin Config directory"
        return 1
    fi

    normal_card="$(
        {
            for base in \
                "$HOME/.var/app/org.DolphinEmu.dolphin-emu/data/dolphin-emu/GC" \
                "$HOME/.local/share/dolphin-emu/GC"
            do
                if [[ -d "$base" ]]; then
                    find "$base" \
                        -maxdepth 2 \
                        -type f \
                        -name 'MemoryCardA*.raw' \
                        -printf '%T@ %p\n' \
                        2>/dev/null || true
                fi
            done
        } \
            | LC_ALL=C sort -nr \
            | head -1 \
            | cut -d' ' -f2-
    )"

    if [[ -z "$normal_card" || ! -f "$normal_card" ]]; then
        echo "ERROR: could not locate source Memory Card A for isolated copy"
        return 1
    fi

    NORMAL_CARD_SOURCE="$normal_card"
    NORMAL_CARD_SOURCE_SHA="$(
        sha256sum "$NORMAL_CARD_SOURCE" |
        awk '{print $1}'
    )"

    normal_gc="$(dirname "$NORMAL_CARD_SOURCE")"

    mkdir -p "$USERDIR"

    cp -a \
        "$normal_config" \
        "$USERDIR/Config"

    mkdir -p "$USERDIR/GC"

    cp -f \
        "$NORMAL_CARD_SOURCE" \
        "$USERDIR/GC/$(basename "$NORMAL_CARD_SOURCE")"

    ISOLATED_CARD="$USERDIR/GC/$(basename "$NORMAL_CARD_SOURCE")"

    # Preserve the normal Dolphin frontend/logger/controller behaviour, but
    # redirect any explicit reference to the normal GC directory into this
    # disposable user. Default relative card paths automatically follow -u.
    python3 -B - \
        "$USERDIR/Config" \
        "$normal_gc" \
        "$USERDIR/GC" \
        "$NORMAL_CARD_SOURCE" \
        "$ISOLATED_CARD" <<'PYCFG'
from pathlib import Path
import sys

config_dir = Path(sys.argv[1])
normal_gc = sys.argv[2]
isolated_gc = sys.argv[3]
normal_card = sys.argv[4]
isolated_card = sys.argv[5]

for path in config_dir.rglob("*"):
    if not path.is_file():
        continue

    try:
        raw = path.read_text()
    except (UnicodeDecodeError, OSError):
        continue

    rewritten = raw.replace(normal_card, isolated_card)
    rewritten = rewritten.replace(normal_gc, isolated_gc)

    if rewritten != raw:
        path.write_text(rewritten)
PYCFG

    if grep -RIFq -- "$NORMAL_CARD_SOURCE" "$USERDIR/Config"; then
        echo "ERROR: isolated Config still references normal Memory Card path"
        return 1
    fi

    if grep -RIFq -- "$normal_gc" "$USERDIR/Config"; then
        echo "ERROR: isolated Config still references normal GC directory"
        return 1
    fi

    echo
    echo "Isolated Dolphin user:"
    echo "  $USERDIR"
    echo "Normal Config copied from:"
    echo "  $normal_config"
    echo "Source Memory Card A (copy-only):"
    echo "  $NORMAL_CARD_SOURCE"
    echo "Disposable Memory Card A:"
    echo "  $ISOLATED_CARD"
    echo "Source SHA256:"
    echo "  $NORMAL_CARD_SOURCE_SHA"
    echo
}

REGRESSION_BUILD_STARTED=0


cleanup()
{
    local rc=$?

    stop_dolphin

    if ((REGRESSION_BUILD_STARTED)); then
        echo
        echo "Removing regression build products so normal builds cannot reuse"
        echo "objects compiled with DOOMCUBE_REGRESSION..."

        make --no-print-directory clean \
            >/dev/null 2>&1 || true
    fi

    return "$rc"
}

trap cleanup EXIT
trap 'exit 130' INT TERM

LOG_DIR="$(
    mktemp -d \
        "${TMPDIR:-/tmp}/doomcube-save-probe.XXXXXX"
)"

prepare_isolated_user || exit 1

BUILD_LOG="$LOG_DIR/build.log"

echo
echo "============================================================"
echo " DoomCube automated save-size probe"
echo "============================================================"
echo
echo "Logs: $LOG_DIR"
echo
echo "Uses Doom's real G_SaveGame() serializer."
echo "All cases use one unchanged GameCube image."
echo

echo "Building single save-probe artifact..."

REGRESSION_BUILD_STARTED=1

if ! make clean >"$BUILD_LOG" 2>&1; then
    echo "ERROR: make clean failed"
    tail -100 "$BUILD_LOG"
    exit 1
fi

if ! make REGRESSION=1 >>"$BUILD_LOG" 2>&1; then
    echo "ERROR: regression build failed"
    tail -120 "$BUILD_LOG"
    exit 1
fi

if ! make iso REGRESSION=1 >>"$BUILD_LOG" 2>&1; then
    echo "ERROR: ISO build failed"
    tail -120 "$BUILD_LOG"
    exit 1
fi

ISO="$(
    find "$ROOT" \
        -maxdepth 1 \
        -type f \
        -name 'doomcube-*.iso' \
        -printf '%T@ %p\n' \
        | sort -nr \
        | head -1 \
        | cut -d' ' -f2-
)"

if [[ -z "$ISO" || ! -f "$ISO" ]]; then
    echo "ERROR: could not locate newly-built ISO"
    exit 1
fi

SHA_BEFORE="$(
    sha256sum "$ISO" |
        awk '{print $1}'
)"

echo
echo "Artifact:"
echo "  ISO    : $ISO"
echo "  SHA256 : $SHA_BEFORE"
echo

PASS_COUNT=0
FAIL_COUNT=0

RESULT_NAMES=()
RESULT_BYTES=()
RESULT_BLOCKS=()
RESULT_ZBYTES=()
RESULT_ZBLOCKS=()
RESULT_RATIO=()

for entry in "${CASES[@]}"; do
    IFS='|' read -r name index <<<"$entry"

    if ! want_case "$name"; then
        continue
    fi

    rtc=$((RTC_BASE + index * RTC_SLOT))
    log="$LOG_DIR/${name}.log"

    expected_case="DoomCube: REGRESSION CASE ${index}/"

    echo "------------------------------------------------------------"
    echo "Probe: $name"
    echo "Case : $index"
    echo "RTC  : $rtc"
    echo "------------------------------------------------------------"

    stop_dolphin

    flatpak run \
        --filesystem="$ROOT:ro" \
        --filesystem="$USERDIR" \
        "$DOLPHIN_APP_ID" \
        -u "$USERDIR" \
        --audio_emulation=LLE \
        --batch \
        --config=Dolphin.Core.EnableCustomRTC=True \
        --config=Dolphin.Core.CustomRTCValue="$rtc" \
        --exec="$ISO" \
        >"$log" 2>&1 &

    pid=$!
    started="$(date +%s)"

    result="FAIL"
    reason=""

    while :; do
        if grep -Eqi "$CRASH_RE" "$log" 2>/dev/null; then
            reason="crash/error marker found"
            break
        fi

        if grep -Fq "$expected_case" "$log" 2>/dev/null &&
           grep -Fq ": $name" "$log" 2>/dev/null &&
           grep -Eq \
               'DoomCube: SAVE PROBE DEFLATE: raw=[0-9]+ compressed=[0-9]+' \
               "$log" 2>/dev/null
        then
            result="PASS"
            reason="serializer completed"
            break
        fi

        if ! kill -0 "$pid" 2>/dev/null; then
            reason="Dolphin exited before save completed"
            break
        fi

        now="$(date +%s)"
        elapsed=$((now - started))

        if ((elapsed >= TIMEOUT_SECONDS)); then
            reason="timeout after ${TIMEOUT_SECONDS}s"
            break
        fi

        sleep 0.5
    done

    stop_dolphin
    wait "$pid" 2>/dev/null || true

    if [[ "$result" == "PASS" ]]; then
        size_line="$(
            grep -E \
                'temporary Doom save complete \([0-9]+ bytes\)' \
                "$log" \
                | tail -1 \
                || true
        )"

        bytes="$(
            printf '%s\n' "$size_line" |
                sed -n \
                    's/.*temporary Doom save complete (\([0-9][0-9]*\) bytes).*/\1/p'
        )"

        if [[ -z "$bytes" ]]; then
            size_line="$(
                grep -E \
                    'DoomCube: .* saved: [0-9]+ bytes' \
                    "$log" \
                    | tail -1 \
                    || true
            )"

            bytes="$(
                printf '%s\n' "$size_line" |
                    sed -n \
                        's/.* saved: \([0-9][0-9]*\) bytes.*/\1/p'
            )"
        fi

        if [[ -z "$bytes" ]]; then
            echo "[FAIL] $name"
            echo "       save committed but byte count missing"

            FAIL_COUNT=$((FAIL_COUNT + 1))
            echo
            continue
        fi

        zline="$(
            grep -E \
                'DoomCube: SAVE PROBE DEFLATE: raw=[0-9]+ compressed=[0-9]+' \
                "$log" \
                | tail -1 \
                || true
        )"

        zbytes="$(
            printf '%s\n' "$zline" |
                sed -n \
                    's/.* compressed=\([0-9][0-9]*\).*/\1/p'
        )"

        if [[ -z "$zbytes" ]]; then
            echo "[FAIL] $name"
            echo "       raw save succeeded but DEFLATE measurement missing"

            FAIL_COUNT=$((FAIL_COUNT + 1))

            echo
            grep -E \
                'SAVE PROBE|temporary Doom save|saved:' \
                "$log" \
                2>/dev/null \
                || true

            echo
            continue
        fi

        blocks=$(((bytes + 8191) / 8192))
        zblocks=$(((zbytes + 8191) / 8192))

        ratio="$(
            awk \
                -v raw="$bytes" \
                -v compressed="$zbytes" \
                'BEGIN {
                    printf "%.1f", (compressed * 100.0) / raw
                }'
        )"

        RESULT_NAMES+=("$name")
        RESULT_BYTES+=("$bytes")
        RESULT_BLOCKS+=("$blocks")
        RESULT_ZBYTES+=("$zbytes")
        RESULT_ZBLOCKS+=("$zblocks")
        RESULT_RATIO+=("$ratio")

        PASS_COUNT=$((PASS_COUNT + 1))

        echo "[PASS] $name"
        echo "       raw       : $bytes bytes ($blocks blocks)"
        echo "       deflated  : $zbytes bytes ($zblocks blocks)"
        echo "       size      : ${ratio}% of raw"
    else
        FAIL_COUNT=$((FAIL_COUNT + 1))

        echo "[FAIL] $name"
        echo "       $reason"
        echo
        echo "------- useful markers -------"

        grep -E \
            'REGRESSION CASE|REGRESSION AUTOSELECT|REGRESSION WARP|SAVE PROBE|temporary Doom save|saved:|commit|>>> I_ERROR:' \
            "$log" \
            2>/dev/null \
            || true

        echo
        echo "------- log tail -------"

        tail -80 "$log" 2>/dev/null || true

        echo "------------------------"
    fi

    echo
done

SHA_AFTER="$(
    sha256sum "$ISO" |
        awk '{print $1}'
)"

echo "============================================================"
echo " Save-size results"
echo "============================================================"

printf '%-24s %10s %7s %10s %7s %8s\n' \
    "CASE" \
    "RAW" \
    "BLOCKS" \
    "DEFLATE" \
    "BLOCKS" \
    "% RAW"

printf '%-24s %10s %7s %10s %7s %8s\n' \
    "------------------------" \
    "----------" \
    "-------" \
    "----------" \
    "-------" \
    "--------"

for ((i = 0; i < ${#RESULT_NAMES[@]}; ++i)); do
    printf '%-24s %10s %7s %10s %7s %7s%%\n' \
        "${RESULT_NAMES[$i]}" \
        "${RESULT_BYTES[$i]}" \
        "${RESULT_BLOCKS[$i]}" \
        "${RESULT_ZBYTES[$i]}" \
        "${RESULT_ZBLOCKS[$i]}" \
        "${RESULT_RATIO[$i]}"
done

echo
echo "PASS   : $PASS_COUNT"
echo "FAIL   : $FAIL_COUNT"
echo "ISO    : $ISO"
echo "SHA256 : $SHA_AFTER"
echo "Logs   : $LOG_DIR"
echo

if [[ "$SHA_BEFORE" == "$SHA_AFTER" ]]; then
    echo "GOOD: every probe used the exact same ISO"
else
    echo "ERROR: ISO changed during probe"
    FAIL_COUNT=$((FAIL_COUNT + 1))
fi

if ((FAIL_COUNT != 0)); then
    exit 1
fi
