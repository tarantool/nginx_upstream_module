#!/usr/bin/env python
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

# ============
#
print ('[+] basic insert')
result = get_success(BASE_URL + '/delete', {'index': 1}, None, False)
assert 'result' in result and 'id' in result, 'expected: result and id'
result = get_success(BASE_URL + '/delete', {'index': 2}, None, False)
assert 'result' in result and 'id' in result, 'expected: result and id'

# Format - "index=%u&string=%s&float=%f&double=%d&bool=%b&int=%i";

insert_1 = [
    {'index': 1},
    {'string': 'some big string'},
    {'float': 2.1},
    {'double': 3.1},
    {'bool': True},
    {'int': -1000}
]
result = get_success(BASE_URL + '/insert', insert_1, None)
assert result == arr_of_dicts_to_arr(insert_1), "Expected != result"

insert_2 = [
    {'index': 2},
    {'string': 'the new big and random string'},
    {'float': 20.1},
    {'double': 30.1},
    {'bool': False},
    {'int': -2}
]
result = get_success(BASE_URL + '/insert', insert_2, None)
assert result == arr_of_dicts_to_arr(insert_2), "Expected != result"
print ('[+] OK')

# ============
#
print ('[+] basic select')
result = get_success(BASE_URL + '/select', {'index': 0}, None, False)
assert arr_of_dicts_to_arr(insert_1) == result['result'][0], \
        "Expected != result"
assert arr_of_dicts_to_arr(insert_2) == result['result'][1], \
        "Expected != result"
print ('[+] OK')

# ============
#
print ('[+] basic replace')
insert_1[5]['int'] = 1000000
result = get_success(BASE_URL + '/replace', insert_1, None)
assert arr_of_dicts_to_arr(insert_1) == result, \
        "Expected != result"
print ('[+] OK')

# ============
#
print ('[+] basic delete')
result = get_success(BASE_URL + '/delete', {'index': 1}, None)
assert arr_of_dicts_to_arr(insert_1) == result, \
        "Expected != result"
result = get_success(BASE_URL + '/delete', {'index': 2}, None)
assert arr_of_dicts_to_arr(insert_2) == result, \
        "Expected != result"
print ('[+] OK')

# ============
#
print ('[+] https://github.com/tarantool/nginx_upstream_module/issues/98')
result = get_success(BASE_URL + '/error_if_escaped', {'getArg': 'a b'}, None)
assert result == True, 'Expected True'
print ('[+] OK')

# ============
#
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

expected = arr_of_dicts_to_arr(data)
for i in range(1, 100):
    data[2]['value'] = i
    result = get_success(BASE_URL + '/delete_ext_fmt', data, None, False)
    assert 'result' in result and 'id' in result, 'expected result and id'
    result = get_success(BASE_URL + '/insert_ext_fmt', data, None)
    assert arr_of_dicts_to_arr(data, 2) == result, 'Expected != result'
print ('[+] OK')

# ============
#
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

# ============
#
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

# ============
#
print ('[+] select will reach a limit')
rc, result = get(BASE_URL + '/select_ext_fmt',[
    {'space_id': 10}, {'index_id': 10}, {'iter': 'ge'}, {'limit': 1000 },
    {'offset': 0}], [])
assert rc == 400, 'Expected 400'
print ('[+] OK')

# ============
#
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

# ============
#
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

# ============
#
print ('[+] Wrong order')

data = [
    {'float': 2.1},
    {'value': 100000},
    {'space_id': 513},
    {'index_id': 0},
    {'string': 'some big string'},
    {'double': 3.1},
    {'bool': True},
    {'int': -1000}
]

expected = [
    {'value': 100000},
    {'string': 'some big string'},
    {'float': 2.1},
    {'double': 3.1},
    {'bool': True},
    {'int': -1000}
]

result = get_success(BASE_URL + '/delete_ext_fmt', data, None, False)
assert 'result' in result and 'id' in result, 'expected result and id'
result = get_success(BASE_URL + '/insert_ext_fmt', data, None)
assert arr_of_dicts_to_arr(expected) == result, 'Expected != result'

