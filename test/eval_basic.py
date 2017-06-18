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
    '200',
    '201',
    '202',
## TODO Fix [[[
    #'204',
    #'206',
    #'300',
    #'301',
    #'302',
    #'303',
    #'304',
    #'307',

    # Does not suported by Python with GET!!! [[
    #'400',
    #'401',
    #'403',
    # ]]
## ]]]
    '404',
    '405',
    '408',
    '409',
    '411',
    '412',
    '413',
    '414',
    '415',
    '416',
    '421',
    '500',
    '501',
    '502',
    '503',
    '504',
    '507'
]

methods = [
    [post_2, 'POST'],
#    [get_2, 'GET']
]

prev_result = None

for method in methods:

    for code in http_codes:

        curl = BASE_URL + '/eval_basic?status_code=' + code
        if VERBOSE:
            print (curl)

        (rcode, result) = method[0](curl, [], [])
        assert(rcode == int(code)), 'expected ' + code

        if prev_result:

            if VERBOSE:
                print ('===============')
                print (curl, method[1])
                print (prev_result[1])
                print (result)

            # Here is we don't test those fields [[[
            prev_result[1]['args']['status_code'] = code
            prev_result[1]['uri'] = result[1]['uri']
            prev_result[1]['method'] = result[1]['method']
            prev_result[1]['headers'] = None
            result[1]['headers'] = None
            # ]]]

            if prev_result[1] != result[1]:
                print (prev_result[1])
                print (result[1])

            assert(prev_result[1] == result[1])
            assert(prev_result[2] == result[2])

        prev_result = result

print('[+] OK')


# =============
#
print('[+] Test headers codes')

print('[+] OK')
