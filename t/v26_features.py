#!/usr/bin/python
# -_- encoding: utf8 -_-

import sys
import time
sys.path.append('./t')
from http_utils import *



print ('[+] basic insert')

result = get_success(BASE_URL + '/delete', {'index': 1}, None, False)
assert 'result' in result and 'id' in result, 'expected: result and id'
result = get_success(BASE_URL + '/delete', {'index': 2}, None, False)
assert 'result' in result and 'id' in result, 'expected: result and id'

# Format - "index=%u&string=%s&float=%f&double=%d&bool=%b&int=%i";

insert_1 = {
    'index': 1,
    'string': 'some big string',
    'float': 2.1,
    'double': 3.1,
    'bool': True,
    'int': -1000
}
result = get_success(BASE_URL + '/insert', insert_1, None)
assert [ v for v in insert_1.values() ] == result, "Expected != result"

insert_2 = {
    'index': 2,
    'string': 'the new big and random string',
    'float': 20.1,
    'double': 30.1,
    'bool': False,
    'int': -2
}
result = get_success(BASE_URL + '/insert', insert_2, None)
assert [ v for v in insert_2.values() ] == result, "Expected != result"
print ('[+] OK')


print ('[+] basic select')
result = get_success(BASE_URL + '/select', {
    'index': 0
}, None, False)
assert [ v for v in insert_1.values() ] == result['result'][0], \
        "Expected != result"
assert [ v for v in insert_2.values() ] == result['result'][1], \
        "Expected != result"
print ('[+] OK')


print ('[+] basic replace')
insert_1['int'] = 1000000
result = get_success(BASE_URL + '/replace', insert_1, None)
assert [ v for v in insert_1.values() ] == result, \
        "Expected != result"
print ('[+] OK')


print ('[+] basic delete')
result = get_success(BASE_URL + '/delete', {'index': 1}, None)
assert [ v for v in insert_1.values() ] == result, \
        "Expected != result"
result = get_success(BASE_URL + '/delete', {'index': 2}, None)
assert [ v for v in insert_2.values() ] == result, \
        "Expected != result"
print ('[+] OK')


print ('[+] https://github.com/tarantool/nginx_upstream_module/issues/98')
result = get_success(BASE_URL + '/error_if_escaped', {'getArg': 'a b'}, None)
assert result == True, 'Expected True'
print ('[+] OK')