print ('[+] OK')

# ============
#
print ('[+] Format validation')
data = [
    {'space_id': 513},
    {'index_id': 'str'},
]
rc, result = get(BASE_URL + '/insert_ext_fmt', data, None)
assert rc == 400, 'Expected 400'
print ('[+] OK')

print ('[+] basic update')

result = get_success(BASE_URL + '/delete', {'index': 1}, None, False)
assert 'result' in result and 'id' in result, 'expected: result and id'

insert_1 = [
    {'index': 1},
    {'string': 'some big string'},
    {'float': 2.1},
    {'double': 3.1},
    {'bool': True},
    {'int': -1000}
]
result = get_success(BASE_URL + '/insert', insert_1, None)
assert result == arr_of_dicts_to_arr(insert_1), "Expected != result"

update = [
    {'space_id': 512},
    {'value': 1},
    {'string': '=,2,some XXXX big string'},
    {'float': '-,3,2.1'},
    {'double': '=,4,4.1'},
    {'bool': '=,5,false'},
    {'int': '%2B,6,1001'}
]

expected = [
    {'value': 1},
    {'string': 'some XXXX big string'},
    {'float': 0},
    {'double': 4.1},
    {'bool': False},
    {'int': 1}
]

result = get_success(BASE_URL + '/update_fmt', update, None)
assert result == arr_of_dicts_to_arr(expected), "Expected != result"

print ('[+] OK')

# ============
#
print ('[+] Update multipart index')

result = get_success(BASE_URL + '/delete_mt_fmt', {
    'space_id': 514,
    'index_id': 1,
    'key': 1,
    'key1': 'str',
    }, None, False)
assert 'result' in result and 'id' in result, 'expected: result and id'

insert_1 = [
    {'space_id': 514},
    {'index_id': 1},
    {'key': 1},
    {'key1': 'str'},
    {'string': 'some big string'}
]
result = get_success(BASE_URL + '/insert_mt_fmt', insert_1, None)
assert result == arr_of_dicts_to_arr([{'key': 1}, {'key1': 'str'}, \
    {'string': 'some big string'} ]),\
    "Expected != result"

update = [
    {'space_id': 514},
    {'index_id': 1},
    {'key': 1},
    {'key1': 'str'},
    {'string': '=,3,updated string'}
]

expected = [
    {'key': 1},
    {'key1': 'str'},
    {'string': 'updated string'}
]

result = get_success(BASE_URL + '/update_mt_fmt', update, None)
assert result == arr_of_dicts_to_arr(expected), "Expected != result"

print ('[+] OK')

# ============
#
print ('[+] Upsert')

upsert = [
    {'space_id': 514},
    {'key': 2},
    {'key1': 'str2'},
    {'string': '=,3,Text'}
]

result = get_success(BASE_URL + '/upsert_fmt', upsert, None, False)
assert 'result' in result and 'id' in result, 'expected: result and id'

upsert = [
    {'space_id': 514},
    {'key': 2},
    {'key1': 'str2'},
    {'string': '=,3,'}
]

result = get_success(BASE_URL + '/upsert_fmt', upsert, None, False)
assert 'result' in result and 'id' in result, 'expected: result and id'

expected = [
    {'key': 2},
    {'key1': 'str2'},
    {'string': ''}
]

result = get_success(BASE_URL + '/select_514', {'key':2, 'key1': 'str2'}, None)
assert result == arr_of_dicts_to_arr(expected), "Expected != result"

print ('[+] OK')

# ============
#
print ('[+] Issue - https://github.com/tarantool/nginx_upstream_module/issues/108')

data = []
for i in range(1, 300):
    e = {}
    e['key' + str(i)] = i
    data.append(e)
result = get_success(BASE_URL + '/issue_108', data, None, False)
assert 'headers' in result, "Expected != result"

