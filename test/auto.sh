#!/bin/bash

set -x -e
WORK_DIR=test

for ver_tag in `cat $WORK_DIR/ngx_versions_list`; do

  # Checkout nginx version via tag
  cd nginx
  git checkout $ver_tag
  cd -

  rm -f nginx/objs/nginx
  make configure-for-testing
  make -j2

  echo "[+] Start testing $ver_tag"
  ./nginx/objs/nginx 2> /dev/null &
  nginx_pid=$!
  tarantool $WORK_DIR/test.lua 2> /dev/null &
  tnt_pid=$!
  sleep 2
  ./$WORK_DIR/run_all.sh
  for pid in $nginx_pid $tnt_pid; do
    echo "[+] Terminating $pid"
    kill -s TERM $pid
    wait $pid
  done

  # Clean build.
  make clean
  echo "[+] Test - OK"
done

echo "[+] All tests - OK"
