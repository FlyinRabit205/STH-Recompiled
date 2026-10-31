#!/bin/bash
# dis.sh <hexva> [before] [after]: disassemble .text around an address
va=$((16#$1)); b=${2:-40}; a=${3:-8}
objdump -D -b binary -m i386 --adjust-vma=$((0x11000-0x1000)) --start-address=$((va-b)) --stop-address=$((va+a)) /home/claude/game/extracted/default.xbe | tail -n +8
