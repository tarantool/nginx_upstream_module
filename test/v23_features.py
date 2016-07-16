#!/usr/bin/python

import sys
sys.path.append('./test')
from http_utils import *

# =============
#
print('[+] Parse query args common test')

preset_method_location = BASE_URL + '/rest_api_parse_query_args'
args = {'arg1': 1, 'arg2': 2}
result = get_success(preset_method_location, args, {})
assert_query_args(result, args)

for i in range(1, 10):
    args['arg' + str(i)] = 'some_string_plus_' + str(i)
result = get_success(preset_method_location, args, {})
assert_query_args(result, args)

# =============
#
print('[+] Proto and method in result set')
result = get_success(preset_method_location, args, {})
result = result[0]
assert(result['proto'] == 'HTTP/1.1'), 'expected HTTP/1.1'
assert(result['method'] == 'GET'), 'expected GET, got'

# ============
#
print('[+] Parse query args overflow test')
for i in range(1, 100):
    args['arg' + str(i)] = 'some_string_plus_' + str(i)
(code, msg) = get_fail(preset_method_location, args, {})
assert(code == 414), 'expected http code 414'


# ============
#
print('[+] issue 44 (lua error from Tarantool)')

for suf in [ 'issue_44', 'issue_44_not_pure', 'issue_44_skip']:
  preset_method_location = BASE_URL + '/' + suf
  (code, msg) = get_fail(preset_method_location, {}, {})
  assert_if_not_error(msg, -32800)

# ============
#
print('[+] issue 43 (REST restriction)')

preset_method_location = BASE_URL + '/issue_43'
(code, msg) = get_fail(preset_method_location, {}, {})
assert(code == 405), "expected 405, got " + str(code)

preset_method_location = BASE_URL + '/issue_43'
result = post_success(preset_method_location, {
    'method':'echo_2', 'params':[[1,2,3], [4]], 'id': 1}, {})
assert (result == [1,2,3]), 'expected [1,2,3]'

preset_method_location =  BASE_URL + '/echo_1/issue_43_post_and_get'
result = post_success(preset_method_location, {
    'method':'echo_2', 'params':[[1,2,3],[4]], 'id': 1}, {})
assert (result == [1,2,3]), 'expected [1,2,3]'

preset_method_location = BASE_URL + '/echo_1/issue_43_post_and_get'

result = post_success(preset_method_location, {
    'params':[[1,2,3],[4]], 'id': 1}, {})
assert (result == [1,2,3]), 'expected [1,2,3]'

result = get_success(preset_method_location, {
  'params':[[1,2,3],[4]], 'id': 1}, {})

(code, msg) = put_fail(preset_method_location, {}, {})
assert(code == 405), 'expected 405, got ' + str(code)
