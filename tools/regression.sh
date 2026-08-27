#!/usr/bin/env bash
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

TIMEOUT_SECONDS="${DOOMCUBE_REGRESSION_TIMEOUT:-120}"

DOLPHIN_APP_ID="org.DolphinEmu.dolphin-emu"

RTC_BASE=1704067200
RTC_SLOT=300

CASES=(
    "sigil-e5m6-secret|0|DoomCube: TEST PROGRESSION: E5M6 -> E5M9 (secret exit)"
    "sigil-e5m9-return|1|DoomCube: TEST PROGRESSION: E5M9 -> E5M7"
    "sigil2-e6m3-secret|2|DoomCube: TEST PROGRESSION: E6M3 -> E6M9 (secret exit)"
    "sigil2-e6m9-return|3|DoomCube: TEST PROGRESSION: E6M9 -> E6M4"
    "sigil-e5m8-finale|4|DoomCube: TEST FINALE SURVIVED: E5M8"
    "sigil2-e6m8-finale|5|DoomCube: TEST FINALE SURVIVED: E6M8"
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


cleanup()
{
    stop_dolphin
}


run_case()
{
    local name="$1"
    local index="$2"
    local expected="$3"

    local rtc=$((RTC_BASE + index * RTC_SLOT))
    local expected_case="DoomCube: REGRESSION CASE ${index}/"
    local log="$LOG_DIR/${name}.log"

    local pid
    local started
    local now
    local elapsed

    local result="FAIL"
    local reason=""

    echo
    echo "============================================================"
    echo " DoomCube regression: $name"
    echo " Case: $index"
    echo " RTC : $rtc"
    echo " ISO : $ISO"
    echo "============================================================"

    stop_dolphin

    flatpak run \
        --filesystem="$ROOT:ro" \
        "$DOLPHIN_APP_ID" \
        --batch \
        --config=Dolphin.Core.EnableCustomRTC=True \
        --config=Dolphin.Core.CustomRTCValue="$rtc" \
        --exec="$ISO" \
        >"$log" 2>&1 &

    pid=$!
    started="$(date +%s)"

    while :; do
        if grep -Eqi "$CRASH_RE" "$log" 2>/dev/null; then
            reason="crash/error marker found"
            break
        fi

        if grep -Fq "$expected_case" "$log" 2>/dev/null &&
           grep -Fq ": $name" "$log" 2>/dev/null &&
           grep -Fq "$expected" "$log" 2>/dev/null
        then
            result="PASS"
            reason="$expected"
            break
        fi

        if ! kill -0 "$pid" 2>/dev/null; then
            reason="Dolphin exited before success marker"
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
        echo "[PASS] $name"
        echo "       $reason"
        return 0
    fi

    echo "[FAIL] $name"
    echo "       $reason"
    echo
    echo "------- regression markers -------"

    grep -E \
        'REGRESSION RTC:|REGRESSION CASE|REGRESSION AUTOSELECT|REGRESSION WARP|TEST PROGRESSION|>>> I_ERROR:' \
        "$log" 2>/dev/null || true

    echo
    echo "------- log tail: $log -------"
    tail -80 "$log" 2>/dev/null || true
    echo "--------------------------------"

    return 1
}


trap cleanup EXIT
trap 'exit 130' INT TERM

LOG_DIR="$(
    mktemp -d \
        "${TMPDIR:-/tmp}/doomcube-regression.XXXXXX"
)"

echo
echo "DoomCube automated regression"
echo "Logs: $LOG_DIR"
echo "Timeout per test: ${TIMEOUT_SECONDS}s"

echo
echo "============================================================"
echo " Building single regression artifact"
echo "============================================================"
echo

if ! make clean; then
    echo "ERROR: make clean failed"
    exit 1
fi

if ! make REGRESSION=1; then
    echo "ERROR: regression DOL build failed"
    exit 1
fi

if ! make iso REGRESSION=1; then
    echo "ERROR: regression ISO build failed"
    exit 1
fi

ISO="$(
    find "$ROOT" \
        -maxdepth 1 \
        -type f \
        \( -name '*.iso' -o -name '*.gcm' \) \
        -printf '%T@ %p\n' 2>/dev/null |
    sort -nr |
    head -1 |
    cut -d' ' -f2-
)"

if [[ -z "$ISO" || ! -f "$ISO" ]]; then
    echo "ERROR: regression ISO not found"
    exit 1
fi

ISO_HASH_BEFORE="$(
    sha256sum "$ISO" |
    awk '{print $1}'
)"

echo
echo "Regression artifact:"
echo "  ISO    : $ISO"
echo "  SHA256 : $ISO_HASH_BEFORE"

passes=0
failures=0
selected=0

for entry in "${CASES[@]}"; do
    IFS='|' read -r name index expected <<< "$entry"

    if ! want_case "$name"; then
        continue
    fi

    selected=$((selected + 1))

    if run_case \
        "$name" \
        "$index" \
        "$expected"
    then
        passes=$((passes + 1))
    else
        failures=$((failures + 1))
    fi
done

ISO_HASH_AFTER="$(
    sha256sum "$ISO" |
    awk '{print $1}'
)"

echo
echo "============================================================"
echo " DoomCube regression complete"
echo " PASS   : $passes"
echo " FAIL   : $failures"
echo " ISO    : $ISO"
echo " SHA256 : $ISO_HASH_AFTER"
echo " Logs   : $LOG_DIR"
echo "============================================================"

if [[ "$ISO_HASH_BEFORE" != "$ISO_HASH_AFTER" ]]; then
    echo
    echo "ERROR: regression ISO changed during test suite"
    exit 1
fi

echo
echo "GOOD: every test used the exact same ISO"

if ((selected == 0)); then
    echo
    echo "ERROR: no matching regression cases selected"
    exit 2
fi

if ((failures != 0)); then
    exit 1
fi
