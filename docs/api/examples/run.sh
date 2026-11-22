#!/bin/sh
# Boot the sample domain and run every example program.
set -e

# /Q examples: device in ./QUE, space and queues must exist before
# tmboot so TMQUEUE can open the space on both platforms; scrub stale
# queue IPC so the space ipckey is free
rm -rf QUE TLOG
export QMCONFIG="$(pwd)/QUE"
for id in $(ipcs -q | awk 'NR>3{print $2}'); do ipcrm -q "$id" 2>/dev/null || true; done
for id in $(ipcs -s | awk 'NR>3{print $2}'); do ipcrm -s "$id" 2>/dev/null || true; done
for id in $(ipcs -m | awk 'NR>3{print $2}'); do ipcrm -m "$id" 2>/dev/null || true; done
qipc=$((($$ % 30000) + 20001))
printf 'crdl %s 0 200000\nqspacecreate -n 100B\nDOCS\n%d\n100\n6\n4\n9\n3\nERRQ\nn\n16\nqopen DOCS\nqcreate TASKS fifo none 2 30 100%% 0%% -\nqcreate PINGRPLY fifo none 2 30 100%% 0%% -\n' "$QMCONFIG" "$qipc" | \
	qmadmin "$QMCONFIG"

# the platform flow: allocate the log device with crdl -z, then
# initialize it with crlog, both before boot
printf 'crdl -b 200 -z %s\ncrlog -m tuxapp\nq\n' "$(pwd)/TLOG" | tmadmin

tmboot -y
sleep 3

trap 'tmshutdown -y 2>/dev/null; exit 1' EXIT HUP INT TERM

./tpcall "mixed Case"
./tpcall FORWARD "mixed Case"
./acall "mixed Case"
./cancel anything
./buffers
./session "mixed Case"
./txdemo
./tpcall GREET "hello there"
./tpcall GREETX "hello there"
./fmldemo
./conv
./prio

# /Q examples: the queues were created before the boot above
./enqueue
./dequeue

trap - EXIT HUP INT TERM
tmshutdown -y
echo "api examples: all programs passed"
