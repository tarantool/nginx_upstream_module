#!/usr/bin/python
# -_- encoding: utf8 -_-

import sys
import time
sys.path.append('./t')
from http_utils import *

please_unescape_it = BASE_URL + '/issue_120/'

# print ('[+] unescape regression test')
# args = {'arg1': 1, 'arg2': 'somestring'}
# result = get_success(please_unescape_it + '19UM|SMSO/a%7Cx%3Db', args, {})
# assert(result['uri'] == '/issue_120/19UM|SMSO/a|x=b?arg1=1&arg2=somestring'), \
#     "expected result"
# print ('[+] OK')

# print ('[+] issue 120')
# result = get_success(please_unescape_it + '%7B%7D', {}, {})
# assert(result['uri'] == '/issue_120/{}'), "expected result"
# print ('[+] OK')

print ('[+] Garbage fields skipping.')
preset_method_location = BASE_URL + '/skipping_test'
skipping_map_at_the_beginning = {
  "skipping_map_at_the_beginning": {
    "ttl": 123
  },
  "id": 1,
  "method": "skipping_test",
  "params": []
}
result = post_success_pure(preset_method_location, body, {})

skipping_map_in_the_middle = {
  "id": 1,
  "skipping_map_in_the_middle": {
    "ttl": 123
  },
  "method": "skipping_test",
  "params": []
}
skipping_map_in_the_end = {
  "id": 1,
  "method": "skipping_test",
  "params": [],
  "skipping_map_in_the_end": {
    "ttl": 123
  }
}
skipping_array_at_the_beginning = {
  "skipping_array_at_the_beginning": [
    "ttl",
    123
  ],
  "id": 1,
  "method": "skipping_test",
  "params": [],
  "meta": [
    "ttl",
    123
  ]
}
skipping_array_in_the_middle = {
  "id": 1,
  "method": "skipping_test",
  "skipping_array_in_the_middle": [
    "ttl",
    123
  ],
  "params": []
}
skipping_array_in_the_end = {
  "id": 1,
  "method": "skipping_test",
  "params": [],
  "skipping_array_in_the_end": [
    "ttl",
    123
  ]
}
skipping_map_with_nesting = {
  "skipping_map_with_nesting": {
    "ttl": {
      "ttl": 123,
      "array":[]
      }
  },
  "id": 1,
  "method": "skipping_test",
  "params": []
}
skipping_array_with_nesting = {
  "skipping_array_with_nesting": [
    "ttl",
    [
      "ttl",
      {
        "map":
        123
      }
    ]
  ],
  "id": 1,
  "method": "skipping_test",
  "params": []
}

result = post_success_pure(preset_method_location, body, {})
for item in result:
  assert (item['result'][0] == 200), "response in not 200"
print('[+] OK')
