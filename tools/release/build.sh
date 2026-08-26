#!/bin/sh

set -eu

SCRIPT_DIR=$(
    CDPATH= cd -- "$(dirname -- "$0")" &&
    pwd
)

cd "$SCRIPT_DIR"

if command -v python3 >/dev/null 2>&1; then
    exec python3 "$SCRIPT_DIR/pack.py" "$@"
fi

if command -v python >/dev/null 2>&1; then
    exec python "$SCRIPT_DIR/pack.py" "$@"
fi

echo
echo "[ERROR] Python 3 was not found."
echo
echo "Install Python 3 and run build.sh again."
exit 1
