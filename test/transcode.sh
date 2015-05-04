#!/bin/bash

#set -x

CUR_PATH=$PWD
TRANSCODE_OFF=no
DUMP_RESULT_ONLY=yes

do_trasncode() {
  local in=$1
  local name=`basename $1`
  local result=`${CUR_PATH}/misc/json2tp --in-file=$in --out-file=$in.tmp 2>&1`
  if [[ x$result = x'' ]]; then
    result=`cat $in.tmp | ${CUR_PATH}/misc/tp_dump`
  fi

  if [[ x"$DUMP_RESULT_ONLY" = x"yes" ]]; then
    echo "$name -> $result" >> ${CUR_PATH}/test/cases/tc.out
    return;
  fi
}

####
rm -f ${CUR_PATH}/test/cases/tc.out > /dev/null
if [[ x"$TRANSCODE_OFF" != x"yes" ]]; then
  for _case in `ls ${CUR_PATH}/test/cases/*json`; do
    do_trasncode $_case
  done
fi

cat ${CUR_PATH}/test/cases/tc.out | sort > ${CUR_PATH}/test/cases/tc.out.sorted
d=`diff ${CUR_PATH}/test/cases/tc.out.sorted ${CUR_PATH}/test/cases/expected`
if [[ x"$d" = x'' ]]; then
  echo "[OK] transcode"
  exit 0
else
  echo "[ERROR] transcode"
  echo "$d"
  exit 1
fi
