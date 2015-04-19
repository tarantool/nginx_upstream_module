#!/bin/bash

#set -x

WORK_DIR=$PWD/test

## Env
$PWD/nginx/objs/nginx 2> /dev/null &
cd $WORK_DIR
tarantool echo.lua 2> /dev/null &
cd -
sleep 2

## Test
$WORK_DIR/client.py

## Clean up
rm -f $WORK_DIR/*.xlog $WORK_DIR/*.snap >/dev/null
for job in `jobs -p`; do
  kill -s SIGTERM $job
done
for job in `jobs -p`; do
  wait $job
done
