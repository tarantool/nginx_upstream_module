#!/usr/bin/env python
# -_- encoding: utf8 -_-

import sys
import time
sys.path.append('./t')
from http_utils import *

# =============
#
print('[+] Post/Put body')

preset_method_location = BASE_URL + '/pass_body'
data = {"some":"custom", "json":[]}
result = post_success(preset_method_location, data, {})
assert (json.loads(result[0]['body']) == data), 'result != data'

# =============
#
print('[+] Post body - error')
data = ""
for i in range(1, 4096):
    data = data + str(time.time())
preset_method_location = BASE_URL + '/pass_body'
rc, result = post(preset_method_location, data, {})
assert (rc == 500), "rc != 500"

# =============
#
print('[+] Body sould not pass')
preset_method_location = BASE_URL + '/dont_pass_body'
result = post_success(preset_method_location, {"body": True}, {})
assert (('body' in result) == False), "body in result"

# =============
#
print('[+] Headers out')
preset_method_location = BASE_URL + '/headers_out'
post_success(preset_method_location, {"body": True}, {})

# ============
#
print('[+] Unescape issue')
arg_a = 'some string with spaces'
preset_method_location = BASE_URL + '/unescape?a=' + arg_a
result = get_success(preset_method_location, None, {})
assert(result[0]['args']['a'] == arg_a), 'does not expected (args.a)'

# ============
#
print('[+] Post form')
preset_method_location = BASE_URL + '/form'
data = { 'a':'b', 'b':'c', 'c':'d'}
result = get_result(post_form_success(preset_method_location, data))
assert(result[0]['body'] == urllib.urlencode(data)), 'result does not match'
# ============
#
print('[+] Post large form')
preset_method_location = BASE_URL + '/form_large'
for i in range(1000):
    key = 'a' + str(i)
    data[key] = 'b'
result = get_result(post_form_success(preset_method_location, data))
assert(result[0]['body'] == urllib.urlencode(data)), "result does not matched"

# ============
#
print('[+] Post empty form')
preset_method_location = BASE_URL + '/form_nothing'
result = get_result(post_form_success(preset_method_location, {}))
assert(not 'body' in result), "result contains 'body'"

# ============
#
print('[+] Post overflow form')
preset_method_location = BASE_URL + '/form_large'
for i in range(100000):
    key = 'a' + str(i)
    data[key] = 'b'
#post_form_ec500(preset_method_location, data, None, default_print_f)

data = {'params': [{'array': []}]}
for i in range(100000):
    data['params'][0]['array'].append(i)
(code, ret) = post(BASE_URL + '/echo_big', data, None)
assert(code == 200), 'expected 200'
