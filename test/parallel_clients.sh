#!/bin/bash

for i in {1..10}; do
  ./t/basic_features.py &
  ./t/v20_features.py &
  ./t/v23_features.py &
  ./t/v24_features.py &
#  ./t/lua.py &
  ./t/v25_features.py &
  ./t/v26_features.py
done

for i in `jobs -p`; do
  wait $i
done

