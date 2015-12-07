#!/bin/bash

set -e #-x

WORK_DIR=$PWD/test
NGINX_PREFIX=$PWD/test-root
NGINX="$PWD/nginx/objs/nginx -p $NGINX_PREFIX"

cleanup()
{
  `$NGINX -s stop`

  rm -f $WORK_DIR/*.xlog $WORK_DIR/*.snap >/dev/null

  for job in `jobs -p`; do
    kill -s TERM $job
  done
  for job in `jobs -p`; do
    wait $job
  done

  echo '[+] Done.'
}
trap cleanup EXIT

rm -f $WORK_DIR/*.xlog $WORK_DIR/*.snap >/dev/null

## Env
if [ -e $NGINX_PREFIX/logs/nginx.pid ]; then
  `$NGINX -s stop` 2> /dev/null
fi
`$NGINX`

cd $WORK_DIR 1> /dev/null
tarantool test.lua > /dev/null &
cd - 1> /dev/null
sleep 2

echo "[+] Testing ..."

## Tests
for i in {1..10}; do
  echo "[+] try: $i"
  $WORK_DIR/client.py 1> /dev/null || (
      echo "[-] $WORK_DIR/client.py failed" && exit 1
    )
done

clients_pids=
for i in {1..3}; do
  `$WORK_DIR/client.py 1> /dev/null || (
      echo "[-] $WORK_DIR/client.py failed" && exit 1
    )` &
    clients_pids="$clients_pids $!"
done
for job in $clients_pids; do
  wait $job
done

