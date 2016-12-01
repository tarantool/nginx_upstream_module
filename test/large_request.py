#!/usr/bin/python
# -_- encoding: utf8 -_-

import sys
sys.path.append('./test')
from http_utils import *

BASE_URL = "http://0.0.0.0:8081/issue_59"
err_msg = { 'error': { 'message':
                        "Request too large, consider increasing your " +
                        "server's setting 'client_body_buffer_size'",
                        'code': -32001 } }

preset_method_location = BASE_URL + '/rest_api_parse_query_args'

obj = {}
for i in range(1, 10000):
    obj[str(i) + 'some_key_name'] = [ i, { 'n': i,
                                           'some_key_name': [[1,2,3],[4]]}]
for i in range(1, 10000):
    code, result = post(preset_method_location, { 'params': [obj] }, {})
    assert(code == 400), 'expected 400'
    assert(result == err_msg), 'expected error msg (too large)'

    expected = obj[str(i) + 'some_key_name']
    result = post_success(preset_method_location, { 'params': expected }, {})
    assert(result[1] == expected), 'expected != result (too large)'
