#!/bin/bash
# evrun.sh <run dir> [env...]: headless run from the user's save into event1101 (about 40 s), then stop
R=$1; shift
pkill -x shadow; rm -rf $R && mkdir -p $R/hdd/E/UDATA $R/hdd/E/TDATA/53450086 && cp -r /home/claude/save/53450086 $R/hdd/E/UDATA/
cd $R && (env "$@" timeout 600 /home/claude/hb/shadow /home/claude/rungame > run.out 2>&1 &)
sleep 9; for i in 1 2 3 4 5 6; do echo "START 4" > cmd; sleep 2; done; sleep 2; echo "A 4" > cmd; sleep 3; echo "START 4" > cmd; sleep ${WAIT:-14}
pkill -x shadow
