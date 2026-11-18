# wait until restartme is back (BBL auto-restart), fail after 60s
i=0
while [ $i -lt 60 ]; do
  if pgrep -x restartme >/dev/null 2>&1; then
    # give tpsvrinit a moment, then require the service to answer
    sleep 1
    exit 0
  fi
  i=$((i+1))
  sleep 1
done
echo "restartme did not come back" >&2
exit 1
