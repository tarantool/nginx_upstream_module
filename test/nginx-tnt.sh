#!/bin/bash

set -e #-x

WORK_DIR=$PWD/test
NGINX_PREFIX=$PWD/test-root
NGINX="$PWD/nginx/objs/nginx -p $NGINX_PREFIX"

## Env
if [ -e $NGINX_PREFIX/logs/nginx.pid ]; then
  `$NGINX -s stop` 2> /dev/null
fi
`$NGINX`

cd $WORK_DIR 1> /dev/null
tarantool test.lua 2> /dev/null &
cd - 1> /dev/null
sleep 1

## Test
echo "[+] Testing ..."
for i in {1..50}; do
  $WORK_DIR/client.py 1> /dev/null || (
      echo "[-] $WORK_DIR/client.py failed, at: $i" && exit 1
    )
done

## Clean up
`$NGINX -s stop`

rm -f $WORK_DIR/*.xlog $WORK_DIR/*.snap >/dev/null
for job in `jobs -p`; do
  kill -s TERM $job
done
for job in `jobs -p`; do
  wait $job
done

echo "[+] OK"

exit 0
