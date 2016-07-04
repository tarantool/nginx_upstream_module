#!/usr/bin/python

import sys
sys.path.append('./test')
from http_utils import *

big_args_in = {}
big_headers_in = {}
headers_in = {'My-Header': '1', 'My-Second-Header': '2'}
for i in range(1, 20):
    i_str = str(i)
    big_headers_in['My-Header-' + i_str] = i_str
    big_args_in['arg' + i_str] = i

# =============
#
print('[+] Rest interface test')

preset_method_location = BASE_URL + '/preset_method/should_not_call_this'
result = get_success(preset_method_location,
                     {'arg1': 1, 'arg2': 2},
                     headers_in)
assert_headers(result, headers_in)

rest_location = BASE_URL + '/echo_2'
result = get_success(rest_location,
                     {'arg1': 1, 'arg2': 2},
                     headers_in)
for header in headers_in:
    header_from_server = result[0]['headers'][header]
    assert(header_from_server == headers_in[header]), 'expected headers_in'

result = get_success(rest_location + '/method_does_not_exists',
                      {'arg1': 1, 'arg2': 2},
                       headers_in)
assert_headers(result, headers_in)

overflow_rest_api_location = BASE_URL + '/echo_2/overflow_rest_api'
(code, result) = get(overflow_rest_api_location, big_args_in, big_headers_in)
assert(code == 500), 'expected 500'

# =============
#
print('[+] New Put/Delete features')

preset_method_location = BASE_URL + '/preset_method'
result = put_success(preset_method_location,
                     {'params': [{"arg1": 1}], 'id': 1},
                     None)
assert(result[0]["arg1"] == 1), "expected arg1 == 1"

rest_api_location = BASE_URL + '/echo_2'
result = put_success(rest_api_location,
                     {'params': [{"arg1": 1}], 'id': 1},
                     headers_in)
assert_headers(result, headers_in)
assert(result[1]['arg1'] == 1), "expected arg1 == 1"

# =============
#
print('[+] New post features test')

post_pass_http_request_location = BASE_URL + '/post_pass_http_request'
result = post_success(post_pass_http_request_location,
                      {'method': 'echo_2', 'params': [], 'id': 1},
                      headers_in)
assert_headers(result, headers_in)

post_pass_preset_method_location = BASE_URL + '/post_preset_method'
result = post_success(post_pass_preset_method_location,
                      {'params': [], 'id': 1},
                      headers_in)
assert_headers(result, headers_in)

post_pass_http_request_location = BASE_URL + '/post_preset_method'
result = post_success(post_pass_http_request_location,
                      {'method': 'method_does_not_exists', 'params': [], 'id': 1},
                      headers_in)
assert_headers(result, headers_in)

overflow_post_api_location = BASE_URL + '/overflow_post_pass_http_request'
(code, result) = post(overflow_post_api_location, big_args_in, big_headers_in)
assert(code == 500), 'expected 500'

# ============
#
print('[+] Pure & skip count test')

result = get_success_pure(BASE_URL + "/pure_result_rest",
                          {'arg1': 1, 'arg2': 2},
                          headers_in)
assert_headers(result[0], headers_in)

result = get_success_pure(BASE_URL + "/pure_result_rest_skip_count_2",
                          {'arg1': 1, 'arg2': 2},
                          headers_in)
assert_headers_pure(result, headers_in)

result = post_success_pure(BASE_URL + "/post_pure_result",
                          {'method': 'echo_2', 'params': [1, 2], 'id': 1},
                          headers_in)
assert(result[0][0] == 1), "expected [[1,..]]"
assert(result[0][1] == 2), "expected [[..,2]]"

result = post_success_pure(BASE_URL + "/post_pure_result_skip_count_1",
                          {'method': 'echo_2', 'params': [1, 2], 'id': 1},
                          headers_in)
assert(result[0] == 1), "expected [1,..]"
assert(result[1] == 2), "expected [..,2]"

# ============
# Issue 37 test
print('[+] Issue: https://github.com/tarantool/nginx_upstream_module/issues/37')

expected = {"status":{
    "code":200,
    "text":"OK"},
    "meta":{"debug":{"front":"app-2","auth":True,"source":False},
    "exec_time":0.005933,
    "related":["/ucp/0001/accounts/account/79031234567/services"]},
    "data":{"p2":79031234568,"p1":79031234567,"account":79031234567},
    "aux":{"hello":"hello 79031234567"}}

result = get_success_pure(BASE_URL + "/ucp/read", {}, {})
diff = cmp(result, expected)
assert(diff == 0), '(GET) Issue 37 - have diff: ' + str(diff)

result = post_success_pure(BASE_URL + "/ucp",
                          {'method': 'ucp', 'params': [], 'id': 1}, {})
diff = cmp(result, expected)
assert(diff == 0), '(POST) Issue 37 - have diff: ' + str(diff)
