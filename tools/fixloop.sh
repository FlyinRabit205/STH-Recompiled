#!/usr/bin/env bash
# Boot-fix loop for the headless harness: run the game, and when it traps on an indirect call to code the analysis
# missed, add that address to [functions] in config/game.toml, recompile and run again.
# usage: tools/fixloop.sh <iterations> [seconds per run]
# env:   RECOMP (xbe_recomp), BUILD (harness out dir), GAMEDIR (extracted game files), RUN (run out dir)
set -u
cd "$(dirname "$0")/.."
N=${1:-10}; T=${2:-60}
RECOMP=${RECOMP:-/home/claude/toolbuild/build/xbe_recomp}
BUILD=${BUILD:-/home/claude/hb}
GAMEDIR=${GAMEDIR:-/home/claude/rungame}
RUN=${RUN:-/home/claude/run1}
CFG=config/game.toml

for ((i = 1; i <= N; i++)); do
    rm -rf "$RUN"
    bash sdk/harness/run.sh -x "$BUILD/shadow" -g "$GAMEDIR" -o "$RUN" -t "$T" >/dev/null 2>&1
    LOG=$(ls "$RUN"/1/logs/*.log | head -1)
    addrs=$(grep -o 'add 0x[0-9A-Fa-f]* to \[functions\]' "$LOG" | awk '{print $2}' | sort -u)
    if [ -z "$addrs" ]; then
        echo "run $i: no missing-function trap"
        grep -E '\[error\]' "$LOG" | head -5
        exit 0
    fi
    for a in $addrs; do
        if grep -qi "^$a *=" "$CFG"; then
            echo "run $i: $a already listed, stopping"; grep -E '\[error\]' "$LOG" | head -8; exit 1
        fi
        # [functions] takes address = "name" entries; keep them under the [functions] header
        sed -i "/^\[functions\]/a $a = \"sub_${a#0x}\"" "$CFG"
        echo "run $i: added $a"
    done
    "$RECOMP" "$CFG" --xdk sdk/xdk >/dev/null 2>&1 || { echo "recomp failed"; exit 1; }
    JOBS=$(nproc) bash sdk/harness/build.sh -r . -g generated/game -o "$BUILD" -n shadow -x texturedumper 2>&1 \
        | grep -E 'error' | head -5
done
echo "stopped after $N iterations"
