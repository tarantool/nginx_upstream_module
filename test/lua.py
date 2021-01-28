#!/usr/bin/env python
# -_- encoding: utf8 -_-

import sys
import time
sys.path.append('./t')
from http_utils import *
import os

# Debugging flag.
VERBOSE = os.getenv('VERBOSE', False)

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
        curl = BASE_URL + '/lua?status_code=' + str(code)
        (rcode, result) = method[0](curl, code, {'X-From': 'eval_basic'})
        # Python does not work if server returns some codes!
        if rcode == True:
            continue;
        assert(code == rcode)
print('[+] OK')
