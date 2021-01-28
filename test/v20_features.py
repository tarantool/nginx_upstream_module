#!/usr/bin/env python

import sys
sys.path.append('./t')
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


print ('[+] Test "large request"')

err_msg = { 'error': { 'message':
                        "Request too large, consider increasing your " +
                        "server's setting 'client_body_buffer_size'",
                        'code': -32001 } }

preset_method_location = BASE_URL + '/issue_59/rest_api_parse_query_args'

obj = {}
for i in range(1, 40000):
    obj[str(i) + 'some_key_name'] = [ i, { 'n': i,
                                           'some_key_name': [[1,2,3],[4]]}]
for i in range(1, 10):
    code, result = post(preset_method_location, { 'params': [obj] }, {})
    assert(code == 400), 'expected 400'

    expected = obj[str(i) + 'some_key_name']
    result = post_success(preset_method_location, { 'params': expected }, {})
    assert(result[1] == expected), 'expected != result (too large)'
print ('[+] OK')
