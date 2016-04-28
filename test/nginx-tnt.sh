#!/bin/bash

set -e #-x

WORK_DIR=$PWD/test
NGINX_PREFIX=$PWD/test-root

## Tests
echo "[+] Testing ..."

for i in {1..10}; do
  echo "[+] try: $i"
  $WORK_DIR/basic_features.py 1> /dev/null || (
      echo "[-] $WORK_DIR/basic_features.py failed" && exit 1
    )
  $WORK_DIR/v20_features.py 1> /dev/null || (
      echo "[-] $WORK_DIR/v20_features.py failed" && exit 1
    )
done

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
done
for job in $clients_pids; do
  wait $job
done

