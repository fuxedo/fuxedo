#!/bin/bash
export TUXDIR=/home/aivarsk/Github/fuxedo/tmp/dest/usr/local
export LD_LIBRARY_PATH=$TUXDIR/lib
export PATH=$TUXDIR/bin:$PATH
export TUXCONFIG=$PWD/tuxconfig
make tuxconfig client server >/dev/null 2>&1
tmshutdown -y >/dev/null 2>&1
for r in s q m; do for id in $(ipcs -$r | grep "^0x00008001" | awk '{print $2}'); do ipcrm -$r $id 2>/dev/null; done; done
tmboot -y >/dev/null 2>&1
sleep 3
./client "$@"
rc=$?
tmshutdown -y >/dev/null 2>&1
exit $rc
