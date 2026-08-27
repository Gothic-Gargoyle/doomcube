#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PWAD="data/pwad/SIGIL_COMPAT_V1_23.wad"

if [[ ! -f "$PWAD" ]]; then
    echo "ERROR: SIGIL compat PWAD not found:"
    echo "  $PWAD"
    echo
    echo "Place SIGIL_COMPAT_V1_23.wad in data/pwad/ first."
    exit 1
fi

if (($# > 0)); then
    maps=("$@")
else
    maps=(1 2 3 4 5 6 7 8 9)
fi

for map in "${maps[@]}"; do
    if [[ ! "$map" =~ ^[1-9]$ ]]; then
        echo "ERROR: invalid SIGIL map: $map"
        exit 1
    fi

    echo
    echo "============================================================"
    echo " DoomCube SIGIL stress test"
    echo " E3M${map}"
    echo "============================================================"
    echo
    echo "In the launcher select:"
    echo "  DOOM"
    echo "  SIGIL_COMPAT_V1_23.wad"
    echo
    echo "The engine will then warp directly to E3M${map}."
    echo

    make test \
        WARP_EPISODE=3 \
        WARP_MAP="$map"

    echo
    echo "Completed E3M${map} test."
done

echo
echo "============================================================"
echo " SIGIL stress test complete"
echo "============================================================"
