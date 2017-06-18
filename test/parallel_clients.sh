#!/bin/bash

for i in {1..10}; do
  ./test/basic_features.py &
  ./test/v20_features.py &
  ./test/v23_features.py &
  ./test/v24_features.py &
  ./test/eval_basic.py &
done

for i in `jobs -p`; do
  wait $i
done

