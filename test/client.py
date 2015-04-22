#!/usr/bin/python

import json
import urllib2
import traceback
import os

URL = 'http://127.0.0.1:8081/tnt'
VERBOSE = False

def request_raw(data):
    out = '{}'
    try:
        req = urllib2.Request(URL)
        req.add_header('Content-Type', 'application/json')

        res = urllib2.urlopen(req, data)
        out = res.read()
        rc = res.getcode()

        if VERBOSE:
            print "code: ", rc, " recv: '", out, "'"

        if rc != 500:
            return (rc, json.loads(out))

        return (rc, False)
    except urllib2.HTTPError, e:
        if e.code == 400:
            out = e.read();
        return (e.code, json.loads(out))
    except Exception:
        print traceback.format_exc()
        return (False, False)

def request(data):
    return request_raw(json.dumps(data))

def assert_if_not_error(s, code = None):
    assert('error' in s), 'expected error'
    assert('message' in s['error']), 'expected error message'
    assert('code' in s['error']), 'expected error message'
    if code:
        assert(s['error']['code'] == code), 'expected code'

def echo_check(r, bad_request_expected = False):
    (rc, res) = request(r)
    if bad_request_expected == True:
        assert(rc == 400), 'bad request expected'
        assert_if_not_error(res)
    else:
        assert(rc == 200), 'echo_check expected HTTP code 200'
        assert('result' in res), 'expected result'
        assert(res['result'][0] == r['params']), \
            'echo_check result must be same as params'

###
# Spec. cases
(rc, res) = request_raw('{"method":"call", "params":["name", __wrong__], "id":555}');
assert(rc == 400), 'expected 400'
assert_if_not_error(res, -32700)

(rc, res) = request_raw('');
assert(rc == 500), 'expected 500'

###
# nginx -> tnt request - reply cases
echo_check({
    'method': 'call',
    'params': [ 'echo', [{'a':1,'b':2,'c':[1,2,3,4,5,6,7,8,9]}] ],
    'id': 555
    })

bigarray = []
for i in range(100000): bigarray.append(i)
echo_check({
    'method': 'call',
    'params': [ 'echo', bigarray ],
    'id': 555
    }, True)


print '[OK] client.py'
