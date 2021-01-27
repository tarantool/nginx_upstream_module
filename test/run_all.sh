#!/bin/bash

set -e #-x

WORK_DIR=test
NGINX_PREFIX=$PWD/test-root
LOG_PATH=$NGINX_PREFIX/logs


# v24_features and v26_features fail now. They should be added
# into this array with gh-144 fix.
declare -a test_files=("basic_features" "v20_features" "v23_features"
                       "v25_features" "v27_features")

echo "[+] Logs saved into $LOG_PATH."

echo "[+] Basic test"
for i in {1..10}; do
  echo "[+] try: $i"
  for test_file in "${test_files[@]}"; do
    $WORK_DIR/$test_file.py 1> $LOG_PATH/$test_file.log || (
        echo "[-] $WORK_DIR/$test_file.py failed" && exit 1
      )
  done
done
echo '[+] OK'


echo '[+] Parallel clients'
clients_pids=
for i in {1..3}; do
  echo "[+] try: $i"
  for test_file in "${test_files[@]}"; do
    `$WORK_DIR/$test_file.py 1> $LOG_PATH/"$i"_parallel_$test_file.log || (
        echo "[-] $WORK_DIR/$test_file.py failed" && exit 1
      )` &
    clients_pids="$clients_pids $!"
  done
done
for job in $clients_pids; do
  wait $job
done
echo '[+] OK'

