#!/bin/bash

#
# Notice
#
# 1. Tarantool conf {{{
# box.cfg {listen = 3113}
# tester = box.schema.space.create('tester', {if_not_exists=true})
# index = tester:create_index('pk', {parts={1, 'str'}})
# print ('SPACE_ID = ', tester.id)
# print ('INDEX_ID = ', index.id)
# }}}
#
# 2. nginx.conf {{{
#
# upstream tnt {
#  server 127.0.0.1:3113;
#}
#
# location /api/v1/kv {
#
#      tnt_http_rest_methods get post put delete patch;
#
#      if ($request_method = GET) {
#        tnt_select {SPACE_ID} {INDEX_ID} 0 100 all "key=%s";
#      }
#      if ($request_method = POST) {
#        tnt_insert {SPACE_ID} "key=%s,value=%n";
#      }
#      if ($request_method = PUT) {
#        tnt_update {SPACE_ID} "key=%ks" "value=%n";
#      }
#      if ($request_method = DELETE) {
#       tnt_delete {SPACE_ID} {INDEX_ID} "key=%s";
#     }
#     if ($request_method = PATCH) {
#       tnt_upsert {SPACE_ID} "key=%s,new_value=%n" "updated_value=%on";
#       tnt_pass tnt;
#     }
#
#     tnt_pass tnt;
#  }

req_url='http://127.0.0.1:8081/api/v1/kv'

# Insert values into the tester
echo Inserted = `curl -s -X POST -d 'key=my_key_5' -d 'value=1' $req_url`
echo inserted = `curl -s -X POST $req_url?"key=my_key_6&value=2"`

# Update values into the tester

# %23 is urencode('#'). Delete field from the tuple
echo Updated = `curl -s -X PUT $req_url?"key=my_key_5&value=%23,2,10"`
# %2B is urencode('+')
echo Updated = `curl -s -X PUT -d 'key=my_key_6' -d "value=%2B,2,999" $req_url`

# %3D is urencode('=').
echo Upserted = `curl -s -X PATCH $req_url?"key=my_key_5&new_value=10&updated_value=%3D,2,10"`

# Select values from the tester
echo Selected = `curl -s $req_url?key=my_key_5`
echo Selected = `curl -s -X GET -d 'key=my_key_6' $req_url`

# Delete values from the tester
echo Deleted = `curl -s -X DELETE $req_url?key=my_key_5`
echo Deleted = `curl -s -X DELETE -d 'key=my_key_6' $req_url`

