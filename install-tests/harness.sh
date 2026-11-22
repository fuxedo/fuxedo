#!/bin/bash
# fuxedo domain test harness: one clean cycle (wipe, load, boot,
# readiness-wait, driver, shutdown) with fail-fast diagnostics.
# Ends the "boot seems fine but the domain is empty" and "timeouts
# everywhere after a partial death" classes of debugging pain.
#
# Usage:
#   harness.sh run DIR [DRIVER...]     full cycle; DRIVER runs when ready
#   harness.sh wipe                    kill + remove every IPC object
#   harness.sh wait DIR [SECS]         wait for the domain in DIR to be ready
#
# Environment (must be exported by the caller):
#   TUXDIR, TUXCONFIG-less (derived from DIR), FLDTBLDIR/FIELDTBLS as the
#   domain needs, plus whatever the binaries need at runtime.
set -u

wipe() {
  tmshutdown -y >/dev/null 2>&1
  pkill -9 -x BBL 2>/dev/null
  for p in TMS_SQL TMS_QM TMS ACCT BAL BALC BTADD TLR XFER AUDITC TMQUEUE \
           TMQFORWARD WSL GWTDOMAIN convsvr apisvr advsvr server svr \
           worker restartme pump bankmgr; do
    pkill -9 -x "$p" 2>/dev/null
  done
  sleep 2
  ipcrm -a 2>/dev/null
  sleep 1
}

expected_servers() {
  # count bootable servers from the loaded config
  tmunloadcf 2>/dev/null |
    awk '/^\*SERVERS/{f=1;next} /^\*[A-Z]/{f=0} f' |
    grep -vE "^\*|^$" | wc -l
}

domain_ready() {
  # every configured server ACTive in psc
  local want have
  want=$(expected_servers)
  [ "$want" -eq 0 ] && return 1
  have=$(echo "psc" | tmadmin 2>/dev/null | grep -c "ACT")
  [ "$have" -ge "$want" ]
}

any_server_dead() {
  # BBL gone means the domain is gone
  pgrep -x BBL >/dev/null 2>&1 || return 0
  return 1
}

wait_ready() {
  local dir=$1 secs=${2:-90} i
  for ((i = 0; i < secs; i++)); do
    if any_server_dead; then
      echo "harness: a server process died during boot" >&2
      tail -n 15 "$dir"/ULOG* 2>/dev/null >&2
      return 1
    fi
    if domain_ready; then
      return 0
    fi
    sleep 1
  done
  echo "harness: domain not ready after ${secs}s" >&2
  echo "harness: psc:" >&2
  echo "psc" | tmadmin 2>/dev/null >&2
  tail -n 15 "$dir"/ULOG* 2>/dev/null >&2
  return 1
}

cmd=$1
shift
case "$cmd" in
wipe)
  wipe
  ;;
run)
  dir=$1; shift
  [ -d "$dir" ] || { echo "harness: no such dir $dir" >&2; exit 2; }
  cd "$dir" || exit 2
  export TUXCONFIG=$PWD/tuxconfig
  [ -f UBB ] || { echo "harness: $dir/UBB missing" >&2; exit 2; }
  wipe
  if ! tmloadcf -y UBB > tmloadcf.log 2>&1; then
    echo "harness: tmloadcf failed:" >&2
    cat tmloadcf.log >&2
    exit 1
  fi
  tmboot -y > tmboot.log 2>&1
  if ! wait_ready "$dir" "${HARNESS_WAIT:-90}"; then
    exit 1
  fi
  rc=0
  if [ $# -gt 0 ]; then
    "$@" || rc=$?
  fi
  tmshutdown -y >> tmboot.log 2>&1
  exit "$rc"
  ;;
wait)
  dir=$1; shift
  cd "$dir" || exit 2
  export TUXCONFIG=$PWD/tuxconfig
  wait_ready "$dir" "${1:-90}"
  ;;
*)
  echo "usage: harness.sh {wipe|run DIR [CMD...]|wait DIR [SECS]}" >&2
  exit 2
  ;;
esac
