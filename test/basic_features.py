#!/usr/bin/env python

import json
import urllib2
import traceback
import sys

URL = 'http://127.0.0.1:8081/tnt'
VERBOSE = False

def request_raw(data):
    out = '{}'
    try:
        req = urllib2.Request(URL)
        req.add_header('Content-Type', 'application/json')

        res = urllib2.urlopen(req, data)
        out = res.read()
        out = out + res.read()
        rc = res.getcode()

        if VERBOSE:
            print "code: ", rc, " recv: '", out, "'"

        if rc != 500:
            return (rc, json.loads(out))

        return (rc, False)
    except urllib2.HTTPError, e:
        if e.code == 400:
            out = e.read();

        if VERBOSE:
            print "code: ", e.code, " recv: '", out, "'"

        return (e.code, json.loads(out))
    except Exception, e:
        print traceback.format_exc()
        return (False, e)

def request(data):
    return request_raw(json.dumps(data))

def assert_if_not_error(s, code = None):
    assert('error' in s), 'expected error'
    assert('message' in s['error']), 'expected error message'
    assert('code' in s['error']), 'expected error message'
    if code:
        assert(s['error']['code'] == code), 'expected code'

def get_id(o):
    assert('id' in o), "expected 'id'"
    return o['id']

def get_id_i(o, i):
    assert('id' in o[i]), "expected 'id'"
    return o[i]['id']

def get_result(o):
    assert('result' in o), "expected 'result'"
    return o['result'][0]

#
def batch_cases():

    print "[+] Batch cases"

    (rc, res) = request([

        { 'method': 'ret_4096',
        'params': [],
        'id': 1
        },

        {'method': 'ret_4096',
        'params': [],
        'id': 2
        }
    ])

    assert(rc == 200), 'expected 200'
    assert(len(res) == 2), 'expected 2 elements, got ' + str(len(res))
    assert(res[0]['result'][0][0][1] == '101234567891234567')
    assert(res[1]['result'][0][0][1] == '101234567891234567')

    ##
    batch = []
    for i in range(0, 100):
        batch.append(dict(
            { 'method': 'echo_1',
              'params': ["test_string_%i" % i],
              'id': i
            }
        ))

    (rc, res) = request(batch)
    assert(rc == 200), 'expected 200'
    assert(len(res) == len(batch)),\
            'expected %i elements, got %i' % (len(batch), len(res))
    for i in range(0, len(res)):
        rr = res[i]['result'][0]
        id = get_id_i(res, i)
        assert(rr[0] == batch[i]['params'][0]),\
                "expected %s, got %s" % (batch[i]['params'][0], rr[0])
        assert(id == batch[i]['id']),\
                "expected id %s, got %s" % (batch[i]['id'], id)

    ##
    (rc, res) = request([

        {'method': 'echo_2',
        'params': [{"first":1}, {"second":2}],
        'id': 1
        },

        {'method': 'ret_4096',
        'params': [],
        'id': 2
        },

        {'method': 'this_function_does_not_exists',
        'params': [],
        'id': 3
        }

    ])
    assert(rc == 200), 'expected 200'

    for rr in res:
        if rr['id'] == 3:
            assert('error' in rr or 'message' in rr), \
                'expected %s returns error/message, got %s' % (rr['id'], rr)
        elif rr['id'] == 2:
            rr_ = get_result(rr)[0]
            assert(rr_[1] == '101234567891234567')
        elif rr['id'] == 1:
            rr_ = get_result(rr)
            assert(rr_ == [{"first":1}, {"second":2}])
        else:
            assert False, "unexpected id %s" % rr['id']

    (rc, res) = request_raw('[{"method":"call", "params":["name", __wrong__], ' +
        '"id":1}, {"method":"call", "params":["name"], "id":2}]');
    assert(rc == 400), 'expected 400'
    assert('error' in res), "expected 'error' in res"
    assert('message' in res['error'] or 'code' in res['error']),\
            "expected 'message'/'code' in 'error'"

    print "[OK] Batch cases"

batch_cases()

###
# Regular cases
print "[+] Regular cases"
#
(rc, res) = request_raw('{"method":"call", "params":["name", __wrong__], "id":555}');
assert(rc == 400), 'expected 400'
print ('[+] __wrong__ OK')

# XXX BAD_REQUEST
#(rc, res) = request_raw('');
#assert(rc == 500), 'expected 500'

#
bigarray = []
for i in range(100):
    bigarray.append(i + 1)
    (rc, res) = request({
        'method': 'echo_1',
        'params': bigarray,
        'id': 1
    })
    assert(res['result'][0][0] == 1), 'expected 1'
print ('[+] Big array OK')

#
(rc, res) = request({
    'method': 'echo_1',
    'params': {"wrong": "params"},
    'id': 555
    })
assert(rc == 400), 'expected 400'
assert_if_not_error(res, -32700)
print ('[+] wrong params OK')

#
req = { 'method': 'echo_2',
    'params': [ 'echo', [{'a':1,'b':2,'c':[1,2,3,4,5,6,7,8,9]}] ],
    'id': 555
    }
(rc, res) = request(req)
assert(rc == 200), 'expected 200'
assert(get_result(res)[0] == req['params'][0])
assert(get_result(res)[1][0]['a'] == req['params'][1][0]['a'])
assert(get_result(res)[1][0]['b'] == req['params'][1][0]['b'])
assert(get_result(res)[1][0]['c'] == req['params'][1][0]['c'])

#
(rc, res) = request({
    'method': 'echo_2',
    'params': [],
    'id': 555
    })
assert(rc == 200), 'expected 200'

(rc, res) = request({
    'method': 'echo_2',
    'params': [],
    'id': 555
    })

print '[+] Regular cases'

## Segfault regress
print "[+] Regualr regress cases"
(rc, res) = request_raw('[{"method":"call", "params":["name"], "i');
assert(rc == 400), 'expected 400'
assert_if_not_error(res, -32700)

(rc, res) = request_raw('[{"');
assert(rc == 400), 'expected 400'
assert_if_not_error(res, -32700)

print '[+] Segfautl regress cases'
(rc, res) = request_raw('[{"');
assert(rc == 400), 'expected 400'
assert_if_not_error(res, -32700)

print '[+] PWN cases'
(rc, res) = request_raw('[]');
assert(rc == 400), 'expected 400'

(rc, res) = request_raw('{}');
assert(rc == 400), 'expected 400'
