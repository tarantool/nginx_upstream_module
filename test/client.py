#!/usr/bin/python

import json
import urllib2
import traceback

URL = 'http://127.0.0.1:8081/tnt'
VERBOSE = False

def request_raw(data):
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
    except Exception:
        if VERBOSE:
            print traceback.format_exc()
        return (500, False)

def request(data):
    return request_raw(json.dumps(data))

def echo_check(data, error_expected = True):
    (rc, res) = request(data)
    assert(rc == 200), 'echo_check expected HTTP code 200'
    assert(res != False), 'echo_check expected some data'
    assert('result' in res), 'expected result'
    if error_expected == True:
        assert(res['result'][0] == data['params'][1]), \
            'echo_check result must be same as params'
    else:
        assert('error' in res), 'expected error'
        assert('message' in res['error']), 'expected error, got'

###
# Spec. cases
(rc, res) = request_raw('{"method":"call", "params":["name", __wrong__], "id":555}');
assert(rc == 200), 'expected 200'
assert('result' in res), 'expected result'
assert('error' in res), 'expected error'
assert('message' in res['error']), 'expected error message'

(rc, res) = request_raw('');
assert(rc == 500), 'expected 500'
assert(res == False), 'expected empty reply'

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
}, False)

print '[OK] client.py'
