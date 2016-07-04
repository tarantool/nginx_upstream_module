#!/usr/bin/python

import json
import urllib
import urllib2
import traceback
import sys

VERBOSE = False
BASE_URL = 'http://127.0.0.1:8081'

def get(url, data, headers):
    out = '{}'
    try:
        full_url = url
        if data:
            full_url = url + '?' + urllib.urlencode(data)
        req = urllib2.Request(full_url)
        if headers:
            for header in headers:
                req.add_header(header, headers[header])

        res = urllib2.urlopen(req)
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

def get_result(o):
    assert('result' in o), "expected 'result'"
    return o['result'][0]

def assert_if_not_error(s, code = None):
    assert('error' in s), 'expected error'
    assert('message' in s['error']), 'expected error message'
    assert('code' in s['error']), 'expected error message'
    if code:
        assert(s['error']['code'] == code), 'expected code'

def get_success(url, data, headers):
    (code, msg) = get(url, data, headers)
    assert(code == 200), 'expected 200'
    result = get_result(msg)
    return result

def get_fail(url, data, headers):
    (code, msg) = get(url, data, headers)
    return (code, msg)

def assert_query_args(result, args):
    for arg in args:
        server_arg = result[0]['args'][arg]
        assert(str(args[arg]) == server_arg), 'expected arg'


# =============
#
print('[+] Parse query args common test')

preset_method_location = BASE_URL + '/rest_api_parse_query_args'
args = {'arg1': 1, 'arg2': 2}
result = get_success(preset_method_location, args, {})
assert_query_args(result, args)

for i in range(1, 10):
    args['arg' + str(i)] = 'some_string_plus_' + str(i)
result = get_success(preset_method_location, args, {})
assert_query_args(result, args)

print('[+] Parse query args overflow test')
for i in range(1, 100):
    args['arg' + str(i)] = 'some_string_plus_' + str(i)
(code, msg) = get_fail(preset_method_location, args, {})
assert(code == 414), 'expected http code 414'

print('[+] issue 44 (lua error from Tarantool)')

for suf in [ 'issue_44', 'issue_44_not_pure', 'issue_44_skip']:
  preset_method_location = BASE_URL + '/' + suf
  (code, msg) = get_fail(preset_method_location, {}, {})
  assert(msg['error']['code'] == -32800), \
    location_suffix + 'expected code -32800, got ' + str(msg['error']['code'])
