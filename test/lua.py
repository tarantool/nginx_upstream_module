#!/usr/bin/python
# -_- encoding: utf8 -_-

import sys
import time
sys.path.append('./test')
from http_utils import *

VERBOSE = False
BASE_URL = 'http://0.0.0.0:8081'

# =============
#
print('[+] Test status codes')

http_codes = [
    200,
    201,
    202,
# NO-Content !!!
# 204,
    206,
# Moved !!! [[
#    300,
#    301,
#    302,
# ]]

# See others !!! [[
#   303,
# ]]

# Not modified [[
#    304,
# ]]

# Temorary redirected [[
#    307,
# ]]

    400,
    401,
    403,
    404,
    405,
    408,
    409,
    411,
    412,
    413,
    414,
    415,
    416,
    421,
    500,
    501,
    502,
    503,
    504,
    507
]

def do_post(url, code, headers):
    return post_2(url, {'params':[1, 2]}, headers)

def do_get(url, code, headers):
    # Python's urllib2 does not suppor these codes! [[
    if code > 200:
        return (True, [])
    # ]]
    return get_2(url, [], headers)

methods = [
   [do_post, 'POST'],
   [do_get, 'GET']
]

prev_result = None

for method in methods:

    for code in http_codes:

        curl = BASE_URL + '/eval_basic?status_code=' + str(code)

        (rcode, result) = method[0](curl, code, {'X-From': 'eval_basic'})

        if rcode == True:
            continue

        if VERBOSE:
            print ('===============')
            print ('req = ', curl, method[1])
            print ('rcore = ', rcode)
            print ('expected code', code)
            print ('curr = ', result)
            if prev_result:
                print ('prev = ', prev_result[1])

        if method[1] != 'GET':
            assert(rcode == code), 'expected ' + str(code)

        if prev_result:
            # Here is we don't test those fields [[[
            prev_result[1]['args']['status_code'] = str(code)
            prev_result[1]['uri'] = result[1]['uri']
            prev_result[1]['method'] = result[1]['method']
            prev_result[1]['headers'] = None
            result[1]['headers'] = None
            # ]]]

            if prev_result[1] != result[1]:
                print ('==== NOT EQUAL ====')
                print (prev_result[1])
                print (result[1])

            assert(prev_result[1] == result[1])
            assert(prev_result[2] == result[2])

        prev_result = result

print('[+] OK')


# =============
#
print('[+] Test headers')
loc = BASE_URL + '/eval_headers'
result = post_success(loc, {'params': []}, {'in_h': 1})
print('[+] OK')
