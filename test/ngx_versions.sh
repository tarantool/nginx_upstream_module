#!/bin/bash

set -x -e

for ver_tag in `cat test/ngx_version`; do

  # Checkout nginx version via tag
  cd nginx
  git checkout $ver_tag
  cd -

  for build_type in configure-debug configure; do

    # Build two configuration
    make $build_type
    make -j2


    # Test debug configuration
    if [ $build_type = "configure-debug" ]; then
      echo "[+] Start testing $ver_tag"
      ./nginx/objs/nginx 2> /dev/null &
      nginx_pid=$!
      tarantool test/test.lua 2> /dev/null &
      tnt_pid=$!
      ./test/run_all.sh
      for pid in $nginx_pid $tnt_pid; do
        echo "[+] Terminating $pid"
        kill -s TERM $pid
        wait $pid
      done
    fi

    # Clean an old build
    make clean
    echo "[+] Test - OK"
  done
done

echo "[+] All tests - OK"
