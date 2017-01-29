#!/usr/bin/python
# -_- encoding: utf8 -_-

import sys
import time
sys.path.append('./test')
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
assert (('body' in result[0]) == False), "body in result"

# =============
#
print('[+] Headers out')
preset_method_location = BASE_URL + '/headers_out'
post_success(preset_method_location, {"body": True}, {})