print ('[+] OK')

# ============
#
print ('[+] Post urlencoded')

post_form_success(BASE_URL + '/delete_post', {'id': 11}, None)
post_form_success(BASE_URL + '/delete_post', {'id': 12}, None)
post_form_success(BASE_URL + '/insert_post', {'id': 11}, None)
post_form_success(BASE_URL + '/insert_post', {'id': 12}, None)
result = post_form_success(BASE_URL + '/select_post', {'id': 11}, None)
assert result['result'][0][0] == 11 and result['result'][1][0] == 12, \
        'Expected != result'
result = post_form_success(BASE_URL + '/update_post', {'id': 12, 'value': '=,2,TEXT',
    'value1': '=,3,3.14'}, None)
assert result['result'][0][0] == 12 and result['result'][0][1] == 'TEXT' \
    and result['result'][0][2] == 3.14, 'Expected != result'

print ('[+] OK')

# ============
#
print ('[+] Update format validation')

_, result = post_form(BASE_URL + '/update_post', {'id': 12, 'value': '=,TEXT',
    'value1': '=,3,3.14'}, None)
assert _ == 400 and 'error' in result, 'Expected != result'

_, result = post_form(BASE_URL + '/update_post', {'id': 12, 'value': 'TEXT',
    'value1': '=,3,3.14'}, None)
assert _ == 400 and 'error' in result, 'Expected != result'
_, result = post_form(BASE_URL + '/update_post', {'id': 12, 'value': '=,,TEXT',
    'value1': '=,4,3.14'}, None)
assert _ == 400 and 'error' in result, 'Expected != result'
_, result = post_form(BASE_URL + '/update_post', {'id': 12, 'value': '=,TEXT',
    'value1': '=,3,3.14'}, None)
assert _ == 400 and 'error' in result, 'Expected != result'

print ('[+] OK')

# ============
#
print ('''[+] Issues https://github.com/tarantool/nginx_upstream_module/issues/110 and
https://github.com/tarantool/nginx_upstream_module/issues/111''')

## Issue 111
_, result = delete(BASE_URL + '/issue_110_and_111', \
        {'key': 'test_inc'}, None, True)
assert _ == 200 and 'result' in result and 'id' in result, 'expected: result and id'

post_form_success(BASE_URL + '/issue_110_and_111', \
        {'key': 'test_inc', 'value': '10'},
        None)

result = put_success(BASE_URL + '/issue_110_and_111', \
        {'key': 'test_inc', 'value': '#,2,10'}, None, True)
assert result[0] == 'test_inc', "Expected != Result"

result = get_success(BASE_URL + '/issue_110_and_111', \
        {'key': 'test_inc'}, None)
assert result[0] == 'test_inc', "Expected != Result"


## Issue 110
_, result = delete(BASE_URL + '/issue_110_and_111', \
        {'key': 'test_inc_2'}, None, True)
assert _ == 200 and 'result' in result and 'id' in result, \
        'expected: result and id'

post_form_success(BASE_URL + '/issue_110_and_111', \
        {'key': 'test_inc_2', 'value': '1'},
        None)

_, result = patch(BASE_URL + '/issue_110_and_111', \
        {'key': 'test_inc_2', 'new_value' : 1, 'updated_value': '+,2,5'})
assert _ == 200 and 'result' in result and 'id' in result, \
        'expected: result and id'

result = get_success(BASE_URL + '/issue_110_and_111', \
        {'key': 'test_inc_2'}, None)
assert result[1] == 6, "Expected != Result"

_, result = patch(BASE_URL + '/issue_110_and_111', \
        {'key': 'test_inc_2', 'new_value' : 1, 'updated_value': '=,2,5'})
assert _ == 200 and 'result' in result and 'id' in result, \
        'expected: result and id'
result = get_success(BASE_URL + '/issue_110_and_111', \
        {'key': 'test_inc_2'}, None)
assert result[1] == 5, "Expected != Result"

print ('[+] OK')
