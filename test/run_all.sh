#!/bin/bash

set -e #-x

WORK_DIR=test
NGINX_PREFIX=$PWD/test-root


echo "[+] Basic test"
for i in {1..10}; do
  echo "[+] try: $i"
  $WORK_DIR/basic_features.py 1> /dev/null || (
      echo "[-] $WORK_DIR/basic_features.py failed" && exit 1
    )
  $WORK_DIR/v20_features.py 1> /dev/null || (
      echo "[-] $WORK_DIR/v20_features.py failed" && exit 1
    )
  $WORK_DIR/v23_features.py 1> /dev/null || (
      echo "[-] $WORK_DIR/v23_features.py failed" && exit 1
    )
  # This test fails now. Should be returned with gh-144 fix.
  # $WORK_DIR/v24_features.py 1> /dev/null || (
  #     echo "[-] $WORK_DIR/v24_features.py failed" && exit 1
  #   )
#  $WORK_DIR/lua.py 1> /dev/null || (
#      echo "[-] $WORK_DIR/lua.py failed" && exit 1
#    )
  $WORK_DIR/v25_features.py 1> /dev/null || (
      echo "[-] $WORK_DIR/v25_features.py failed" && exit 1
    )
  # This test fails now. Should be returned with gh-144 fix.
  # $WORK_DIR/v26_features.py 1> /dev/null || (
  #     echo "[-] $WORK_DIR/v26_features.py failed" && exit 1
  #   )
  $WORK_DIR/v27_features.py 1> /dev/null || (
      echo "[-] $WORK_DIR/v26_features.py failed" && exit 1
    )
done
echo '[+] OK'


echo '[+] Parallel clients'
clients_pids=
for i in {1..3}; do
  `$WORK_DIR/basic_features.py 1> /dev/null || (
      echo "[-] $WORK_DIR/basic_features.py failed" && exit 1
    )` &
  clients_pids="$clients_pids $!"
  `$WORK_DIR/v20_features.py 1> /dev/null || (
      echo "[-] $WORK_DIR/v20_features.py failed" && exit 1
    )` &
  clients_pids="$clients_pids $!"
  `$WORK_DIR/v23_features.py 1> /dev/null || (
      echo "[-] $WORK_DIR/v23_features.py failed" && exit 1
    )` &
  # This test fails now. Should be returned with gh-144 fix.
  # clients_pids="$clients_pids $!"
  # `$WORK_DIR/v24_features.py 1> /dev/null || (
  #     echo "[-] $WORK_DIR/v24_features.py failed" && exit 1
  #   )` &
#  clients_pids="$clients_pids $!"
#  `$WORK_DIR/lua.py 1> /dev/null || (
#      echo "[-] $WORK_DIR/lua.py failed" && exit 1
#    )` &
  $WORK_DIR/v25_features.py 1> /dev/null || (
      echo "[-] $WORK_DIR/v25_features.py failed" && exit 1
    ) &

  # XXX It couldn't be work in parallel
  #$WORK_DIR/v26_features.py 1> /dev/null || (
  #    echo "[-] $WORK_DIR/v26_features.py failed" && exit 1
  #  )
  $WORK_DIR/v27_features.py 1> /dev/null || (
      echo "[-] $WORK_DIR/v26_features.py failed" && exit 1
    )
  clients_pids="$clients_pids $!"
done
for job in $clients_pids; do
  wait $job
done
echo '[+] OK'

