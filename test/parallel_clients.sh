#!/bin/bash

for i in {1..10}; do
  ./test/client.py &
done

for i in `jobs -p`; do
  wait $i
done

