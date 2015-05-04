#!/bin/bash

#set -x

WORK_DIR=$PWD/test

## Env
$PWD/nginx/objs/nginx 2> /dev/null &
cd $WORK_DIR > /dev/null
tarantool echo.lua 2> /dev/null &
cd - > /dev/null
sleep 2

## Test
for i in {1..20}; do
  $WORK_DIR/client.py 1> /dev/null || echo "client.py failed $i"
done

## Clean up
rm -f $WORK_DIR/*.xlog $WORK_DIR/*.snap >/dev/null
for job in `jobs -p`; do
  kill -s SIGTERM $job
done
for job in `jobs -p`; do
  wait $job
done
