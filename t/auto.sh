#!/bin/bash

set -x -e

for ver_tag in `cat t/ngx_versions_list`; do

  # Checkout nginx version via tag
  cd nginx
  git checkout $ver_tag
  cd -

  for build_type in configure-for-testing configure; do

    # Build two configuration
    rm -f nginx/obj/nginx
    make $build_type
    make -j2

    # Test debug configuration
    if [ $build_type = "configure-for-testing" ]; then
      echo "[+] Start testing $ver_tag"
      ./nginx/objs/nginx 2> /dev/null &
      nginx_pid=$!
      tarantool t/test.lua 2> /dev/null &
      tnt_pid=$!
      ./t/run_all.sh
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
