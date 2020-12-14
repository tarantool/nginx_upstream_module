#!/bin/bash

#set -e #-x

TRANSCODE_OFF=no
DUMP_RESULT_ONLY=yes

do_trasncode() {
  local in=$1
  local name=`basename $1`
  local result=`${PWD}/misc/json2tp --in-file=$in --out-file=$in.tmp 2>&1`
  if [[ x$result = x'' ]]; then
    result=`cat $in.tmp | ${PWD}/misc/tp_dump`
  fi

  if [[ x"$DUMP_RESULT_ONLY" = x"yes" ]]; then
    echo "$name -> $result" >> ${PWD}/t/cases/tc.out
    return;
  fi
}

####
echo "[+] Testing ..."
rm -f ${PWD}/t/cases/tc.out > /dev/null
if [[ x"$TRANSCODE_OFF" != x"yes" ]]; then
  for _case in `ls ${PWD}/t/cases/*json`; do
    do_trasncode $_case
  done
fi

cat ${PWD}/t/cases/tc.out | sort > ${PWD}/t/cases/tc.out.sorted
diff_result=`diff ${PWD}/t/cases/tc.out.sorted \
                  ${PWD}/t/cases/expected`
if [[ ! x"$diff_result" = x'' ]]; then
  echo "[-] $diff_result"
  exit 1
fi

echo "[+] OK"
