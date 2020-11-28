#!/usr/bin/env python
# -_- encoding: utf8 -_-

import sys
import time
from collections import OrderedDict
sys.path.append('./t')
from http_utils import *

please_unescape_it = BASE_URL + '/issue_120/'

print ('[+] unescape regression test')
args = {'arg1': 1, 'arg2': 'somestring'}
result = get_success(please_unescape_it + '19UM|SMSO/a%7Cx%3Db', args, {})
assert(result['uri'] == '/issue_120/19UM|SMSO/a|x=b?arg1=1&arg2=somestring'), \
    "expected result"
print ('[+] OK')

print ('[+] issue 120')
result = get_success(please_unescape_it + '%7B%7D', {}, {})
assert(result['uri'] == '/issue_120/{}'), "expected result"
print ('[+] OK')

print('[+] Garbage fields skipping.')
preset_method_location = BASE_URL + '/echo'
exp_res = {u'id': 1, u'result': [u'param']}

skipping_map_at_the_beginning = OrderedDict([
  ("skipping_map_at_the_beginning", {"ttl": 123}),
  ("id", 1),
  ("method", "echo"),
  ("params", ["param"])
])
result = post_success_pure(preset_method_location, skipping_map_at_the_beginning, {})
assert (result == exp_res), "skipping_map_at_the_beginning"

skipping_map_in_the_middle = OrderedDict([
  ("id", 1),
  ("skipping_map_in_the_middle", {"ttl": 123}),
  ("method", "echo"),
  ("params", ["param"])
])
result = post_success_pure(preset_method_location, skipping_map_in_the_middle, {})
assert (result == exp_res), "skipping_map_in_the_middle"

skipping_map_in_the_end = OrderedDict([
  ("id", 1),
  ("method", "echo"),
  ("params", ["param"]),
  ("skipping_map_in_the_end", {"ttl": 123})
])
result = post_success_pure(preset_method_location, skipping_map_in_the_end, {})
assert (result == exp_res), "skipping_map_in_the_end"

skipping_array_at_the_beginning = OrderedDict([
  ("skipping_array_at_the_beginning", ["ttl", 123]),
  ("id", 1),
  ("method", "echo"),
  ("params", ["param"]),
  ("meta", ["ttl", 123])
])
result = post_success_pure(preset_method_location, skipping_array_at_the_beginning, {})
assert (result == exp_res), "skipping_array_at_the_beginning"

skipping_array_in_the_middle = OrderedDict([
  ("id", 1),
  ("method", "echo"),
  ("skipping_array_in_the_middle", ["ttl", 123]),
  ("params", ["param"])
])
result = post_success_pure(preset_method_location, skipping_array_in_the_middle, {})
assert (result == exp_res), "skipping_array_in_the_middle"

skipping_array_in_the_end = OrderedDict([
  ("id", 1),
  ("method", "echo"),
  ("params", ["param"]),
  ("skipping_array_in_the_end", ["ttl", 123])
])
result = post_success_pure(preset_method_location, skipping_array_in_the_end, {})
assert (result == exp_res), "skipping_array_in_the_end"

skipping_map_with_nesting_1 = OrderedDict([
  ("skipping_map_with_nesting_1", {"ttl": {"ttl": 123, "array":[]}}),
  ("id", 1),
  ("method", "echo"),
  ("params", ["param"])
])
result = post_success_pure(preset_method_location, skipping_map_with_nesting_1, {})
assert (result == exp_res), "skipping_map_with_nesting_1"

skipping_array_with_nesting_2 = OrderedDict([
  ("skipping_array_with_nesting_2", ["ttl", ["ttl", {"map": 123}]]),
  ("id", 1),
  ("method", "echo"),
  ("params", ["param"])
])
result = post_success_pure(preset_method_location, skipping_array_with_nesting_2, {})
assert (result == exp_res), "skipping_array_with_nesting_2"

batch = [skipping_map_at_the_beginning, skipping_map_in_the_middle,
         skipping_map_in_the_end, skipping_array_at_the_beginning,
         skipping_array_in_the_middle, skipping_array_in_the_end,
         skipping_map_with_nesting_1, skipping_array_with_nesting_2]
result = post_success_pure(preset_method_location, batch, {})
for item in result:
  assert (item == exp_res), "batch"
print('[+] OK')
