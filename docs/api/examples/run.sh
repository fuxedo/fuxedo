#!/bin/sh
# Boot the sample domain and run every example program.
set -e

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

trap - EXIT HUP INT TERM
tmshutdown -y
echo "api examples: all programs passed"
