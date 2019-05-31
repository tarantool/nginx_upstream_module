#!/usr/bin/python

import json
import urllib
import urllib2

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

# Helpers {{{
def request(req_url, args, req_type):
    try:
        url = req_url + '?' + urllib.urlencode(args)

        req = urllib2.Request(url)
        req.get_method = lambda: req_type
        res = urllib2.urlopen(req)

        out = res.read()
        out = out + res.read()

        return json.loads(out)
    except urllib2.HTTPError as e:
        return (e.code, json.loads(e.read()))

def select_eq(url, data):
    return request(url, data, 'GET')

def update(url, data):
    return request(url, data, 'PUT')

def insert(url, data):
    return request(url, data, 'POST')

def upsert(url, data):
    return request(url, data, 'PATCH')

def delete(url, data):
    return request(url, data, 'DELETE')
# }}}


#
# Test starts here
#
req_url = 'http://127.0.0.1:8081/api/v1/kv'

# ===
# insert into the 'tester'
inserted = insert(req_url, { 'key': 'my_key', 'value': 10})
# should be {"restul": [[{list of insterted values}]] }, if okay
print ('inserted = ', inserted)


# ===
# upsert (insert in this case) into the 'tester'
upserted = upsert(req_url, { 'key': 'my_key_1', 'new_value': 100,
    "updated_value": "+,2,10"})

# should be {"result": []}, if okay
print ('upserted = ', upserted)

# ===
# add 10 to the value into the 'tester'
upserted = upsert(req_url, { 'key': 'my_key_1', 'new_value': 1000,
    "updated_value": "+,2,10"})
# should be {"result": []}, if okay
print ('upserted = ', upserted)

# ===
# - 10 to the value into the 'tester'
updated = update(req_url, { 'key': 'my_key', 'value': "-,2,10"})
# should be {"result": [[{list of updated values}]]}, if okay
print ('updated = ', updated)

# ===
# Select by key
selected_1 = select_eq(req_url, {'key': 'my_key'})
selected_2 = select_eq(req_url, {'key': 'my_key_1'})
# should be {"result": [[{list of updated values}]]}, if okay
print ('selected_1 = ', selected_1)
print ('selected_2 = ', selected_2)

# ===
# Delete by key
deleted = delete(req_url, {'key': 'my_key'})
# should be {"result": [[{list of deleted values}]]}, if okay
print ('deleted = ', deleted)

deleted = delete(req_url, {'key': 'my_key_1'})
# should be {"result": [[{list of deleted values}]]}, if okay
print ('deleted = ', deleted)

