#!/usr/bin/env python
# -_- encoding: utf8 -_-

import sys
import time
sys.path.append('./t')
from http_utils import *


default_headers = {"Content-Type": "application/x-www-form-urlencoded"}
preset_method_location = BASE_URL + '/url_encoded'

# ============
#
print ('[+] Post form - 0 param')
rc, out = post_form(preset_method_location, default_headers)
assert rc == 200, "rc != 200"
assert len(out['result']) == 1, "len(result) != 1"
assert out['result'][0]["headers"]["Content-Type"] == \
        default_headers["Content-Type"], "Content-Type not equals"
print ('[+] OK')

# ============
#
print ('[+] Post form - 1 param')
rc, out = post_form(preset_method_location, {"a": "b"}, default_headers)
assert rc == 200, "rc != 200"
assert len(out['result']) == 1, "len(result) != 1"
assert out['result'][0]["headers"]["Content-Type"] == \
        default_headers["Content-Type"], "Content-Type not equals"
assert out['result'][0]['args_urlencoded'][0] == {"a": "b"}, "not expected result"
print ('[+] OK')

# ============
#
print ('[+] Post form - N param')
args = {}
expected = []
for i in range(1, 1000):
    key = 'a' + str(i)
    value = str(i)
    args[key] = value
    expected.append({key: value})

rc, out = post_form(preset_method_location, args, default_headers)
assert rc == 200, "rc != 200"
assert len(out['result']) == 1, "len(result) != 1"
assert out['result'][0]["headers"]["Content-Type"] == \
        default_headers["Content-Type"], "Content-Type not equals"
assert len(out['result'][0]['args_urlencoded']) == len(expected), 'Expected more/less data'
count = 0
from_tt = sorted(out['result'][0]['args_urlencoded'], key=lambda k: k.keys())
expected = sorted(expected, key=lambda k: k.keys())
for k in from_tt:
    if expected[count] != k:
        print ('DIFF res = ', k, ', expected = ', expected[count], \
                ' at', count)
        assert False, 'It has a diff'
    count = count + 1
print ('[+] OK')

# ============
#
print ('[+] Method ccv')
for m in {"method_1", "method_2", "method_3"}:
    preset_method_location = BASE_URL + '/method_ccv/' + m + '/comming'
    rc, out = get(preset_method_location, None, None)
    assert rc == 200, "rc != 200"
    assert out['result'][0]['uri'] == '/method_ccv/' + m + '/comming', \
        'not expected URL'
print ('[+] OK')

# ============
#
print ('[+] Headers ccv')
preset_method_location = BASE_URL + '/headers_ccv'
rc, out = get(preset_method_location, None, None)
assert rc == 200, "rc != 200"
assert out['result'][0]['headers']['Host'] == '0.0.0.0:8081', \
        'not expected host'
assert out['result'][0]['headers']['X-Str'] == 'str', \
        'X-Str is not expected'
assert out['result'][0]['headers']['X-Uri'] == '/headers_ccv', \
        'X-Uri is not expected'
print ('[+] OK')

# ============
#
print ('[+] Post form - identical params')
preset_method_location = BASE_URL + '/url_encoded'
rc, out = post_form(preset_method_location, "a=b&a=b&a=b&a=b",
        default_headers)
assert rc == 200, "rc != 200"
assert len(out['result'][0]['args_urlencoded']) == 4, 'expected 4'
for k in out['result'][0]['args_urlencoded']:
    assert k['a'] == 'b', 'not expected'
print ('[+] OK')

# ============
#
print ('[+] Post - skip_count_1')
preset_method_location = BASE_URL + '/skip_count_1'
rc, out = post(preset_method_location, {'params': [{"arg1": 1}], 'id': 1},
        default_headers)
assert rc == 200, "rc != 200"
assert len(out['result']) == 1, 'result should be an array with 2 elems'
assert out['result'][0]['arg1'] == 1, 'arg1 != 1'
print ('[+] OK')

# ============
#
print ('[+] Post - skip_count_2')
preset_method_location = BASE_URL + '/skip_count_2'
rc, out = post(preset_method_location, {'params': [{"arg1": 1}], 'id': 1},
        default_headers)
assert rc == 200, "rc != 200"
assert isinstance(out['result'], dict), 'not {}'
assert out['result']['arg1'] == 1, 'arg1 != 1'
print ('[+] OK')
