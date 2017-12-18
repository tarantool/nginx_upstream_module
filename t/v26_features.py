#!/usr/bin/python
# -_- encoding: utf8 -_-

import sys
import time
sys.path.append('./t')
from http_utils import *


def arr_of_dicts_to_arr(arr_of_dicts, start_from = 0):
    res = []
    for k in arr_of_dicts:
        res.append(k.values()[0])
    return res[start_from:]

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


print ('[+] insert + extented format')

data = [
    {'space_id': 513},
    {'index_id': 0},
    {'value': 1},
    {'string': 'some big string'},
    {'float': 2.1},
    {'double': 3.1},
    {'bool': True},
    {'int': -1000}
]

## tnt_insert off "space_id=%space_id&index_id=%index_id&value=%n&string=%s&float=%f&double=%d&bool=%b&int=%n";
expected = arr_of_dicts_to_arr(data)
for i in range(1, 100):
    data[2]['value'] = i
    result = get_success(BASE_URL + '/delete_ext_fmt', data, None, False)
    assert 'result' in result and 'id' in result, 'expected: result and id'
    result = get_success(BASE_URL + '/insert_ext_fmt', data, None)
    assert arr_of_dicts_to_arr(data, 2) == result, 'Expected != result'
print ('[+] OK')


print ('[+] select + extented format')

data = [
    {'space_id': 513},
    {'index_id': 0},
    {'iter': 'all'},
    {'limit': 1},
    {'offset': 0},
    {'value': 1},
    {'string': 'some big string'},
    {'float': 2.1},
    {'double': 3.1},
    {'bool': True},
    {'int': -1000}
]

## tnt_insert off "space_id=%space_id&index_id=%index_id&value=%n&string=%s&float=%f&double=%d&bool=%b&int=%n";
expected = arr_of_dicts_to_arr(data, 5)
for i in range(1, 100):
    expected[0] = i
    result = get_success(BASE_URL + '/select_ext_fmt', data, None)
    assert result == expected, "Expected != result"
    data[4]['offset'] = i

print ('[+] OK')

print ('[+] replace + extented format')

data = [
    {'space_id': 513},
    {'index_id': 0},
    {'value': 1000},
    {'string': ''},
    {'float': 0.0},
    {'double': 0.0},
    {'bool': False},
    {'int': 0}
]

## tnt_insert off "space_id=%space_id&index_id=%index_id&value=%n&string=%s&float=%f&double=%d&bool=%b&int=%n";
expected = arr_of_dicts_to_arr(data, 5)
result = get_success(BASE_URL + '/replace_ext_fmt', data, None)
assert result == arr_of_dicts_to_arr(data, 2), "Expected != result"
print ('[+] OK')


print ('[+] select + extented format w/o FMT args')

rc, result = get(BASE_URL + '/select_ext_fmt',[
    {'index_id': 0}, {'iter': 'all'}, {'limit': 1}, {'offset': 0}, ] , None)
assert rc == 400, 'Expected 400'

rc, result = get(BASE_URL + '/select_ext_fmt',[
    {'space_id': 0}, {'iter': 'all'}, {'limit': 1}, {'offset': 0}, ] , None)
assert rc == 400, 'Expected 400'

rc, result = get(BASE_URL + '/select_ext_fmt',[
    {'space_id': 0}, {'index_id': 0}, {'limit': 1}, {'offset': 0}, ] , None)
assert rc == 400, 'Expected 400'

rc, result = get(BASE_URL + '/select_ext_fmt',[
    {'space_id': 0}, {'index_id': 0}, {'iter': 'all'}, {'offset': 0}, ] , None)
assert rc == 400, 'Expected 400'

rc, result = get(BASE_URL + '/select_ext_fmt',[
    {'space_id': 0}, {'index_id': 0}, {'iter': 'all'}, {'limit': 0}, ] , None)
assert rc == 400, 'Expected 400'

print ('[+] OK')


print ('[+] select will reach a limit')
rc, result = get(BASE_URL + '/select_ext_fmt',[
    {'space_id': 10}, {'index_id': 10}, {'iter': 'ge'}, {'limit': 1000 },
    {'offset': 0}], [])
assert rc == 400, 'Expected 400'
print ('[+] OK')


print ('[+] insert + extented format w/o space_id')

data = [
    {'index_id': 0},
    {'value': 1},
    {'string': 'some big string'},
    {'float': 2.1},
    {'double': 3.1},
    {'bool': True},
    {'int': -1000}
]

rc, result = get(BASE_URL + '/insert_ext_fmt', data, None)
assert rc == 400, 'Expected 400'

print ('[+] OK')

print ('[+] insert + extented format w/o index_id')

data = [
    {'space_id': 0},
    {'value': 1},
    {'string': 'some big string'},
    {'float': 2.1},
    {'double': 3.1},
    {'bool': True},
    {'int': -1000}
]

rc, result = get(BASE_URL + '/insert_ext_fmt', data, None)
assert rc == 400, 'Expected 400'

print ('[+] OK')


print ('[+] insert + extented format w/o {index,space}_id')

data = [
    {'value': 1},
    {'string': 'some big string'},
    {'float': 2.1},
    {'double': 3.1},
    {'bool': True},
    {'int': -1000}
]

rc, result = get(BASE_URL + '/insert_ext_fmt', data, None)
assert rc == 400, 'Expected 400'

print ('[+] OK')


print ('[+] Allowed spaces and indexes')

rc, result = get(BASE_URL + '/dml_allowed_sp', [
    {'s': 512},{'i': 10},{'v': 8000}], None)
assert rc == 400, 'Expected 400'

rc, result = get(BASE_URL + '/dml_allowed_sp', [
    {'s': 5120},{'i': 0},{'v': 8000}], None)
assert rc == 400, 'Expected 400'

rc, result = get(BASE_URL + '/dml_allowed_sp', [
    {'s': 512},{'i': 0},{'v': 8000}], None)
assert rc == 200, 'Expected 400'

print ('[+] OK')

