#!/bin/bash

for i in {1..10}; do
  ./test/basic_features.py &
  ./test/v20_features.py &
done

for i in `jobs -p`; do
  wait $i
done

