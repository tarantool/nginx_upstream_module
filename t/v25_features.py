#!/usr/bin/python
# -_- encoding: utf8 -_-

import sys
import time
sys.path.append('./t')
from http_utils import *


default_headers = {"Content-Type": "application/x-www-form-urlencoded"}
preset_method_location = BASE_URL + '/url_encoded'


print ('[+] Post form - 0 param')
rc, out = post_form(preset_method_location, default_headers)
assert rc == 200, "rc != 200"
assert len(out['result']) == 1, "len(result) != 1"
assert out['result'][0]["headers"]["Content-Type"] == \
        default_headers["Content-Type"], "Content-Type not equals"
print ('[+] OK')


print ('[+] Post form - 1 param')
rc, out = post_form(preset_method_location, {"a": "b"}, default_headers)
assert rc == 200, "rc != 200"
assert len(out['result']) == 1, "len(result) != 1"
assert out['result'][0]["headers"]["Content-Type"] == \
        default_headers["Content-Type"], "Content-Type not equals"
assert out['result'][0]['body'] == {"a": "b"}, "not expected result"
print ('[+] OK')


print ('[+] Post form - N param')
args = {}
for i in range(1, 1000):
    args['a' + str(i)] = str(i)
rc, out = post_form(preset_method_location, args, default_headers)
assert rc == 200, "rc != 200"
assert rc == 200, "rc != 200"
assert len(out['result']) == 1, "len(result) != 1"
assert out['result'][0]["headers"]["Content-Type"] == \
        default_headers["Content-Type"], "Content-Type not equals"
assert out['result'][0]['body'] == args, "not expected result"
print ('[+] OK')


print ('[+] Method ccv')
for m in {"method_1", "method_2", "method_3"}:
    preset_method_location = BASE_URL + '/method_ccv/' + m + '/comming'
    rc, out = get(preset_method_location, None, None)
    assert rc == 200, "rc != 200"
    assert out['result'][0]['uri'] == '/method_ccv/' + m + '/comming', \
        'not expected URL'
print ('[+] OK')


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

